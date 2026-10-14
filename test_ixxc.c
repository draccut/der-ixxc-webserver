#define _POSIX_C_SOURCE 200809L

#include "ixxc_endpoints.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>
#include <errno.h>
#include <curl/curl.h>
#include <unistd.h>

#define COLOR_RED    "\x1b[31m"
#define COLOR_GREEN  "\x1b[32m"
#define COLOR_RESET  "\x1b[0m"

#define DEFAULT_BUFFER_SIZE 0x8000 /* Fallback-Puffergröße, falls die lokale Dateigröße nicht ermittelt werden kann */

#define MAX_FILE_SIZE 0x6400000 /* 100 MiB Harte Obergrenze pro Datei */

/**
 * Ziel-URL des zu testenden Servers.
 * Mit -DTEST_LOCAL_SERVER wird gegen den lokalen Entwicklungs-Server getestet
 * SSL-Prüfung dann deaktiviert
 */
#ifdef TEST_LOCAL_SERVER
#define TARGET_URL "https://127.0.0.1:8080"
#else
#define TARGET_URL "https://info.ixxc.de"
#endif

struct file_buf {
    uint8_t *data;
    size_t   len;
};

struct mem_writer {     /* Ermöglicht es, die empfangenen Daten direkt in einen vorallokierten Puffer zu schreiben */
    uint8_t *buf;       /* Zielpuffer */
    size_t   capacity;  /* maximale Kapazität */
    size_t   len;       /* bereits geschriebene Bytes */
    int      overflow;  /* 1 = Puffer war zu klein */
};

/**
 * Extrahiert den Dateinamen Basename aus einem Pfad.
 * Beispiel: /foo/bar/index.html wird zu index.html
 * Bei leerem oder ungültigem Pfad wird index zurückgegeben.
 *
 * @param path    Eingabe-Pfad
 * @param out     Ausgabe-Puffer für den Dateinamen
 * @param outlen  Größe des Ausgabe-Puffers
 */
static void basename_from_path(const char *path, char *out, size_t outlen)
{
    if (!path || !out || outlen == 0) {
        return;
    }
    size_t len = strlen(path);
    if (len == 0) {
        strncpy(out, "index", outlen - 1);
        out[outlen - 1] = '\0';
        return;
    }
    while (len > 0 && path[len - 1] == '/') {
        len--;
    }
    if (len == 0) {
        strncpy(out, "index", outlen - 1);
        out[outlen - 1] = '\0';
        return;
    }
    const char *p = path + len - 1;
    while (p > path && *p != '/') {
        p--;
    }
    if (*p == '/') {
        p++;
    }
    size_t copylen = strlen(p);
    if (copylen == 0) {
        strncpy(out, "index", outlen - 1);
    } else {
        if (copylen >= outlen) {
            copylen = outlen - 1;
        }
        memcpy(out, p, copylen);
        out[copylen] = '\0';
    }
}

/**
 * Extrahiert den Pfad-Anteil aus einem HTTP-Endpoint-String.
 * Beispiel: "GET /asm" wird zu "/asm"
 * Falls kein Leerzeichen gefunden wird, wird der gesamte String kopiert.
 *
 * @param endpoint  Endpoint-String (z. B. "GET /path")
 * @param out       Ausgabe-Puffer für den Pfad
 * @param outlen    Größe des Ausgabe-Puffers
 */
static void extract_path_from_endpoint(const char *endpoint, char *out, size_t outlen)
{
    if (!endpoint || !out || outlen == 0) {
        return;
    }
    const char *space = strchr(endpoint, ' ');
    if (!space) {
        strncpy(out, endpoint, outlen - 1);
        out[outlen - 1] = '\0';
        return;
    }
    const char *path = space + 1;
    while (*path == ' ') {
        path++;
    }
    strncpy(out, path, outlen - 1);
    out[outlen - 1] = '\0';
}

/**
 * Lädt eine komplette Datei in den Heap.
 * Die Datei darf maximal MAX_FILE_SIZE Bytes groß sein.
 *
 * @param path  Pfad zur Datei
 * @param out   Wird mit Zeiger und Länge befüllt (bei Fehler data == NULL)
 * @return      1 bei Erfolg, 0 bei Fehler
 */
static int load_file_to_heap(const char *path, struct file_buf *out)
{
    out->data = NULL;
    out->len  = 0;
    FILE *f = fopen(path, "rb");
    if (!f) {
        perror(path);
        return 0;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return 0;
    }
    long sz = ftell(f);
    if (sz < 0 || (size_t)sz > MAX_FILE_SIZE) {
        fclose(f);
        return 0;
    }
    rewind(f);
    uint8_t *buf = malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f);
        return 0;
    }
    size_t n = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (n != (size_t)sz) {
        free(buf);
        return 0;
    }
    buf[n] = '\0';
    out->data = buf;
    out->len  = n;
    return 1;
}

