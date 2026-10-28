#include "../../header/prod/meme_types_handler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

size_t *IXXC_Archieved_File_Sizes = NULL;

#define IXXC_SERVER_HEADER \
"HTTP/1.1 200 OK\r\n" \
"Content-Type: text/html; charset=utf-8\r\n" \
"Content-Length: %zu\r\n" \
"Connection: close\r\n" \
"X-Content-Type-Options: nosniff\r\n" \
"X-Frame-Options: DENY\r\n" \
"Referrer-Policy: no-referrer\r\n" \
"Server: ixxc/1.2\r\n" \
"\r\n"

#define IXXC_SERVER_HEADER_DOWNLOAD \
"HTTP/1.1 200 OK\r\n" \
"Content-Type: application/octet-stream\r\n" \
"Content-Disposition: attachment\r\n" \
"Content-Length: %zu\r\n" \
"Connection: close\r\n" \
"X-Content-Type-Options: nosniff\r\n" \
"Server: ixxc/1.2\r\n" \
"\r\n"

#define IXXC_SERVER_HEADER_WEBP \
"HTTP/1.1 200 OK\r\n" \
"Content-Type: image/webp\r\n" \
"Content-Length: %zu\r\n" \
"Connection: close\r\n" \
"X-Content-Type-Options: nosniff\r\n" \
"X-Frame-Options: DENY\r\n" \
"Referrer-Policy: no-referrer\r\n" \
"Server: ixxc/1.2\r\n" \
"\r\n"

#define IXXC_FORBIDDEN \
"HTTP/1.1 403 Forbidden\r\n" \
"Content-Type: text/html; charset=utf-8\r\n" \
"Content-Length: 68\r\n" \
"Connection: close\r\n" \
"Server: ixxc/1.2\r\n" \
"\r\n" \
"<h1>ERROR: 403</h1>" \
"<h1>Forbidden</h1>"

/*
 * Die Header-Makros enthalten "%zu" als Platzhalter für Content-Length.
 * sizeof( . . . ) zählt die 3 Zeichen von "%zu" mit.
 * Diese werden zur Laufzeit durch die tatsächliche Zahl ersetzt und
 * erscheinen nicht in der fertigen Ausgabe.
 * Deshalb: sizeof(Formatstring) - 3 + maximale Ziffernlänge einer uint64_t.
 */
#define MAX_UINT64_STR                    "18446744073709551615"
#define MAX_UINT64_STR_LEN                (sizeof(MAX_UINT64_STR))
#define IXXC_SERVER_HEADER_LEN            (sizeof(IXXC_SERVER_HEADER)           + MAX_UINT64_STR_LEN)
#define IXXC_SERVER_HEADER_DOWNLOAD_LEN   (sizeof(IXXC_SERVER_HEADER_DOWNLOAD)  + MAX_UINT64_STR_LEN)
#define IXXC_SERVER_HEADER_WEBP_LEN       (sizeof(IXXC_SERVER_HEADER_WEBP)      + MAX_UINT64_STR_LEN)
#define IXXC_FORBIDDEN_LEN                (sizeof(IXXC_FORBIDDEN))

/**
 * @brief MIME-/Antwort-Kategorie eines Endpoints.
 */
typedef enum {
    MEME_TYPE_DOWNLOAD,   /**< Binärdownload (octet-stream) */
    MEME_TYPE_TEXT,       /**< HTML/Text-Antwort */
    MEME_TYPE_FORBIDDEN,  /**< 403 Forbidden */
    MEME_TYPE_WEBP,       /**< image/webp */
} Meme_type_enum_e;

/**
 * @brief Prüft, ob eine Dateiendung als Download ausgeliefert werden soll.
 *
 * Erkannte Endungen: .docx, .png, .pdf, .odt, .odg.
 *
 * @param dot  Zeiger auf die Endung inklusive führendem Punkt
 *             (z. B. ".pdf"); darf nicht NULL sein.
 * @return     1 wenn Download-Typ, sonst 0.
 */
