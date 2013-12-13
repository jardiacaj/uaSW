/*
 * File:   fil.c
 * Author: Joan Ardiaca Jové
 *
 * Rutines dels fils d'execució (atenció de peticions)
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
#include <time.h>
#include <netinet/tcp.h>

#include "header.h"

// Configuracions (veure configuracio.c)
extern int temps_espera;
extern char directori_documents[MIDA_HTTP_PATH];
extern char arxiu_portada[MIDA_BUFFER_CONFIG];
extern char host[MIDA_BUFFER_CONFIG];
extern char directori_documents_error[MIDA_HTTP_PATH];
extern char charset[MIDA_CHARSET];
extern char port_escolta[MIDA_PORT_ESCOLTA];
extern char ruta_cgi[MIDA_HTTP_PATH];
extern struct tipus_mime mimes[MAX_MIME_TYPES];
extern int num_mimes;

// Variables de main.c
extern struct fil fils[MAX_ABSOLUT_FILS];
extern int finalitza;
extern void modificar_nombre_fils(int variacio);

// De cgi.c
extern int executar_cgi(struct fil *fil, char* ruta_recurs);


/*
 * deduir_mime()
 * retorna el tipus MIME d'un fitxer segons la seva extenció, comprovant els
 * tipus MIME carregats
 * Params:
 * ruta_recurs: ruta al fitxer
 * Retorna un punter a una cadena que conté el tipus MIME
 */
char *deduir_mime(char ruta_recurs[]) {
    char *extensio;
    int i = 0;

    extensio = strrchr(ruta_recurs, '.');
    extensio++;
    if(extensio == NULL || strlen(extensio) >= MIDA_EXTENSIO) return "text/plain";

    while(i < num_mimes && strcmp(extensio, mimes[i].extensio)) i++;

    if(i == num_mimes) return "text/plain";

    return mimes[i].mime;
}


/*
 * uncork()
 * força l'enviament immediat de dades pendents en el socket, activant i
 * desactivant seguidament l'opció TCP_CORK del socket
 * Params:
 * fil: punter a la tupla del fil d'execució
 */
void uncork(struct fil *fil) {
    int val = 0;
    setsockopt(fil->descriptor, IPPROTO_TCP, TCP_CORK, &val, sizeof(val));
    val = 1;
    setsockopt(fil->descriptor, IPPROTO_TCP, TCP_CORK, &val, sizeof(val));
}

/*
 * acabar_fil()
 * finalitza un fil d'execucio, alliberant els recursos
 * Params:
 * fil: punter a la tupla del fil d'execució
 */
void acabar_fil(struct fil *fil) {
    // uncork
    uncork(fil);
    // tanquem la connexió
    close(fil->descriptor);
    // netejem estructura de dades
    memset(fil, 0, sizeof(*fil));
    // actualitzem el comptador de fils
    modificar_nombre_fils(-1);
    // i finalitzem el fil
    pthread_exit(NULL);
}

/*
 * enviar_arxiu()
 * Enviar un arxiu per un socket utilitzant sendfile()
 * Params:
 *  ruta_recurs: ruta al arxiu a enviar
 *  fil: punter a la tupla del fil d'execució
 *  *offset: nombre del byte a partir del qual llegir (pot ser NULL)
 *  bytes_a_enviar: nombre de bytes a enviar de l'arxiu
 * retorn: 0=OK, 1=Error al orbir l'arxiu, 2=Error al enviar l'arxiu
 */
int enviar_arxiu(char ruta_recurs[], struct fil *fil, off_t *offset, size_t bytes_a_enviar) {
    FILE *recurs; // punter al recurs
    int fd_recurs; // descriptor del recurs

    // Obrim el recurs
    if((recurs = fopen(ruta_recurs, "re")) == NULL) { // r = lectura, e = close on exec
        fprintf(stderr, "Treballador - Error al obrir el arxiu (%s): %s\n", ruta_recurs, strerror(errno));
        return 1;
    }

    // Obtenim el descriptor
    if((fd_recurs = fileno(recurs)) < 0) {
        fprintf(stderr, "Treballador - error al obtenir el descriptor del recurs: (%s): %s\n", ruta_recurs, strerror(errno));
        return 1;
    }

    // Enviem l'arxiu
    if(sendfile(fil->descriptor, fd_recurs, offset, bytes_a_enviar) < 0) {
        fprintf(stderr, "Treballador - error al enviar l'arxiu amb la crida a sendfile(): (%s): %s\n", ruta_recurs, strerror(errno));
        return 2;
    }

    // Tanquem l'arxiu i retornem
    fclose(recurs);
    return 0;
}

