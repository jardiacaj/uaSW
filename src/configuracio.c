/*
 * File:   configuracio.c
 * Author: Joan Ardiaca Jové
 *
 * Arxiu principal del codi del servidor HTTP per al TFC.
 *
 * Created on 5 / abril / 2010, 16:21
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>

#include "header.h"

// Variables per a desar les configuracions, s'inicialitzen als valors per defecte
char arxiu_configuracio[MIDA_HTTP_PATH] = DEF_ARXIU_CONFIGURACIO; // ruta de l'arxiu de configuració
int max_fils = DEF_MAX_FILS; // nombre màxim de fils d'execució d'atenció de connexions
int max_fils_sobrecarrega = DEF_MAX_FILS_SOBRECARREGA; // nombre màxim de fils d'execució de sobrecàrrega
int mida_cua_entrants = DEF_MIDA_CUA_ENTRANTS; // mida de la cua de connexions entrants (veure man listen)
char port_escolta[MIDA_PORT_ESCOLTA] = DEF_PORT_ESCOLTA; // nombre del port pel qual el servidor esperarà connexions
int temps_espera = DEF_TEMPS_ESPERA; // temps d'espera fins a rebre una petició HTTP
char directori_documents[MIDA_HTTP_PATH] = DEF_DIRECTORI_DOCUMENTS; // directori dels documents que es serviran
char arxiu_portada[MIDA_BUFFER_CONFIG] = DEF_ARXIU_PORTADA; // arxiu que s'accedirà quan es rebi una petició a un directori
char host[MIDA_BUFFER_CONFIG] = DEF_HOST; // host del servidor
char directori_documents_error[MIDA_HTTP_PATH] = DEF_DIRECTORI_DOCUMENTS_ERRORS; // directori dels documents que es mostraràn en cas d'error
char charset[MIDA_CHARSET] = DEF_CHARSET; // conjunt de caràcters dels documents
char ruta_cgi[MIDA_HTTP_PATH] = DEF_RUTA_CGI; // subdirectori amb els programes CGI
char log_errors[MIDA_HTTP_PATH] = DEF_LOG_ERRORS; // arxiu de registre d'errors
char log_principal[MIDA_HTTP_PATH] = DEF_LOG_PRINCIPAL; // arxiu de registre principal
char arxiu_mime_types[MIDA_HTTP_PATH] = DEF_RUTA_MIME_TYPES; // arxiu amb els tipus mime per extensió
struct tipus_mime mimes[MAX_MIME_TYPES] = {0}; // vector que conté els diferents tipus mime llegits en una tupla
int num_mimes = 0; // nombre de tipus mime llegits

/*
 * llegir_configuracio()
 * Llegeix l'arxiu de configuració i crida la rutina de lectura dels tipus MIME.
 */