/**
 * libcurl Write-Callback.
 * Schreibt ankommende Daten in den mem_writer-Puffer.
 * Bei Überlauf wird das overflow-Flag gesetzt, der Transfer aber
 * trotzdem fortgesetzt (Rückgabe von total), damit curl nicht abbricht.
 */
static size_t mem_write_cb(void *ptr, size_t size, size_t nmemb, void *userdata)
{
    size_t total = size * nmemb;
    struct mem_writer *mw = userdata;
    if (mw->overflow) {
        return total;
    }
    size_t can_write = mw->capacity - mw->len;
    if (total <= can_write) {
        memcpy(mw->buf + mw->len, ptr, total);
        mw->len += total;
    } else {
        if (can_write > 0) {
            memcpy(mw->buf + mw->len, ptr, can_write);
            mw->len += can_write;
        }
        mw->overflow = 1;
    }
    return total;
}

/**
 * Lädt den Inhalt einer URL vollständig in den Heap.
 *
 * SSL-Verhalten:
 *   - Normalbau:   Zertifikat und Hostname werden strikt geprüft
 *   - TEST_LOCAL_SERVER: Prüfung deaktiviert
 *
 * @param url       Vollständige URL
 * @param capacity  Maximale akzeptierte Antwortgröße
 * @param out       Wird mit den empfangenen Daten befüllt
 * @return          1 bei Erfolg HTTP 200 + kein Overflow, sonst 0
 */
static int fetch_url_to_heap(const char *url, size_t capacity, struct file_buf *out)
{
    out->data = NULL;
    out->len  = 0;
    uint8_t *buf = malloc(capacity + 1);
    if (!buf) {
        return 0;
    }
    memset(buf, 0, capacity + 1);
    CURL *curl = curl_easy_init();
    if (!curl) {
        free(buf);
        return 0;
    }
    struct mem_writer mw = {
        .buf      = buf,
        .capacity = capacity,
        .len      = 0,
        .overflow = 0
    };
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, mem_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &mw);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "ixxc-test/1.0");
    #ifdef TEST_LOCAL_SERVER
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    #else
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTPS);
    #endif
    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);
    if (res != CURLE_OK) {
        fprintf(stderr, "curl fehlgeschlagen für %s: %s\n", url, curl_easy_strerror(res));
        free(buf);
        return 0;
    }
    if (http_code != 200) {
        fprintf(stderr, "HTTP-Fehler für %s: Status %ld\n", url, http_code);
        free(buf);
        return 0;
    }
    if (mw.overflow) {
        fprintf(stderr, "Puffer zu klein für %s (cap=%zu)\n", url, capacity);
        free(buf);
        return 0;
    }
    buf[mw.len] = '\0';
    out->data = buf;
    out->len  = mw.len;
    return 1;
}

/**
 * Vergleicht zwei Speicherbereiche per XOR.
 * Die Bereiche gelten als identisch, wenn alle XOR-Ergebnisse 0 sind
 * und beide die gleiche Länge besitzen.
 *
 * @return  1 wenn identisch, 0 wenn unterschiedlich
 */
static int xor_equal(const uint8_t *a, size_t a_len, const uint8_t *b, size_t b_len)
{
    if (a_len != b_len) {
         return 0;
    }
    if (a_len == 0) {
        return 1;
    }
    uint8_t acc = 0;
    for (size_t i = 0; i < a_len; ++i) {
        acc |= (uint8_t)(a[i] ^ b[i]);
    }
    return (acc == 0);
}

/**
 * Prüft, ob alle in IXXC_ENDPOINTS hinterlegten Quelldateien
 * auf dem lokalen Dateisystem existieren und lesbar sind.
 *
 * @param missing_count  Wird mit der Anzahl fehlender Dateien gefüllt darf NULL sein
 * @return               Anzahl der fehlenden Dateien alles vorhanden: 0
 */
static int check_all_local_files(size_t *missing_count)
{
    if (missing_count) {
        *missing_count = 0;
    }
    size_t n = IXXC_ENDPOINTS_COUNT;
    int missing = 0;
    for (size_t i = 0; i < n; ++i) {
        const char *path = IXXC_ENDPOINTS[i].source_file_path;
        if (path == NULL || access(path, R_OK) != 0) {
            printf(COLOR_RED "[MISSING]" COLOR_RESET " %s\n", path ? path : "(null)");
            ++missing;
        } else {
            printf(COLOR_GREEN "[OK]" COLOR_RESET " %s\n", path);
        }
    }
    if (missing_count) {
        *missing_count = (size_t)missing;
    }
    return missing;
}