/* analitzar_peticio()
 * Analitza una petició HTTP, desant-la en un struct peticio_http
 * Params:
 *  fil: punter a la tupla del fil d'execució
 *  buffer_in[]: buffer que conté al menys una petició HTTP
 * retorn: veure header.h, positiu=OK (nombre de bytes analitzats)
 */
int analitzar_peticio(struct fil *fil, char buffer_in[]) {
    char http_header_field[MIDA_HTTP_HEADER_FIELD] = {0}; // buffer per a desar el nom d'un camp del encapçalament
    char http_header_value[MIDA_MAX_HTTP_HEADER_VALUE] = {0}; // buffer per a desar el valor d'un camp del encapçalament
    char format[30]; // buffer per a format
    int resultat_scan = 0; // desa el resultat de la crida a sscanf
    char *linia; // punter per a analitzar línia per línia (apunta a l'inici de la línia actual)

    // Processem la primera línia
    if((linia = buffer_in) == NULL) {
        return AP_INVALIDA;
    }
    sprintf(format, "%%%is %%%is %%%is", MIDA_HTTP_METHOD, MIDA_HTTP_PATH, MIDA_HTTP_VERSION); // prepara format per a sscanf
    resultat_scan = sscanf(linia, format, fil->peticio.http_method, fil->peticio.http_path, fil->peticio.http_protocol); // escaneja la línia principal

    // Comprovem que hem rebut els tres camps (method, path i protocol)
    if(resultat_scan < 3) {
        return AP_INVALIDA;
    }

    // Comprovem protocol (si no és HTTP/1.1, suposem que no podrem continuar
    // la comunicació, per tant finalitzem la connexió immediatament.
    if(strcmp(fil->peticio.http_protocol, PROTOCOL) != 0) {
        return AP_VERSIO_NO_SUPORTADA;
    }

    // Comprovem i processem query-string (contingut després del caràcter '?' a l'URI)
    char *posicio_interrogant = strstr(fil->peticio.http_path, "?");
    if(posicio_interrogant != NULL) {
        *posicio_interrogant = '\0';
        strncpy(fil->peticio.http_query_string, &posicio_interrogant[1], sizeof(fil->peticio.http_query_string) - 1);
    }
    
    sprintf(format, "%%%is %%%i[^\r\n]", MIDA_HTTP_HEADER_FIELD, MIDA_MAX_HTTP_HEADER_VALUE); // prepara format per a sscanf

    // Analitzem encapçalaments
    while((linia = strstr(linia, "\r\n")) != NULL) { // línia per línia
        linia += 2; // obviem CRLF
        if(strncmp("\r\n", linia, 2) == 0) break; // si la línia és buida, hem arribat al final de les capçaleres

        sscanf(linia, format, http_header_field, http_header_value); // escaneja línia

        // Comprovem quin encapçalament és i el processem (es troben comentades les capçaleres que no s'analitzen)
        if (strcasecmp("Accept:", http_header_field) == 0) {
        //    strncpy(fil->peticio.accept, http_header_value, sizeof(fil->peticio.accept) - 1);

        //} else if (strcasecmp("Accept-Charset:", http_header_field) == 0) {
        //    strncpy(fil->peticio.accept_charset, http_header_value, sizeof(fil->peticio.accept_charset) - 1);

        //} else if (strcasecmp("Accept-Encoding:", http_header_field) == 0) {
        //    strncpy(fil->peticio.accept_encoding, http_header_value, sizeof(fil->peticio.accept_encoding) - 1);
            
        //} else if (strcasecmp("Accept-Language:", http_header_field) == 0) {
        //    strncpy(fil->peticio.accept_language, http_header_value, sizeof(fil->peticio.accept_language) - 1);

        //} else if (strcasecmp("Accept-Range:", http_header_field) == 0) {
        //    strncpy(fil->peticio.accept_range, http_header_value, sizeof(fil->peticio.accept_range) - 1);

        //} else if (strcasecmp("Authorization:", http_header_field) == 0) {
        //    strncpy(fil->peticio.authorization, http_header_value, sizeof(fil->peticio.authorization) - 1);

        //} else if (strcasecmp("Cache-Control:", http_header_field) == 0) {
            //strncpy(fil->peticio.cache_control, http_header_value, sizeof(fil->peticio.cache_control) - 1);

        } else if (strcasecmp("Connection:", http_header_field) == 0) {
            strncpy(fil->peticio.connection, http_header_value, sizeof(fil->peticio.connection) - 1);

        } else if (strcasecmp("Content-Length:", http_header_field) == 0) {
            fil->peticio.content_length = strtol(http_header_value, NULL, 10);

        } else if (strcasecmp("Content-Type:", http_header_field) == 0) {
            strncpy(fil->peticio.content_type, http_header_value, sizeof(fil->peticio.content_type) - 1);

        //} else if (strcasecmp("Cookie:", http_header_field) == 0) {
            //strncpy(fil->peticio.cookie, http_header_value, sizeof(fil->peticio.cookie) - 1);

        //} else if (strcasecmp("Date:", http_header_field) == 0) {
            //strncpy(fil->peticio.date, http_header_value, sizeof(fil->peticio.date) - 1);

        } else if (strcasecmp("Expect:", http_header_field) == 0) {
            strncpy(fil->peticio.expect, http_header_value, sizeof(fil->peticio.expect) - 1);

        //} else if (strcasecmp("From:", http_header_field) == 0) {
        //    strncpy(fil->peticio.from, http_header_value, sizeof(fil->peticio.from) - 1);

        //} else if (strcasecmp("Host:", http_header_field) == 0) {
            //strncpy(fil->peticio.host, http_header_value, sizeof(fil->peticio.host) - 1);

        //} else if (strcasecmp("If-Match:", http_header_field) == 0) {
            //strncpy(fil->peticio.if_match, http_header_value, sizeof(fil->peticio.if_match) - 1);

        } else if (strcasecmp("If-Modified-Since:", http_header_field) == 0) {
            strncpy(fil->peticio.if_modified_since, http_header_value, sizeof(fil->peticio.if_modified_since) - 1);

        //} else if (strcasecmp("If-None-Match:", http_header_field) == 0) {
            //strncpy(fil->peticio.if_none_match, http_header_value, sizeof(fil->peticio.if_none_match) - 1);

        //} else if (strcasecmp("If-Range:", http_header_field) == 0) {
            //strncpy(fil->peticio.if_range, http_header_value, sizeof(fil->peticio.if_range) - 1);

        //} else if (strcasecmp("If-Unmodified-Since:", http_header_field) == 0) {
            //strncpy(fil->peticio.if_unmodified_since, http_header_value, sizeof(fil->peticio.if_unmodified_since) - 1);

        //} else if (strcasecmp("Max-Forwards::", http_header_field) == 0) {
        //    fil->peticio.max_forwards = strtol(http_header_value, NULL, 10);

        //} else if (strcasecmp("Pragma:", http_header_field) == 0) {
            //strncpy(fil->peticio.pragma, http_header_value, sizeof(fil->peticio.pragma) - 1);

        //} else if (strcasecmp("Proxy-Authorization:", http_header_field) == 0) {
        //    strncpy(fil->peticio.proxy_authorization, http_header_value, sizeof(fil->peticio.proxy_authorization) - 1);

        //} else if (strcasecmp("Range:", http_header_field) == 0) {
            //strncpy(fil->peticio.range, http_header_value, sizeof(fil->peticio.range) - 1);

        //} else if (strcasecmp("Referer:", http_header_field) == 0) {
            //strncpy(fil->peticio.referer, http_header_value, sizeof(fil->peticio.referer) - 1);

        //} else if (strcasecmp("TE:", http_header_field) == 0) {
        //    strncpy(fil->peticio.te, http_header_value, sizeof(fil->peticio.te) - 1);

        //} else if (strcasecmp("Upgrade:", http_header_field) == 0) {
        //    strncpy(fil->peticio.upgrade, http_header_value, sizeof(fil->peticio.upgrade) - 1);

        } else if (strcasecmp("User-Agent:", http_header_field) == 0) {
            strncpy(fil->peticio.user_agent, http_header_value, sizeof(fil->peticio.user_agent) - 1);

        //} else if (strcasecmp("Via:", http_header_field) == 0) {
        //    strncpy(fil->peticio.via, http_header_value, sizeof(fil->peticio.via) - 1);

        //} else if (strcasecmp("Warn:", http_header_field) == 0) {
            //strncpy(fil->peticio.warn, http_header_value, sizeof(fil->peticio.warn) - 1);

        } else {
            //CAPÇALERA DESCONEGUDA
        }
    }

    return 0;
}

