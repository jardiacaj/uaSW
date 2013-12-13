/* 
 * File:   header.h
 * Author: joan
 *
 * Created on 19 / abril / 2010, 17:14
 */

#include <netdb.h>

#ifndef _HEADER_H
#define	_HEADER_H

#ifdef	__cplusplus
extern "C" {
#endif
    #define _GNU_SOURCE

    // GENERAL
    #define NOM_SERVIDOR "uaSW/0.3" // nom del programa
    #define PROTOCOL "HTTP/1.1" // protocol per defecte
    #define RFC1123_PRINTF "%a, %d %b %Y %H:%M:%S GMT" // format de data RFC1123 per a print
    #define RFC1123_SCANF "%a, %d %b %Y %H:%M:%S %Z" // format de data RFC1123 per a scan
    #define FORMAT_DATA_LOGS "%a %b %d %T %y" // format de les dates als arxius de registre
    #define MAX_ABSOLUT_FILS 500 // màxim absolut de fils concurrents
    #define MAX_MIME_TYPES 500 // màxim de mime-types que es coneixeran
    #define MIDA_BUFFER_ENTRADA 20000 // mida del búfer de les dades rebudes
    #define MIDA_BUFFER_SORTIDA 2000 // mida del búfer per a enviar les dades
    #define MIDA_STACK_FIL 50000 // mida dels stacks dels fils

    // Configuracions per defecte (veure configuracio.c)
    #define DEF_ARXIU_CONFIGURACIO "uasw.conf"
    #define DEF_MAX_FILS 250
    #define DEF_MAX_FILS_SOBRECARREGA 40
    #define DEF_MIDA_CUA_ENTRANTS 50
    #define DEF_PORT_ESCOLTA "80"
    #define DEF_TEMPS_ESPERA 60
    #define DEF_ARXIU_PORTADA "index.html"
    #define DEF_DIRECTORI_DOCUMENTS "/var/www"
    #define DEF_DIRECTORI_DOCUMENTS_ERRORS "/var/www/errors"
    #define DEF_CHARSET "utf-8"
    #define DEF_HOST "localhost"
    #define DEF_RUTA_CGI "/cgi-bin/"
    #define DEF_LOG_PRINCIPAL "uasw.log"
    #define DEF_LOG_ERRORS "errors.log"
    #define DEF_RUTA_MIME_TYPES "mime_types.conf"

    // mida cadenes i buffers
    #define MIDA_EXTENSIO 10 // extensions d'arxius
    #define MIDA_MIME 40 // tipus mime
    #define MIDA_HTTP_METHOD 10 // mètode HTTP
    #define MIDA_HTTP_PATH 1000 // ruta del recurs HTTP
    #define MIDA_HTTP_VERSION 10 // versió del protocol
    #define MIDA_HTTP_HEADER_FIELD 40 // tipus de capçalera HTTP
    #define MIDA_DEF_HTTP_HEADER_VALUE 200 // valor de capçalera HTTP per defecte
    #define MIDA_MAX_HTTP_HEADER_VALUE 20000  // valor de capçalera HTTP més llarga
    #define MIDA_HTTP_ENTITY 15000 // entity de les peticions HTTP
    #define MIDA_BUFFER_CONFIG 200 // mida del buffer per a la lectura de les configuracions
    #define MIDA_PORT_ESCOLTA 8 // port escolta
    #define MIDA_CHARSET 10 // charset
    #define MIDA_DATA 100 // dates
    #define MIDA_COOKIES 4000 // cookies

    // Retorns de la rutina esperar_peticio() (fil.c)
    #define EP_CLIENT_HA_TANCAT -1 // el client ha tancat la connexió
    #define EP_TEMPS_ESPERA_EXCEDIT -2 // s'ha excedit el temps d'espera
    #define EP_ERROR -3 // ha ocorregut un error
    #define EP_MASSA_LLARG -4 // petició HTTP massa llarga
    #define EP_PETICIO_NO_VALIDA -5 // petició HTTP no vàlida
    #define EP_FINALITZA -6 // el programa està finalitzant
    #define EP_EXPECT_NO_VALID -7 // no s'ha entès el contingut de la capçalera expect

    // Retorns de la rutina analitzar_petició
    #define AP_INVALIDA -102 // petició HTTP no vàlida
    #define AP_VERSIO_NO_SUPORTADA -103 // protocol no vàlid o incompatible


    // Codis resposta HTTP implementats
    #define HTTP_CONTINUE 100
    #define HTTP_OK 200
    #define HTTP_NOT_MODIFIED 304

    #define HTTP_BAD_REQUEST 400
    #define HTTP_FORBIDDEN 403
    #define HTTP_NOT_FOUND 404
    #define HTTP_NOT_ACCEPTABLE 406
    #define HTTP_REQUEST_TIMEOUT 408
    #define HTTP_PRECONDITION_FAILED 412
    #define HTTP_REQUEST_ENTITY_TOO_LARGE 413
    #define HTTP_REQUEST_URI_TOO_LONG 414
    #define HTTP_REQUEST_UNSUPPORTED_MEDIA_TYPE 415
    #define HTTP_REQUESTED_RANGE_NOT_SATISFIABLE 416
    #define HTTP_EXPECTATION_FAILED 417

    #define HTTP_INTERNAL_SERVER_ERROR 500
    #define HTTP_NOT_IMPLEMENTED 501
    #define HTTP_SERVICE_UNAVAILABLE 503
    #define HTTP_VERSION_NOT_SUPPORTED 505

    // Tupla per a peticions HTTP
    struct peticio_http {
        char http_method[MIDA_HTTP_METHOD];
        char http_protocol[MIDA_HTTP_VERSION];
        char http_path[MIDA_HTTP_PATH];
        char http_entity[MIDA_HTTP_ENTITY];
        char http_query_string[MIDA_HTTP_PATH];
        char connection[20];
        long content_length;
        char content_type[MIDA_DEF_HTTP_HEADER_VALUE];
        char expect[20];
        char if_modified_since[MIDA_DATA];
        char user_agent[MIDA_DEF_HTTP_HEADER_VALUE];

        // capçaleres no implementades

        //char accept[MIDA_DEF_HTTP_HEADER_VALUE];
        //char accept_charset[MIDA_DEF_HTTP_HEADER_VALUE];
        //char accept_encoding[MIDA_DEF_HTTP_HEADER_VALUE];
        //char accept_language[20];
        //char authorization[MIDA_DEF_HTTP_HEADER_VALUE];
        //char cache_control[20];
        //char cookie[MIDA_COOKIES];
        //char date[MIDA_DATA];
        //char from[50];
        //char host[MIDA_DEF_HTTP_HEADER_VALUE];
        //char if_match[200];
        //char if_none_match[MIDA_DEF_HTTP_HEADER_VALUE];
        //char if_range[MIDA_DEF_HTTP_HEADER_VALUE];
        //char if_unmodified_since[MIDA_DATA];
        // long max_forwards;
        //char pragma[20];
        //char proxy_autorization[MIDA_DEF_HTTP_HEADER_VALUE];
        //char range[200];
        //char referer[MIDA_HTTP_PATH];
        //char te[MIDA_DEF_HTTP_HEADER_VALUE];
        //char upgrade[MIDA_DEF_HTTP_HEADER_VALUE];
        //char via[MIDA_HTTP_PATH];
        //char warn[200];
    };

    // Tupla per a respostes HTTP
    struct resposta_http {
        long content_lenght;
        char content_type[MIDA_MIME];
        char date[MIDA_DATA];
        char last_modified[MIDA_DATA];
        int status_code;
        
        // capçaleres no implementades
        
        //char accept_ranges[MIDA_DEF_HTTP_HEADER_VALUE];
        //long age; // l'ignorem
        //char allow[MIDA_DEF_HTTP_HEADER_VALUE];
        //char cache_control[MIDA_DEF_HTTP_HEADER_VALUE];
        //char content_encoding[20];
        //char content_language[20];
        //char content_location[MIDA_HTTP_PATH];
        //char content_disposition[MIDA_DEF_HTTP_HEADER_VALUE];
        //char content_md5[30];
        //char content_range[MIDA_DEF_HTTP_HEADER_VALUE];
        //char etag[50];
        //char expires[MIDA_DATA];
        //char location[MIDA_HTTP_PATH];
        //char pragma[MIDA_DEF_HTTP_HEADER_VALUE];
        //char proxy_authenticate[MIDA_DEF_HTTP_HEADER_VALUE];
        //char refresh[MIDA_HTTP_PATH];
        //long retry_after;
        //char server[MIDA_DEF_HTTP_HEADER_VALUE];
        //char set_cookie[MIDA_COOKIES];
        //char trailer[MIDA_DEF_HTTP_HEADER_VALUE];
        //char transfer_encoding[20];
        //char vary[MIDA_DEF_HTTP_HEADER_VALUE];
        //char via[MIDA_DEF_HTTP_HEADER_VALUE];
        //char warning[MIDA_DEF_HTTP_HEADER_VALUE];
        //char www_authenticate[MIDA_DEF_HTTP_HEADER_VALUE];
    };

    // Tupla amb les dades d'un fil d'execució
    struct fil {
        pthread_t pthread_obj; // objecte pthread_t del fil
        int descriptor; // descriptor del socket connectat amb el client
        struct sockaddr_in adreca_remota; // adreça de xarxa del client
        struct resposta_http resposta; // dades de la resposta HTTP
        struct peticio_http peticio; // dades de la petició HTTP
    };

    // Tupla per a desar tipus mime relacionant-los amb l'extensió de fitxer corresponent
    struct tipus_mime {
        char extensio[MIDA_EXTENSIO];
        char mime[MIDA_MIME];
    };
    
#ifdef	__cplusplus
}
#endif

#endif	/* _HEADER_H */