/**
 * Ablauf:
 *  1. Prüfen, ob alle lokalen Quelldateien vorhanden sind
 *  2. Alle lokalen Dateien vollständig in den Heap laden
 *  3. Alle Endpunkte vom Server fetchen und ebenfalls im Heap halten
 *  4. Jeden lokalen Puffer per XOR mit dem Remote-Puffer vergleichen
 *  5. Ergebnis farbig ausgeben und Aufräumen
 */
int main(void)
{
    printf("starte test\n");
    size_t missing = 0;
    if (check_all_local_files(&missing) > 0) {
        fprintf(stderr, "Es fehlen %zu lokale Dateien. Abbruch.\n", missing);
        return 1;
    }
    size_t n = IXXC_ENDPOINTS_COUNT;
    if (n == 0) {
        fprintf(stderr, "Keine Endpunkte vorhanden.\n");
        return 1;
    }
    struct file_buf *local_files  = calloc(n, sizeof(struct file_buf));
    struct file_buf *remote_files = calloc(n, sizeof(struct file_buf));
    char           **names        = calloc(n, sizeof(char *));
    if (!local_files || !remote_files || !names) {
        perror("calloc");
        return 1;
    }
    printf("\nLade lokale Dateien...\n");
    for (size_t i = 0; i < n; ++i) {
        const char *path = IXXC_ENDPOINTS[i].source_file_path;
        char ep_path[0x1000];
        extract_path_from_endpoint(IXXC_ENDPOINTS[i].endpoint, ep_path, sizeof(ep_path));
        char name[0x200];
        basename_from_path(ep_path, name, sizeof(name));
        if (name[0] == '\0') {
            strncpy(name, "index", sizeof(name) - 1);
        }
        names[i] = strdup(name);
        if (!load_file_to_heap(path, &local_files[i])) {
            fprintf(stderr, "Konnte lokale Datei nicht laden: %s\n", path);
            local_files[i].data = NULL;
            local_files[i].len  = 0;
        } else {
            printf("  lokal  %-40s  %zu Bytes\n", name, local_files[i].len);
        }
    }
    printf("\nFetche vom Server...\n");
    curl_global_init(CURL_GLOBAL_DEFAULT);
    for (size_t i = 0; i < n; ++i) {
        char ep_path[0x1000];
        extract_path_from_endpoint(IXXC_ENDPOINTS[i].endpoint, ep_path, sizeof(ep_path));
        char url[0x2000];
        snprintf(url, sizeof(url), "%s%s", TARGET_URL, ep_path);
        size_t cap = local_files[i].len > 0 ? local_files[i].len + 64 : DEFAULT_BUFFER_SIZE;
        if (cap > MAX_FILE_SIZE) {
            cap = MAX_FILE_SIZE;
        }
        printf("  fetch  %s\n", url);
        if (!fetch_url_to_heap(url, cap, &remote_files[i])) {
            fprintf(stderr, "  Fehler beim Fetch von %s\n", url);
            remote_files[i].data = NULL;
            remote_files[i].len  = 0;
        } else {
            printf("         -> %zu Bytes\n", remote_files[i].len);
        }
    }
    curl_global_cleanup();
    printf("\nErgebnis der XOR-Vergleiche:\n");
    int mismatches = 0;
    for (size_t i = 0; i < n; ++i) {
        const char *name = names[i] ? names[i] : "?";
        int equal = 0;
        if (local_files[i].data && remote_files[i].data) {
            equal = xor_equal(local_files[i].data,  local_files[i].len, remote_files[i].data, remote_files[i].len);
        }
        if (equal) {
            printf(COLOR_GREEN "[OK] %s" COLOR_RESET "  (%zu Bytes)\n", name, local_files[i].len);
        } else {
            printf(COLOR_RED "[MISMATCH] %s" COLOR_RESET "\n", name);
            printf("  lokal : %zu Bytes\n", local_files[i].len);
            printf("  remote: %zu Bytes\n", remote_files[i].len);
            mismatches++;
        }
    }
    for (size_t i = 0; i < n; ++i) {
        free(local_files[i].data);
        free(remote_files[i].data);
        free(names[i]);
    }
    free(local_files);
    free(remote_files);
    free(names);
    printf("\nfinish test – %d mismatch(es)\n", mismatches);
    return mismatches > 0 ? 1 : 0;
}