/*
 * esperar_petició()
 * Espera a rebre una petició HTTP sencera dintre del temps d'espera especificat
 * Params:
 *  fil: punter a la tupla del fil d'execució
 *  buffer_in[]: buffer on desar la petició
 *  bytes_sobrants: paràmetre de sortida, conté el nombre de bytes rebuts que no corresponen a la primera petició rebuda
 * retorn: veure header.h, positiu=bytes llegits
 */
int esperar_peticio(struct fil *fil, char buffer_in[], int *bytes_sobrants) {
    int resultat_recv = 0, resultat_select = 0; // desa el resultat de les crides
    int mida_buffer = *bytes_sobrants; // mida usada del buffer d'entrada
    time_t temps_inicial = time(NULL); // marca de temps (per a calcular temps d'espera)
    fd_set conjunt_lectura; // conjunt de descriptors per a la crida a select
    struct timeval temps_select = {0}; // temps d'espera per a la crida a select
    int mida_total_peticio = 0; // per a desar la mida total de la petició calculada
    int resultat_ap = 0; // per a desar el resultat de analitzar_peticio()
    int flag_continue = 0; // es posa a 1 si hem enviat el missatge 100 Continue
    char *posicio_crlfcrlf = NULL; // per a desar la posició de la seqüència CRLFCRLF en el buffer

    // Llegim dades
    while(1) {
        // Comprovem si el programa ha rebut l'ordre de finalitzar
        if(finalitza) return EP_FINALITZA;
        
        // Comprovem si hem rebut les capçaleres
        if((posicio_crlfcrlf = strstr(buffer_in, "\r\n\r\n")) != NULL) {

            // Analitzem les capçaleres
            resultat_ap = analitzar_peticio(fil, buffer_in);
            if(resultat_ap < 0) return resultat_ap; // Hi ha algun problema amb la petició, retornem l'error de la rutina analitzar_petició
            
            // Comprovem si l'entity cap al buffer
            if(fil->peticio.content_length >= sizeof(fil->peticio.http_entity)) return EP_MASSA_LLARG;

            // Calculem la mida total de la peticio
            char *inici_entity = &posicio_crlfcrlf[4];
            mida_total_peticio = (inici_entity - buffer_in) + fil->peticio.content_length;

            // Comprovem si hem rebut la petició sencera, incloent l'entity
            if(mida_total_peticio <= mida_buffer) {
                // Copiem l'entity a l'estructura de dades
                memcpy(fil->peticio.http_entity, inici_entity, fil->peticio.content_length); // Copiem l'entity
                *bytes_sobrants = mida_buffer - ((inici_entity - buffer_in) + fil->peticio.content_length);
                return mida_total_peticio; // Retornem nombre de bytes analitzats
            }

            // Responem amb el missatge 100 Continue si el client ho demana
            if(flag_continue == 0 && strcmp(fil->peticio.expect, "100-continue") == 0) {
                if(fil->peticio.content_length > MIDA_BUFFER_ENTRADA - 2) return EP_MASSA_LLARG;
                flag_continue = 1;
                send(fil->descriptor, "HTTP/1.1 100 Continue\r\n\r\n", 25, 0);
            }
            
            // Comprovem si la capçalera expect conté un valor no vàlid
            if(strlen(fil->peticio.expect) && strcmp(fil->peticio.expect, "100-continue") != 0) {
                return EP_EXPECT_NO_VALID;
            }
        }

        // Comprovem si s'ha excedit el temps d'espera (en teoria és innecessari)
        if(time(NULL) - temps_inicial >= temps_espera) {
            return EP_TEMPS_ESPERA_EXCEDIT;
        }

        // Preparem les dades per al select() (descriptors a llegir)
        FD_ZERO(&conjunt_lectura);
        FD_SET(fil->descriptor, &conjunt_lectura);
        // Preparem les dades per al select() (3 segons d'espera)
        temps_select.tv_sec = 3;
        temps_select.tv_usec = 0;

        // Esperem dades entrants al descriptor durant el temps d'espera restant
        resultat_select = select(fil->descriptor + 1, &conjunt_lectura, NULL, NULL, &temps_select);

        if(resultat_select == -1) { // Hi ha hagut un error
            fprintf(stderr, "Treballador - error al select: %s\n", strerror(errno));
            return EP_ERROR;
            break;
        }

        // Llegim les dades disponibles
        resultat_recv = recv(fil->descriptor,
                buffer_in + mida_buffer,
                MIDA_BUFFER_ENTRADA - 1 - mida_buffer,
                MSG_DONTWAIT);

        // Comprovem el resultat de la lectura
        if(resultat_recv == -1 && errno != EAGAIN) { // Hi ha hagut un error
            fprintf(stderr, "Treballador - error al rebre dades: %s\n", strerror(errno));
            return EP_ERROR;
        } else if(resultat_recv == 0) { // El client ha tancat la connexió
            return EP_CLIENT_HA_TANCAT;
        } else if(resultat_recv != -1) { // Hem rebut dades
            mida_buffer += resultat_recv;
        }

        // Comprovem si hem omplert el buffer (si l'execució arriba aquí, es que no hem rebut una petició sencera)
        if(mida_buffer >= MIDA_BUFFER_ENTRADA - 2) {
            return EP_MASSA_LLARG;
        }
    }
}

