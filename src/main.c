/*
 * File:   main.c
 * Author: Joan Ardiaca Jové
 *
 * Arxiu principal del codi del servidor HTTP per al TFC. Conté les rutines
 * bàsiques del programa.
 *
 * Created on 5 / abril / 2010, 16:21
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <signal.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include <time.h>
#include <locale.h>

#include "header.h"

// Configuracions (veure configuracio.c)
extern char arxiu_configuracio[MIDA_BUFFER_CONFIG];
extern int max_fils;
extern int max_fils_sobrecarrega;
extern int mida_cua_entrants;
extern char port_escolta[MIDA_PORT_ESCOLTA];
extern char log_errors[MIDA_HTTP_PATH];
extern char log_principal[MIDA_HTTP_PATH];

// Variables generals per a l'ús del thread principal
int sock_fd; // descriptor del socket escoltant
struct addrinfo *servinfo; // info adreça local
int finalitza = 0; // si val 1, acabem els fils i tanquem

// sobre els fils
int nombre_fils = 0; // nombre de treballadors executant-se
struct fil fils[MAX_ABSOLUT_FILS] = {0}; // dades dels treballadors
pthread_attr_t atributs_fils; // objecte per a establir els atributs dels fils treballadors

// Mutex
pthread_mutex_t nombre_fils_mutex = PTHREAD_MUTEX_INITIALIZER; // mutex per a modificar thread_count

// Rutines de fil.c per a l'atenció de les connexions
extern void *atendre_connexio(void *fil);
extern void *atendre_sobrecarrega(void *fil);

/*
 * modificar_thread_count()
 * Modifica la variable nombre_fils. S'ha de fer utilitzant aquesta funció ja que és una regió crítica.
 * Params:
 * variacio: variacio aplicada a nombre_fils (normalment 1 o -1)
 */
void modificar_nombre_fils(int variacio) {
    pthread_mutex_lock(&nombre_fils_mutex);
    nombre_fils += variacio;
    pthread_mutex_unlock(&nombre_fils_mutex);
}


/*
 * crear_fil()
 * Crea un nou fil d'execució que aten una connexió entrant
 * Params:
 *  descriptor: conté el descriptor del port de comunicació amb el client
 *  adreca: conté l'adreça de xarxa del client
 * Retorn: 0=OK, 1=S'exedeix el límit de fils, 2=Error de consistència, 3=Error al crear el fil
 */
int crear_fil(int descriptor, struct sockaddr_in adreca) {
    void *(*rutina_fil)(void *); // punter a la rutina d'atenció
    int i;

    // Comprova si excedim el límit de fils
    if(nombre_fils > max_fils + max_fils_sobrecarrega) return 1;

    // Cerca una tupla lliure al vector fils
    for(i=0; i < max_fils + max_fils_sobrecarrega; i++) {
        if(fils[i].pthread_obj == 0) break;
    }

    // Comprova error de coherència (aquest error no s'hauria de donar mai)
    if(i == max_fils + max_fils_sobrecarrega) return 2;

    // Preparem tupla (descriptor i adreça de xarxa)
    fils[i].descriptor = descriptor;
    fils[i].adreca_remota = adreca;

    // Comprovem si atenem la connexió o estem sobrecarregats
    if(nombre_fils < max_fils) rutina_fil = atendre_connexio;
    else rutina_fil = atendre_sobrecarrega;

    // Tractem de crear el fil d'execució, amb un punter a la tupla del vector fils com a paràmetre
    if(pthread_create(&fils[i].pthread_obj, &atributs_fils, rutina_fil, (void*) &fils[i])) {
        // Si ha fallat, netejem
        fprintf(stderr, "Fil principal - error al crear fil d'execució (%i fils executant-se): %s\n", nombre_fils, strerror(errno));
        close(descriptor);
        fils[i].descriptor = 0;
        fils[i].pthread_obj = 0;
        return 3;
    } else {
        // Si hem pogut, augmentem nombre de fils
        modificar_nombre_fils(1);
        return 0;
    }
}

/*
 * preaparar_socket()
 * Prepara el socket d'escolta
 * retorn: 0=OK, 1=Error
 */
