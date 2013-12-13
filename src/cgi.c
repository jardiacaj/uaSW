/*
 * File:   cgi.c
 * Author: Joan Ardiaca Jové
 *
 * Rutines per a l'execució de programes CGI
 *
 * Created on 5 / abril / 2010, 16:21
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <sys/sendfile.h>
#include <wait.h>
#include <fcntl.h>

#include "header.h"


// Configuracions (veure configuracio.c)
extern char host[MIDA_BUFFER_CONFIG];
extern char charset[MIDA_CHARSET];
extern char port_escolta[MIDA_PORT_ESCOLTA];

// Variables de main.c
extern int descriptors_fils[MAX_ABSOLUT_FILS]; // descriptors dels sockets oberts (un per fil)
extern struct sockaddr_in adreces_remotes[MAX_ABSOLUT_FILS]; // adreces de les connexions entrants



/*
 * executar_cgi()
 * Executa un script CGI (fent un fork, on el nou procés te el stdout connectat al socket de sortida)
 * Params:
 *  fil: punter a la tupla del fil
 *  ruta_recurs: ruta del script
 * retorn: -1=error, 0=ok
 */
int executar_cgi(struct fil *fil, char* ruta_recurs) {
    char buffer_out[MIDA_BUFFER_SORTIDA] = {0}; // buffer de sortida
    int offset_out = 0; // offset en el buffer de sortida
    char buffer_temps[MIDA_DATA] = {0}; // buffer per a desar data

    FILE *recurs; // punter al arxiu del programa CGI
    int fd_recurs; // descriptor al arxiu del programa CGI
    int pipefd[2]; // pipe
    int pid_fork; // pid del procés fill

    // Obrim el recurs
    if((recurs = fopen(ruta_recurs, "r")) == NULL) { // r = lectura
        fprintf(stderr, "Execució CGI (%s) - error al obrir l'arxiu: %s\n", ruta_recurs, strerror(errno));
        enviar_error(fil, HTTP_FORBIDDEN);
        return -1;
    }

    // Obtenim el descriptor
    if((fd_recurs = fileno(recurs)) < 0) {
        fprintf(stderr, "Execució CGI (%s) - error al obtenir el descriptor: %s\n", ruta_recurs, strerror(errno));
        fclose(recurs);
        enviar_error(fil, HTTP_INTERNAL_SERVER_ERROR);
        return -1;
    }

    // Construim i enviem status-line i capçaleres
    sprintf(buffer_out, "%s 200 OK\r\nServer: %s\r\nDate: %s\r\n",
                        PROTOCOL, NOM_SERVIDOR, buffer_temps);
    offset_out = strlen(buffer_out);
    send(fil->descriptor, buffer_out, offset_out, MSG_MORE);

    // Creem pipe
    if(pipe(pipefd) < 0) {
        fprintf(stderr, "Execució CGI (%s) - error al crear la pipe: %s\n", ruta_recurs, strerror(errno));
        fclose(recurs);
        return -1;
    }

    // Creem procés fill
    if((pid_fork = fork()) < 0) {
        fprintf(stderr, "Execució CGI (%s) - error al crear procés fill: %s\n", ruta_recurs, strerror(errno));
        fclose(recurs);
        return -1;
    }

    if(pid_fork == 0) { // sóm al fill
        char *envarr[20]; // vector de punters a string que conté les variables d'entorn que tindrá el script
        char *argarr[] = {"cgi", (char*) 0}; // Arguments que rebrà el script
        char *punter; // punter temporal

        // Preparem variables d'entorn (vector de punters a cadenes acabat en NULL)
        punter = malloc(MIDA_MAX_HTTP_HEADER_VALUE);
        sprintf(punter, "AUTH_TYPE=");
        envarr[0] = punter;

        punter = malloc(MIDA_MAX_HTTP_HEADER_VALUE);
        sprintf(punter, "CONTENT_LENGHT=%li", fil->peticio.content_length);
        envarr[1] = punter;

        punter = malloc(MIDA_MAX_HTTP_HEADER_VALUE);
        sprintf(punter, "CONTENT_TYPE=%s", fil->peticio.content_type);
        envarr[2] = punter;

        punter = malloc(MIDA_MAX_HTTP_HEADER_VALUE);
        sprintf(punter, "GATEWAY_INTERFACE=CGI/1.1");
        envarr[3] = punter;

        punter = malloc(MIDA_MAX_HTTP_HEADER_VALUE);
        sprintf(punter, "PATH_INFO=");
        envarr[4] = punter;

        punter = malloc(MIDA_MAX_HTTP_HEADER_VALUE);
        sprintf(punter, "PATH_TRANSLATED=");
        envarr[5] = punter;

        punter = malloc(MIDA_MAX_HTTP_HEADER_VALUE);
        sprintf(punter, "QUERY_STRING=%s", fil->peticio.http_query_string);
        envarr[6] = punter;

        punter = malloc(MIDA_MAX_HTTP_HEADER_VALUE);
        sprintf(punter, "REMOTE_ADDR=");
        envarr[7] = punter;

        punter = malloc(MIDA_MAX_HTTP_HEADER_VALUE);
        sprintf(punter, "REMOTE_HOST=");
        envarr[8] = punter;

        punter = malloc(MIDA_MAX_HTTP_HEADER_VALUE);
        sprintf(punter, "REMOTE_IDENT=");
        envarr[9] = punter;

        punter = malloc(MIDA_MAX_HTTP_HEADER_VALUE);
        sprintf(punter, "REMOTE_USER=");
        envarr[10] = punter;

        punter = malloc(MIDA_MAX_HTTP_HEADER_VALUE);
        sprintf(punter, "REQUEST_METHOD=");
        envarr[11] = punter;

        punter = malloc(MIDA_MAX_HTTP_HEADER_VALUE);
        sprintf(punter, "SCRIPT_NAME=");
        envarr[12] = punter;

        punter = malloc(MIDA_MAX_HTTP_HEADER_VALUE);
        sprintf(punter, "SERVER_NAME=%s", host);
        envarr[13] = punter;

        punter = malloc(MIDA_MAX_HTTP_HEADER_VALUE);
        sprintf(punter, "SERVER_PORT=%s", port_escolta);
        envarr[14] = punter;

        punter = malloc(MIDA_MAX_HTTP_HEADER_VALUE);
        sprintf(punter, "SERVER_PROTOCOL=%s", PROTOCOL);
        envarr[15] = punter;

        punter = malloc(MIDA_MAX_HTTP_HEADER_VALUE);
        sprintf(punter, "SERVER_SOFTWARE=%s", NOM_SERVIDOR);
        envarr[16] = punter;

        punter = malloc(MIDA_MAX_HTTP_HEADER_VALUE);
        sprintf(punter, "");
        envarr[17] = punter;
        
        envarr[18] = NULL;

        // Preparem els descriptors, connectant el stdin a la pipe i el stdout al socket
        close(0);
        close(1);
        dup(pipefd[0]);
        dup(fil->descriptor);

        // Tanquem la pipe
        close(pipefd[0]);
        close(pipefd[1]);

        // Canviem el directori de treball al directori del script
        char directori_programa[MIDA_HTTP_PATH]; // directori del programa CGI
        char *ultima_barra; // punter temporal per a generar el directori
        strcpy(directori_programa, ruta_recurs);
        ultima_barra = strrchr(directori_programa, '/'); // cerquem la última barra en la ruta
        *ultima_barra = '\0'; // la substituim per un fi de cadena
        chdir(directori_programa); // canviem el directori

        // Executem el script
        fexecve(fd_recurs, argarr, envarr);

        // Si hem arribat aquí, ha succeït un error
        printf("\n500 Internal Server Error (%s)", strerror(errno));
        exit(EXIT_FAILURE);

    } else { // sóm al procés original
        // Tanquem la sortida del pipe
        close(pipefd[0]);

        // Escrivim el entity al script per la pipe
        if(write(pipefd[1], fil->peticio.http_entity, strlen(fil->peticio.http_entity)) < 0) {
            fprintf(stderr, "Execució CGI (%s) - error al enviar entity: %s", ruta_recurs, strerror(errno));
        }

        // Tanquem recurs
        fclose(recurs);

        // Esperem a la finalització del fill
        if(waitpid(pid_fork, NULL, 0) < 0) {
            fprintf(stderr, "Execució CGI (%s) - error al esperar al fill: %s", ruta_recurs, strerror(errno));
            return -1;
        }

        // Tanquem pipe i acabem
        close(pipefd[1]);
        return 0;
    }

}