/*
 * enviar_error()
 * Envia un codi d'error a un socket (i possiblement també una pàgina d'error)
 * Params:
 *  fil: punter a la tupla del fil d'execució
 *  codi_error: codi d'errir HTTP
 * retorn: 0=OK
 */
int enviar_error(struct fil *fil, int codi_error) {
    char buffer_out[MIDA_BUFFER_SORTIDA] = {0}; // buffer per a les dades sortints
    int offset_out = 0; // offset en el buffer

    struct stat stat_recurs; // resultat de stat()
    char ruta_recurs[MIDA_HTTP_PATH] = {0}; // buffer per a desar la ruta del recurs demanat
    long mida_arxiu = 0; // mida de l'arxiu a enviar
    int resultat_stat; // resultat de la crida a stat()
    int enviar_document = 0; // indica si hem d'enviar el document d'error o no
    time_t ara = time( (time_t*) 0 ); // hora i data actuals

    // comprovem si hem (i podem) enviar el document d'error
    if(strcmp(fil->peticio.http_method, "HEAD") != 0) {
        sprintf(ruta_recurs, "%s/%i.html", directori_documents_error, codi_error);
        resultat_stat = stat(ruta_recurs, &stat_recurs);
        if(resultat_stat >= 0 && access(ruta_recurs, R_OK) != -1) enviar_document = 1;
    }

    if(enviar_document) {
        fil->resposta.content_lenght = stat_recurs.st_size;  // Preparem Content-Lenght
        sprintf(fil->resposta.content_type, "text/html; charset=%s", charset); // Preparem Content-Type
    } else {
        fil->resposta.content_lenght = 0;  // Preparem Content-Lenght
    }

    strftime(fil->resposta.date, sizeof(fil->resposta.date), RFC1123_PRINTF, gmtime(&ara)); // Preparem data actual
    fil->resposta.status_code = codi_error;
    
    switch(codi_error) {
        default:
            sprintf(buffer_out, "%s %i Error\r\nContent-length: %li\r\nServer: %s\r\nDate: %s\r\nContent-type: %s\r\n\r\n",
                PROTOCOL, fil->resposta.status_code, fil->resposta.content_lenght, NOM_SERVIDOR, fil->resposta.date, fil->resposta.content_type);
            offset_out = strlen(buffer_out);
            send(fil->descriptor, buffer_out, offset_out, 0);
            break;
    }

    if(enviar_document) enviar_arxiu(ruta_recurs, fil, NULL, (long) stat_recurs.st_size);

    return 0;
}