int preparar_socket() {
    struct addrinfo *p; // Punter que utilitzem per a trobar un socket vàlid
    struct addrinfo hints; // L'usem per a obtenir els potencials sockets
    int on = 1; // L'usem per a activar opcions

    // Preparem la tupla hints
    memset(&hints, 0, sizeof(hints)); // ens assegurem que el struct es buit
    hints.ai_family = AF_INET; // usem només IPv4
    hints.ai_socktype = SOCK_STREAM; // protocol TCP
    hints.ai_protocol = 0; // qualsevol protocol
    hints.ai_flags = AI_PASSIVE; // trobar automáticament la adreça IP local

    // Obtenim addrinfo
    int retorn_gai;
    if(retorn_gai = getaddrinfo(NULL, port_escolta, &hints, &servinfo)) {
        fprintf(stderr, "Fil principal - error al preparar addrinfo: %s\n", gai_strerror(retorn_gai));
        return 1;
    }

    // passem per tots els resultats de getaddrinfo i utilitzem el primer que ens permeti obtenir un socket amb èxit
    for(p = servinfo; p != NULL; p = p->ai_next) {
        // provem si podem crear el socket, sino passem al següent
        if ((sock_fd = socket(p->ai_family, p->ai_socktype | SOCK_CLOEXEC, p->ai_protocol)) == -1) {
            fprintf(stderr, "Fil principal - error al preparar el socket: %s\n", strerror(errno));
            continue;
        }

        // activem la opció SO_REUSEADDR (evita que el SO s'ens queixi si el port està en l'estat TIME_WAIT)
        if(setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) == -1) {
            fprintf(stderr, "Fil principal - error al configurar el socket: %s.\n", strerror(errno));
            return 2;
        }

        // usem TCP_CORK, que millora el rendiment de les connexions en certes condicions (veure man sendfile)
        if(setsockopt(sock_fd, IPPROTO_TCP, TCP_CORK, &on, sizeof(on)) == -1) {
            fprintf(stderr, "Fil principal - error al configurar el socket: %s.\n", strerror(errno));
            return 2;
        }

        // provem d'associar el socket a un port, si falla passem al següent resultat
        if (bind(sock_fd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sock_fd);
            continue;
        }

        break;
    }

    if (p == NULL)  { // no hem pogut obtenir cap socket
        fprintf(stderr, "Fil principal - no s'ha pogut obrir un socket amb el port %s.\n", port_escolta);
        return 1;
    }

    return 0;
}

/*
 * neteja()
 * neteja l'entorn
 * retorn: 0=OK
 */
int neteja() {
    pthread_attr_destroy(&atributs_fils);
    freeaddrinfo(servinfo);
    return 0;
}

/*
 * handle_sigterm()
 * maneja el signal TERM (atura immediatament)
 */
void handle_sigterm(int signum) {
    fprintf(stdout, "Aturant...\n");
    neteja();
    exit(EXIT_SUCCESS);
}

/*
 * handle_sigint()
 * maneja el signal INT (atura atenent les peticions pendents)
 */
void handle_sigint(int signum) {
    finalitza = 1;
    close(sock_fd); // amb això es desbloqueja la crida a accept() del bucle principal
}

/*
 * preparar_pthread_attr()
 * prepara l'entorn per a la creació de fils (objecte pthread_attr)
 * retorn: 0=OK
 */
int preparar_pthread_attr() {
    pthread_attr_init(&atributs_fils);
    pthread_attr_setstacksize (&atributs_fils, MIDA_STACK_FIL); // mida del stack
    pthread_attr_setscope(&atributs_fils, PTHREAD_SCOPE_PROCESS); // fils de procés
    pthread_attr_setdetachstate(&atributs_fils, PTHREAD_CREATE_DETACHED); // fils "detached" (no joinable, no hem de vigilar quan finalitzen)

    return 0;
}

/*
 * daemonitza()
 * Modifica l'entorn per a que el servidor s'executi com un dimoni. Si falla,
 * aborta el programa.
 */