int llegir_configuracio() {
    FILE *arxiu_config; // Arxius de configuració
    char buffer[MIDA_BUFFER_CONFIG]; // Buffer on copiem les línies de text
    char config[MIDA_BUFFER_CONFIG]; // L'usem per a desar els noms de les configuracions llegides
    char valor_str[MIDA_BUFFER_CONFIG]; // L'usem per a desar els valors en cadenes
    int valor_int; // L'usem per a desar els valors enters
    int fallat = 0; // Indica si alguna configuració ha fallat

    // Obrim l'arxiu
    arxiu_config = fopen(arxiu_configuracio, "r");
    if(arxiu_config == NULL) {
        fprintf(stderr, "Configuració (%s) - no s'ha pogut obrir l'arxiu de configuració: %s\n", arxiu_configuracio, strerror(errno));
        return 1;
    }

    // Llegim línia per línia
    while (fgets(buffer, MIDA_BUFFER_CONFIG, arxiu_config) != NULL) {
        if(strlen(buffer) < 3) continue; // Si la lectura és tan curta, probablement sigui una línia en blanc (o invàlida)
        sscanf(buffer, "%s ", config); // Llegim la primera paraula

        if (buffer[0] == '#' || strlen(config) == 0) { // Ignorem comentaris i línies buides
            continue;

        // Legim, comprovem i desem configuracions
        } else if (strcmp(config, "PortEscolta") == 0) {
            sscanf(buffer, "%s %100s\n", config, valor_str);
            strcpy (port_escolta, valor_str);

        } else if (strcmp(config, "TempsEspera") == 0) {
            sscanf(buffer, "%s %d\n", config, &valor_int);
            if(valor_int < 1) {
                fprintf(stderr, "Configuració (%s) - configuració invàlida: TempsEspera\n", arxiu_configuracio);
                fallat = 1;
            } else temps_espera = valor_int;

        } else if (strcmp(config, "MaxFils") == 0) {
            sscanf(buffer, "%s %d\n", config, &valor_int);
            if(valor_int < 1 || valor_int + max_fils_sobrecarrega > MAX_ABSOLUT_FILS) {
                fprintf(stderr, "Configuració (%s) - configuració invàlida: MaxFils\n", arxiu_configuracio);
                fallat = 1;
            } else max_fils = valor_int;

        } else if (strcmp(config, "MaxFilsSobrecarrega") == 0) {
            sscanf(buffer, "%s %d\n", config, &valor_int);
            if(valor_int < 0 || valor_int + max_fils > MAX_ABSOLUT_FILS) {
                fprintf(stderr, "Configuració (%s) - configuració invàlida: MaxFilsSobrecarrega\n", arxiu_configuracio);
                fallat = 1;
            } else max_fils_sobrecarrega = valor_int;

        } else if (strcmp(config, "MidaCuaEntrants") == 0) {
            sscanf(buffer, "%s %d\n", config, &valor_int);
            if(valor_int < 1) {
                fprintf(stderr, "Configuració (%s) - configuració invàlida: MidaCuaEntrants\n", arxiu_configuracio);
                fallat = 1;
            } else mida_cua_entrants = valor_int;

        } else if (strcmp(config, "DirectoriDocuments") == 0) {
            sscanf(buffer, "%s %200s\n", config, valor_str);
            strcpy(directori_documents, valor_str);

        } else if (strcmp(config, "ArxiuPortada") == 0) {
            sscanf(buffer, "%s %200s\n", config, valor_str);
            strcpy(arxiu_portada, valor_str);

        } else if (strcmp(config, "Host") == 0) {
            sscanf(buffer, "%s %200s\n", config, valor_str);
            strcpy(host, valor_str);

        } else if (strcmp(config, "DirectoriDocumentsError") == 0) {
            sscanf(buffer, "%s %200s\n", config, valor_str);
            strcpy(directori_documents_error, valor_str);

        } else if (strcmp(config, "CharacterSet") == 0) {
            sscanf(buffer, "%s %10s\n", config, valor_str);
            strcpy(charset, valor_str);

/*
        } else if (strcmp(config, "PermetreGZIP") == 0) {
            sscanf(buffer, "%s %d\n", config, &valor_int);
            if(valor_int != 0 && valor_int != 1) {
                printf("Configuració invàlida: PermetreGZIP\n");
                fallat = 1;
            } else permetre_gzip = valor_int;
*/

        } else if (strcmp(config, "RutaCGI") == 0) {
            sscanf(buffer, "%s %200s\n", config, valor_str);
            strcpy(ruta_cgi, valor_str);

        } else if (strcmp(config, "LogErrors") == 0) {
            sscanf(buffer, "%s %200s\n", config, valor_str);
            strcpy(log_errors, valor_str);

        } else if (strcmp(config, "LogPrincipal") == 0) {
            sscanf(buffer, "%s %200s\n", config, valor_str);
            strcpy(log_principal, valor_str);

        } else if (strcmp(config, "MimeTypes") == 0) {
            sscanf(buffer, "%s %200s\n", config, valor_str);
            strcpy(arxiu_mime_types, valor_str);

        } else {
            fprintf(stderr, "Configuració (%s) - configuració errònia: %s", arxiu_configuracio, buffer);
            fallat = 1;
        }
    }

    fclose(arxiu_config);
    if(fallat) return 2;

    // llegim tipus mime
    if(llegir_tipus_mime()) return 3;
    return 0;
}


/*
 * llegir_tipus_mime()
 * Llegeix l'arxiu mime_types.txt
 */
int llegir_tipus_mime() {
    FILE *arxiu_mime; // Arxius de configuració
    char buffer[MIDA_BUFFER_CONFIG]; // Buffer on copiem les línies de text
    char format_lectura[20]; // buffer temporal per a generar el format de lectura (sscanf)
    sprintf(format_lectura, "%%%is %%%i[^\r\n]", MIDA_EXTENSIO, MIDA_MIME); // generem el format de lectura
    num_mimes = 0;

    // Obrim l'arxiu
    arxiu_mime = fopen(arxiu_mime_types, "r");
    if(arxiu_mime == NULL) {
        fprintf(stderr, "Configuració (%s) - no s'ha pogut obrir l'arxiu de mime-types: %s\n", arxiu_mime_types, strerror(errno));
        return 1;
    }

    // Llegim línia per línia
    while (fgets(buffer, MIDA_BUFFER_CONFIG, arxiu_mime) != NULL && num_mimes < MAX_MIME_TYPES) {
        if(strlen(buffer) < 3) continue; // Si la lectura és tan curta, probablement sigui una línia en blanc (o invàlida)

        if (buffer[0] == '#') { // Ignorem comentaris
            continue;
        } else { // Legim i desem tipus mime
            sscanf(buffer, format_lectura, mimes[num_mimes].extensio, mimes[num_mimes].mime);
            num_mimes++;
        }
    }

    // Si hem omplert el vector, generem un avís, ja que possiblement no s'hagi llegit l'arxiu sencer
    if(num_mimes == MAX_MIME_TYPES) fprintf(stderr, "Configuració (%s) - Atenció: s'ha omplert el vector de tipus MIME (%i entrades). Probablement no s'hagi pogut carregar el arxiu de tipus MIME sencer.\n", arxiu_mime_types, MAX_MIME_TYPES);

    fclose(arxiu_mime);
    return 0;
}