/*
 * processar_peticio()
 * Executa una petició entrant analitzada
 * Params:
 *  fil: punter a la tupla del fil d'execució
 * retorn: -1=error (l'envia resposta HTTP directament al client), 0=ok
 */
int processar_peticio(struct fil *fil) {
    char buffer_out[MIDA_BUFFER_SORTIDA] = {0}; // buffer de sortida
    int offset_out = 0; // offset en el buffer de sortida
    time_t ara = time( (time_t*) 0 ); // hora i data actuals
    char ruta_recurs[MIDA_HTTP_PATH] = {0}; // buffer per a desar la ruta del recurs demanat
    int resultat_stat; // resultat de la crida a stat()

    // Construim la ruta del recurs
    strcpy(ruta_recurs, directori_documents);
    strncat(ruta_recurs, fil->peticio.http_path, MIDA_MAX_HTTP_HEADER_VALUE - strlen(ruta_recurs) - 1);

    // Comprovem que no es tracti de sortir del directori de documents
    if(strncmp(ruta_recurs, ".." , 2) == 0
            || strstr(ruta_recurs, "/../" ) != (char*) 0
            || strcmp(&(ruta_recurs[strlen(ruta_recurs)-3]), "/.." ) == 0) {
        enviar_error(fil, HTTP_BAD_REQUEST);
        return -1;
    }

    // Comprovem mètode
    if(strcmp(fil->peticio.http_method, "GET") == 0
        || strcmp(fil->peticio.http_method, "POST") == 0
        || strcmp(fil->peticio.http_method, "HEAD") == 0) {

        struct stat stat_recurs; // per a desar el resultat de stat()

        // Obtenim informació de l'arxiu i comprovem si tenim els permisos per a accedir-hi
        resultat_stat = stat(ruta_recurs, &stat_recurs);
        if(resultat_stat < 0) {
            if(errno == EACCES) enviar_error(fil, HTTP_FORBIDDEN);
            else if(errno == ENOENT) enviar_error(fil, HTTP_NOT_FOUND);
            else enviar_error(fil, HTTP_INTERNAL_SERVER_ERROR);
            return -1;
        }

        // Si el recurs és un directori, retorna l'arxiu d'índex
        if(S_ISDIR(stat_recurs.st_mode)) {
            strncat(ruta_recurs, arxiu_portada, MIDA_MAX_HTTP_HEADER_VALUE - strlen(ruta_recurs) - 1);
            resultat_stat = stat(ruta_recurs, &stat_recurs);
            if(resultat_stat < 0) {
                if(errno == EACCES) enviar_error(fil, HTTP_FORBIDDEN);
                else if(errno == ENOENT) enviar_error(fil, HTTP_NOT_FOUND);
                else  enviar_error(fil, HTTP_INTERNAL_SERVER_ERROR);
                return -1;
            }
        }

        // Comprovem si tenim drets de lectura
        if(access(ruta_recurs, R_OK) == -1) {
            if(errno == EACCES) enviar_error(fil, HTTP_FORBIDDEN);
            else enviar_error(fil, HTTP_INTERNAL_SERVER_ERROR);
            return -1;
        }

        // Comprovem si hem d'executar un script CGI (condicions: ser a ruta_cgi i tenir permisos d'execució
        if(strncmp(ruta_cgi, fil->peticio.http_path, strlen(ruta_cgi)) == 0 && access(ruta_recurs, X_OK) == 0) {
            executar_cgi(fil,ruta_recurs);

            // les peticions a programes CGI sempre tanquen la connexió, ja que poden presentar problemes (no envien la capçalera Content-Length)
            uncork(fil);
            acabar_fil(fil);
        }

        // Construïm capçaleres
        fil->resposta.content_lenght = (long) stat_recurs.st_size; // Preparem content-lenght
        strftime(fil->resposta.date, sizeof(fil->resposta.date), RFC1123_PRINTF, gmtime(&ara)); // Preparem data actual
        strftime(fil->resposta.last_modified, sizeof(fil->resposta.last_modified), RFC1123_PRINTF, (void *) gmtime(&stat_recurs.st_mtim.tv_sec)); // Preparem capçalera Last-Modified:
        sprintf(fil->resposta.content_type, deduir_mime(ruta_recurs), charset); // Establim el tipus mime de l'arxiu

        // Comprovem petició condicional (només If-Modified-Since)
        if(strlen(fil->peticio.if_modified_since)) {
            struct tm data_if_mod_since;
            struct tm data_modificacio_recurs;
            // Parsejem capçalera
            if(strptime(fil->peticio.if_modified_since, RFC1123_SCANF, &data_if_mod_since) != NULL) {
                gmtime_r(&stat_recurs.st_mtim.tv_sec, &data_modificacio_recurs);

                time_t time_t_if_mod_since = mktime(&data_if_mod_since);
                time_t time_t_modificacio_recurs = mktime(&data_modificacio_recurs);

                if(time_t_modificacio_recurs <= time_t_if_mod_since) {
                    fil->resposta.status_code = HTTP_NOT_MODIFIED;
                    sprintf(buffer_out, "%s %i Not modified\r\nContent-length: %li\r\nServer: %s\r\nDate: %s\r\nLast-Modified: %s\r\nContent-type: %s\r\n\r\n",
                        PROTOCOL, fil->resposta.status_code, fil->resposta.content_lenght, NOM_SERVIDOR, fil->resposta.date, fil->resposta.last_modified, fil->resposta.content_type);
                    offset_out = strlen(buffer_out);
                    send(fil->descriptor, buffer_out, offset_out, 0);
                    return 0;
                }
            }
        }

        // Construïm i enviem la resposta: status-line i capçaleres
        fil->resposta.status_code = HTTP_OK;
        sprintf(buffer_out, "%s %i OK\r\nContent-length: %li\r\nServer: %s\r\nDate: %s\r\nLast-Modified: %s\r\nContent-type: %s\r\n\r\n",
                            PROTOCOL, fil->resposta.status_code, fil->resposta.content_lenght, NOM_SERVIDOR, fil->resposta.date, fil->resposta.last_modified, fil->resposta.content_type);
        offset_out = strlen(buffer_out);
        send(fil->descriptor, buffer_out, offset_out, 0);

        // Enviem l'arxiu si procedeix (mètodes GET i POST)
        if(strcmp(fil->peticio.http_method, "HEAD") != 0) {
            if(enviar_arxiu(ruta_recurs, fil, NULL, fil->resposta.content_lenght)) return -1;
        }
        return 0;

    } else if(strcmp(fil->peticio.http_method, "PUT") == 0
            || strcmp(fil->peticio.http_method, "DELETE") == 0
            || strcmp(fil->peticio.http_method, "DELETE") == 0
            || strcmp(fil->peticio.http_method, "TRACE") == 0
            || strcmp(fil->peticio.http_method, "OPTIONS") == 0) {
        enviar_error(fil, HTTP_NOT_IMPLEMENTED);
        return -1;
    } else {
        enviar_error(fil, HTTP_BAD_REQUEST);
        return -1;
    }
}