void daemonitza() {
    int i;

    fprintf(stdout, "L'execució continua en segon pla.\n");

    // redirigeix stdin a /dev/null
    close(0);
    if(open("/dev/null", O_RDWR) == -1) {
        fprintf(stderr, "Fil principal - error al obrir /dev/null: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    // redirigeix stderr a log d'errors
    close(2);
    if(open(log_errors, O_WRONLY | O_APPEND | O_CREAT, S_IRUSR|S_IWUSR|S_IRGRP) == -1) {
        fprintf(stdout, "Fil principal - error al obrir el log d'errors (%s): %s\n", log_errors, strerror(errno));
    }

    // redirigeix stdout a log princpal
    close(1);
    if(open(log_principal, O_WRONLY | O_APPEND | O_CREAT, S_IRUSR|S_IWUSR|S_IRGRP) == -1) {
        fprintf(stderr, "Fil principal - error al obrir el log principal (%s): %s\n", log_principal, strerror(errno));
    }

    // forcem l'execució en segon pla deixant orfe al procés
    i = fork();
    if (i < 0) { // error al fork
        fprintf(stderr, "Fil principal - error al fork inicial.\n");
        exit(EXIT_FAILURE);
    }
    if (i > 0) {
        exit(EXIT_SUCCESS); // el pare finalitza
    }
}

/*
 * main()
 * Rutina principal: prepara l'entorn, entra en un bucle que atén les peticions
 * entrants i neteja l'entorn al acabar.
 */
int main(int argc, char** argv) {
    struct sockaddr_in adr_remota; // Variable per a desar temporalment les adreces remotes
    socklen_t sizeof_adr_remota = sizeof adr_remota;; // Mida de adr_remota
    int nou_descriptor; // Per a desar temporalment els descriptors que es van creant

    printf("%s - Joan Ardiaca Jové 2010\n", NOM_SERVIDOR);

    // Apliquem una máscara als permisos dels arxius que pugui crear el servidor
    umask(027);

    // Signal handlers
    signal(SIGINT, handle_sigint); // Tractem INT (surt immediatament)
    signal(SIGTERM, handle_sigterm); // Tracterm TERM (deixa acabar peticions pendents i surt)
    signal(SIGPIPE, SIG_IGN); // Ignorem pipes trencades (és normal que passi)

    // Llegim les configuracions
    if(argc > 2) strncpy(arxiu_configuracio, argv[2], sizeof(arxiu_configuracio));
    if(llegir_configuracio()) exit (EXIT_FAILURE);

    // Daemonitzem si s'ha indicat així en el primer paràmetre
    if(argc > 1) {
        if(strcmp(argv[1], "n") == 0 || strcmp(argv[1], "normal") == 0) {
            // execució normal, no facis res
        } else if(strcmp(argv[1], "d") == 0 || strcmp(argv[1], "daemon") == 0) {
            daemonitza(); // daemonitza
        } else {
            printf("Argument incorrecte: %s. Hauria de ser 'normal' per a iniciar el servidor normalment o 'daemon' per a iniciar-lo en mode dimoni.", argv[1]);
            exit(EXIT_FAILURE);
        }
    }

    // Premarem el socket per a esperar peticions
    if(preparar_socket()) exit (EXIT_FAILURE);

    // Preparem l'entorn per a fils
    if(preparar_pthread_attr()) exit (EXIT_FAILURE);

    // Posem al socket a escoltar
    if(listen(sock_fd, mida_cua_entrants)) {
        fprintf(stderr, "Fil principal - error al posar el socket en mode escoltar: %s\n", strerror(errno));
        exit (EXIT_FAILURE);
    }

    fprintf(stdout, "Servidor HTTP iniciat, esperant connexions entrants pel port %s...\n", port_escolta);

    // Bucle principal
    while(finalitza == 0) {
        // Esperem connexions entrants
        nou_descriptor = accept4(sock_fd, (struct sockaddr *) &adr_remota, &sizeof_adr_remota, SOCK_CLOEXEC);

        if(nou_descriptor < 0) { // Hi ha hagut un error
            if(errno == EBADF && finalitza == 1) break; // Si el port és tancat perque estem acabant, seguim silenciosament
            else if(errno == EFAULT || errno == EINVAL || errno == ENOTSOCK || errno == EOPNOTSUPP) { // Error irrecuperable
                fprintf(stderr, "Fil principal - error irrecuperable al esperar connexió: %s\n", strerror(errno));
                break;
            } else fprintf(stderr, "Fil principal - error al acceptar connexió: %s\n", strerror(errno)); // Error recuperable
        } else { // Hem rebut una connexió
            crear_fil(nou_descriptor, adr_remota);
        }

    }

    // Netejem l'entorn
    neteja();

    printf("Sortint. S'atendran les peticions pendents.\n\n");
    pthread_exit(NULL); // Fem això (enlloc de return o exit) per a que els fils que s'estiguin executant no s'acabin al acabar la rutina principal
}