static int is_download_meme_type(const char *dot)
{
    static const char *const file_extensions[] = {
        ".png", ".pdf", ".odt", ".odg", ".docx"
    };
    const size_t n = sizeof(file_extensions) / sizeof(file_extensions[0]);
    size_t i;
    if (!dot) {
        return 0;
    }
    for (i = 0; i < n; ++i) {
        if (strcmp(dot, file_extensions[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

/**
 * @brief Bestimmt den Content-/Antwort-Typ eines Endpoints anhand der Dateiendung.
 *
 * @param index_endpoint  Index in IXXC_ENDPOINTS; IXXC_ENDPOINTS_COUNT
 *                        bedeutet „unbekannt"  ->  FORBIDDEN.
 * @return                MEME_TYPE_DOWNLOAD, MEME_TYPE_WEBP,
 *                        MEME_TYPE_TEXT oder MEME_TYPE_FORBIDDEN.
 */
static Meme_type_enum_e detect_meme_type(size_t index_endpoint)
{
    const char *filename;
    const char *dot;
    if (index_endpoint == IXXC_ENDPOINTS_COUNT) {
        return MEME_TYPE_FORBIDDEN;
    }
    filename = IXXC_ENDPOINTS[index_endpoint].source_file_path;
    if (!filename) {
        return MEME_TYPE_FORBIDDEN;
    }
    dot = strrchr(filename, '.');
    if (dot) {
        if (strcmp(dot, ".webp") == 0) {
            return MEME_TYPE_WEBP;
        }
        if (is_download_meme_type(dot)) {
            return MEME_TYPE_DOWNLOAD;
        }
    }
    return MEME_TYPE_TEXT;
}

/**
 * @brief Erzeugt den passenden HTTP-Header (und ggf. den 403-Body)
 *        für den angegebenen Endpoint.
 *
 * Allokiert *header auf dem Heap. Der Caller muss *header mit free() freigeben.
 *
 * @param[out] header  Zielzeiger für den allokierten Header-String
 * @param[out] phlen   Länge des geschriebenen Header-Strings (ohne '\0')
 * @param[in]  idx     Endpoint-Index (oder IXXC_ENDPOINTS_COUNT für 403)
 * @param[in]  len     Content-Length für echte Endpoints
 * @return             0 bei Erfolg, -1 bei Fehler (u. a. OOM, snprintf)
 */
static int get_header_and_meme(char **header, size_t *phlen, size_t idx, size_t len)
{
    size_t alloc = 0;
    int written = -1;
    if (!header || !phlen) {
        return -1;
    }
    *header = NULL;
    *phlen = 0;
    switch (detect_meme_type(idx)) {
        case MEME_TYPE_DOWNLOAD:
            alloc = IXXC_SERVER_HEADER_DOWNLOAD_LEN + 1;
            *header = malloc(alloc);
            if (!*header) {
                goto error;
            }
            written = snprintf(*header, alloc, IXXC_SERVER_HEADER_DOWNLOAD, len);
            break;
        case MEME_TYPE_TEXT:
            alloc = IXXC_SERVER_HEADER_LEN + 1;
            *header = malloc(alloc);
            if (!*header) {
                goto error;
            }
            written = snprintf(*header, alloc, IXXC_SERVER_HEADER, len);
            break;
        case MEME_TYPE_FORBIDDEN:
            alloc = IXXC_FORBIDDEN_LEN + 1;
            *header = malloc(alloc);
            if (!*header) {
                goto error;
            }
            written = snprintf(*header, alloc, "%s", IXXC_FORBIDDEN);
            break;
        case MEME_TYPE_WEBP:
            alloc = IXXC_SERVER_HEADER_WEBP_LEN + 1;
            *header = malloc(alloc);
            if (!*header) {
                goto error;
            }
            written = snprintf(*header, alloc, IXXC_SERVER_HEADER_WEBP, len);
            break;
        default:
            goto error;
    }
    if (written < 0 || (size_t)written >= alloc) {
        free(*header);
        *header = NULL;
        goto error;
    }
    *phlen = (size_t)written;
    return 0;
    error:
    return -1;
}

/**
 * @brief Baut für jeden Endpoint den HTTP-Header und speichert Länge.
 *
 * Bei Fehler an einem Index bleiben header[i] und Längen unvollständig;
 * bereits allokierte Einträge werden nicht freigegeben (Caller/Shutdown).
 *
 * @param[out] http_header          Array[IXXC_ENDPOINTS_COUNT] von char*
 * @param[out] http_header_lengths  Array[IXXC_ENDPOINTS_COUNT] von size_t
 */
void pub_detect_meme_type(char **http_header,
                          size_t *http_header_lengths,
                          const size_t *file_sizes)
{
    size_t index_endpoint;
    if (!http_header || !http_header_lengths || !file_sizes) {
        return;
    }
    for (index_endpoint = 0; index_endpoint < (size_t)IXXC_ENDPOINTS_COUNT; ++index_endpoint) {
        if (get_header_and_meme(&http_header[index_endpoint], &http_header_lengths[index_endpoint], index_endpoint, file_sizes[index_endpoint]) != 0) {
            fprintf(stderr, "Header-Generierung fehlgeschlagen für %zu\n", index_endpoint);
        }
    }
}