/*
 * atendre_connexio()
 * Rutina principal per a l'atenció d'una petició. Els fils comencen en aquesta rutina
 * f_id = id del fil d'execució
 */
void *atendre_connexio(struct fil *fil) {
    char adreca_str[INET_ADDRSTRLEN] = {0}; // adreça del client
    char buffer_in[MIDA_BUFFER_ENTRADA] = {0}; // buffer d'entrada
    int resultat_ep = 0; // desem el resultat de esperar_peticio()
    int bytes_sobrants = 0;

    // Obtenim adreça IP del client
    inet_ntop(fil->adreca_remota.sin_family, &(fil->adreca_remota.sin_addr), adreca_str, sizeof adreca_str);
    fprintf(stdout, "Treballador - connexió entrant de %s\n", adreca_str);

    // Bucle principal (per a connexions persistents)
    while(1) {
        // Esperem una petició entrant (la desem a buffer_in)
        resultat_ep = esperar_peticio(fil, buffer_in, &bytes_sobrants);

        // Comprovem el resultat de la recepció i actuem en conceqüència
        switch(resultat_ep) {
            case EP_CLIENT_HA_TANCAT:
            case EP_FINALITZA:
                acabar_fil(fil);

            case EP_TEMPS_ESPERA_EXCEDIT:
                enviar_error(fil, HTTP_REQUEST_TIMEOUT);
                acabar_fil(fil);

            case EP_ERROR:
                enviar_error(fil, HTTP_INTERNAL_SERVER_ERROR);
                acabar_fil(fil);

            case EP_MASSA_LLARG:
                enviar_error(fil, HTTP_REQUEST_ENTITY_TOO_LARGE);
                acabar_fil(fil);

            case EP_PETICIO_NO_VALIDA:
            case AP_INVALIDA:
                enviar_error(fil, HTTP_BAD_REQUEST);
                acabar_fil(fil);

            case AP_VERSIO_NO_SUPORTADA:
                enviar_error(fil, HTTP_VERSION_NOT_SUPPORTED);
                acabar_fil(fil);

            case EP_EXPECT_NO_VALID:
                enviar_error(fil, HTTP_EXPECTATION_FAILED);
                acabar_fil(fil);

            default:
                if(resultat_ep < 0) {
                    fprintf(stderr, "Treballador - ****!**** Retorn EP desconegut\n");
                    acabar_fil(fil);
                }
                else {
                    processar_peticio(fil);
                    fprintf(stdout, "Treballador - petició rebuda de %s: %s a %s - resposta HTTP %i (content-lenght: %li).\n",
                            adreca_str, fil->peticio.http_method, fil->peticio.http_path, fil->resposta.status_code, fil->resposta.content_lenght);
                    // si no hi ha dades pendents de processar (presumiblement d'una següent petició) enviem les dades immediatament
                    if(bytes_sobrants == 0) uncork(fil);
                }
                break;
        }

        if(strcmp(fil->peticio.connection, "close") == 0 || finalitza) acabar_fil(fil);

        if(bytes_sobrants != 0) {
            // movem el buffer per a rebre més peticions
            memmove(buffer_in, buffer_in + resultat_ep, bytes_sobrants);
            memset(buffer_in + bytes_sobrants, 0, MIDA_BUFFER_ENTRADA - bytes_sobrants);
        } else memset(buffer_in, 0, MIDA_BUFFER_ENTRADA);

        // netejem les estructures de dades de la petició i la resposta
        memset(&fil->peticio, 0, sizeof(fil->peticio));
        memset(&fil->resposta, 0, sizeof(fil->resposta));
    }
}

/*
 * atendre_sobrecarrega()
 * Rutina principal per a informar de sobrecàrrega del servidor al rebre una petició
 * f_id = id del fil d'execució
 */
void *atendre_sobrecarrega(struct fil *fil) {
    char adreca_str[INET_ADDRSTRLEN] = {0}; // adreça del client

    // Obtenim adreça IP del client
    inet_ntop(fil->adreca_remota.sin_family, &(fil->adreca_remota.sin_addr), adreca_str, sizeof adreca_str);
    fprintf(stdout, "Treballador - connexió connexió de %s (no s'atén per sobrecàrrega)\n", adreca_str);

    // Enviem la resposta
    enviar_error(fil, HTTP_SERVICE_UNAVAILABLE);

    // Acabem
    acabar_fil(fil);
}