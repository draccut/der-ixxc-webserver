#define _GNU_SOURCE

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/resource.h>
#include <pwd.h>
#include <grp.h>
#include <signal.h>
#include <time.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdint.h>

#include "html_data.h"
#include "ixxc_endpoints.h"

#define USE_HTTP
#define USE_PRODUCTION_MODE
#define USE_ONLY_LOCALHOST

#ifdef USE_HTTP
    #define PORT 8080
    #define USE_SSL 0
#else
    #define PORT 443
    #define USE_SSL 1
    #include <openssl/ssl.h>
    #include <openssl/err.h>
#endif

#define CERT_PATH "/etc/letsencrypt/live/ixxc.de/fullchain.pem"
#define KEY_PATH "/etc/letsencrypt/live/ixxc.de/privkey.pem"

#if defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__)
    #define IS_BIG_ENDIAN (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
#elif defined(__BIG_ENDIAN__) || defined(_BIG_ENDIAN)
    #define IS_BIG_ENDIAN 1
#else
    #define IS_BIG_ENDIAN 0
#endif

#ifndef USE_PRODUCTION_MODE
    #define LOG_FILE_MACRO "my_server_access.log"
#else
    #define LOG_FILE_MACRO "/var/log/ixxc_server.log"
#endif

#define DROP_USER              "ixxc"
#define MAX_READ                0xFFF
#define WORKERS                 0x4
#define RING_BUFFER_SIZE        0x100
#define MAX_LOG_LINE            0x2000
#define MAX_HTML               (8UL << 20)
#define MAX_LOG_SIZE           (5000000000ULL) /* 5GB */

#if (RING_BUFFER_SIZE & (RING_BUFFER_SIZE - 1)) != 0
    #error "RING_BUFFER_SIZE must be a power of two (e.g. 16, 32, 64, 128, 256)"
#endif

#if RING_BUFFER_SIZE < 2
    #error "RING_BUFFER_SIZE must be at least 2"
#endif

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

/**
* Die Header-Makros enthalten "%zu" als Platzhalter für Content-Length.
* sizeof( . . . ) zählt die 3 Zeichen von "%zu" mit.
* Diese werden zur Laufzeit durch die tatsächliche Zahl ersetzt und
* erscheinen nicht in der fertigen Ausgabe.
* Deshalb: sizeof(Formatstring) - 3 + maximale Ziffernlänge einer uint64_t.
*/
#define MAX_UINT64_STR                    "18446744073709551615"
#define MAX_UINT64_STR_LEN                (sizeof(MAX_UINT64_STR))
#define IXXC_SERVER_HEADER_LEN            (sizeof(IXXC_SERVER_HEADER)           + MAX_UINT64_STR_LEN - 3)
#define IXXC_SERVER_HEADER_DOWNLOAD_LEN   (sizeof(IXXC_SERVER_HEADER_DOWNLOAD)  + MAX_UINT64_STR_LEN - 3)
#define IXXC_SERVER_HEADER_WEBP_LEN       (sizeof(IXXC_SERVER_HEADER_WEBP)      + MAX_UINT64_STR_LEN - 3)
#define IXXC_FORBIDDEN_LEN                (sizeof(IXXC_FORBIDDEN))

static const char * const LOG_FILE = LOG_FILE_MACRO;

typedef enum {
    MEME_TYPE_DOWNLOAD,
    MEME_TYPE_TEXT,
    MEME_TYPE_FORBIDDEN,
    MEME_TYPE_WEBP,
} Meme_type_enum_e;

typedef struct {
    char log_buffer[RING_BUFFER_SIZE][MAX_LOG_LINE];
    _Atomic unsigned int write_index;
    _Atomic unsigned int read_index;
} shared_log_t;

volatile sig_atomic_t shutdown_requested = 0;
pid_t worker_pids[WORKERS];
static shared_log_t *shared_log = NULL;
char   *attr_html_mem[NR_OF_ENDPOINTS];
size_t  attr_html_len[NR_OF_ENDPOINTS];

/**
 * Initialisiert die HTML-Attribute aus dem eingebetteten html_data.
 * Prüft die Konsistenz der Start- und Längen-Arrays und speichert
 * Zeiger sowie Längen in attr_html_mem und attr_html_len.
 *
 * @return 0 bei Erfolg, -1 bei Fehler
 */
int init_html_attr(void) 
{
    static int initialized = 0;
    if (initialized) return 0;
    if (html_total_bytes == 0) {
        fprintf(stderr, "init_html_attr: html_total_bytes == 0\n");
        return -1;
    }
    size_t sum = 0;
    for (int i = 0; i < NR_OF_ENDPOINTS; ++i) {
        if (html_starts[i] > html_total_bytes) {
            fprintf(stderr, "init_html_attr: bad html_starts[%d] = %zu\n", i, (size_t)html_starts[i]);
            return -1;
        }
        if (html_lengths[i] > html_total_bytes) {
            fprintf(stderr, "init_html_attr: bad html_lengths[%d] = %zu\n", i, (size_t)html_lengths[i]);
            return -1;
        }
        if (html_starts[i] + html_lengths[i] > html_total_bytes) {
            fprintf(stderr, "init_html_attr: start+len out of range for index %d\n", i);
            return -1;
        }
        attr_html_mem[i] = (char *)&html_data[ html_starts[i] ];
        attr_html_len[i] = (size_t)html_lengths[i];
        sum += html_lengths[i];
    }
    if (sum != html_total_bytes) {
        fprintf(stderr, "init_html_attr: sanity check failed: sum=%zu total=%zu\n", sum, (size_t)html_total_bytes);
        return -1;
    }
    initialized = 1;
    return 0;
}

/**
 * Legt den gemeinsamen Ringpuffer für asynchrone Logs im Shared Memory an
 * (mmap MAP_SHARED | MAP_ANONYMOUS) und initialisiert die atomaren Indizes.
 */
void init_shared_log(void) 
{
    shared_log = mmap(NULL, sizeof(shared_log_t),
                      PROT_READ | PROT_WRITE,
                      MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (shared_log == MAP_FAILED) {
        perror("mmap shared log");
        exit(1);
    }
    atomic_init(&shared_log->write_index, 0);
    atomic_init(&shared_log->read_index, 0);
}

/**
 * Gibt den Shared-Memory-Bereich des Log-Ringpuffers wieder frei.
 */
void cleanup_shared_log(void) 
{
    if (shared_log != NULL && shared_log != MAP_FAILED) {
        if (munmap(shared_log, sizeof(shared_log_t)) == -1) {
            perror("munmap shared log");
        } else {
            printf("[Parent] Shared Log Speicher erfolgreich freigegeben.\n");
            shared_log = NULL;
        }
    }
}

/**
 * Beendet alle Worker-Prozesse sauber (SIGTERM + waitpid) und gibt
 * anschließend den Shared-Memory-Log-Bereich frei.
 */
void shutdown_all_workers_and_free_mmap(void) 
{
    printf("\n[Parent] Signal empfangen! Fahre Worker herunter...\n");
    for (int i = 0; i < WORKERS; ++i) {
        if (worker_pids[i] > 0) {
            printf("[Parent] Sende SIGTERM an Worker %d (PID: %d)...\n", i, worker_pids[i]);
            if (kill(worker_pids[i], SIGTERM) == -1) {
                perror("kill worker fehlgeschlagen");
            }
        }
    }
    for (int i = 0; i < WORKERS; ++i) {
        if (worker_pids[i] > 0) {
            int status;
            if (waitpid(worker_pids[i], &status, 0) == -1) {
                 if (errno != ECHILD) {
                    perror("waitpid fehlgeschlagen");
                } else {
                    printf("[Parent] Worker %d (PID: %d) war bereits beendet.\n", i, worker_pids[i]);
                }
            } else {
                if (WIFEXITED(status)) {
                    printf("[Parent] Worker %d (PID: %d) sauber beendet mit Code %d.\n", 
                        i, worker_pids[i], WEXITSTATUS(status));
                } else if (WIFSIGNALED(status)) {
                    printf("[Parent] Worker %d (PID: %d) durch Signal %d getötet.\n", 
                        i, worker_pids[i], WTERMSIG(status));
                }
            }
        }
    }
    cleanup_shared_log();
    ixxc_cleanup_endpoints();
    printf("[Parent] Alle Ressourcen bereinigt. Server beendet.\n");
}

/**
 * Legt die Logdatei an (falls sie noch nicht existiert) und setzt
 * sichere Besitz- und Rechte-Informationen.
 *
 * @param path        Pfad zur Logdatei
 * @param owner_name  Benutzername des Besitzers
 * @param group_name  Gruppenname
 * @param mode        Dateirechte (z. B. 0640)
 * @return            0 bei Erfolg, -1 bei Fehler
 */
int prepare_log_file(const char *path, const char *owner_name, const char *group_name, mode_t mode) 
{
    if (!path || !owner_name || !group_name) {
        errno = EINVAL;
        return -1;
    }
    int fd = open(path, O_CREAT | O_NOFOLLOW | O_WRONLY, mode);
    if (fd < 0) {
        if (errno == EEXIST) {
            fd = open(path, O_WRONLY);
            if (fd < 0) {
                perror("open existing logfile");
                return -1;
            }
        } else {
            perror("create logfile");
            return -1;
        }
    }
    struct stat st;
    if (fstat(fd, &st) != 0) {
        perror("fstat logfile: No Symlink no Device");
        close(fd);
        return -1;
    }
    if (!S_ISREG(st.st_mode)) {
        fprintf(stderr, "logfile is not a regular file\n");
        close(fd);
        errno = EINVAL;
        return -1;
    }
    struct passwd *pw = getpwnam(owner_name);
    if (!pw) {
        fprintf(stderr, "unknown user(Owner Group kann nicht aufgeloest werden): %s\n", owner_name);
        close(fd);
        errno = EINVAL;
        return -1;
    }
    struct group *gr = getgrnam(group_name);
    if (!gr) {
        fprintf(stderr, "unknown group: %s\n", group_name);
        close(fd);
        errno = EINVAL;
        return -1;
    }
    if (fchown(fd, pw->pw_uid, gr->gr_gid) != 0) {
        perror("ERROR: Setze Besitzer und Gruppe: fchown logfile");
    }
    if (fchmod(fd, mode) != 0) {
        perror("ERROR: (fchmod): Setze Modus: fchmod logfile");
        close(fd);
        return -1;
    }
    if (fstat(fd, &st) == 0) {
        if (st.st_uid != pw->pw_uid || st.st_gid != gr->gr_gid) {
            fprintf(stderr, "Warnung: owner/group mismatch after fchown\n");
        }
        if (st.st_mode & S_IWOTH) {
            fprintf(stderr, "Warnung: logfile ist world-writable (unsicher)\n");
        }
    }
    close(fd);
    return 0;
}

#if USE_SSL
/**
 * Gibt alle anstehenden OpenSSL-Fehlermeldungen auf stderr aus.
 *
 * @param ctx  Kontext-String, der vor den Fehlern ausgegeben wird
 */
static void log_ssl_errors(const char *ctx) 
{
    fprintf(stderr, "[SSL ERR] %s\n", ctx);
    ERR_print_errors_fp(stderr);
}
#endif

/**
 * Öffnet die Logdatei sicher zum Anhängen (O_APPEND | O_NOFOLLOW | O_CLOEXEC).
 * Prüft zusätzlich, ob es sich um eine reguläre Datei handelt.
 *
 * @param path  Pfad zur Logdatei
 * @return      Filedescriptor oder -1 bei Fehler
 */
static int open_log_fd(const char *path) 
{
    int fd = open(path, O_CREAT | O_APPEND | O_WRONLY | O_NOFOLLOW | O_CLOEXEC, 0640);
    if (fd < 0) {
        perror("ERROR: open log");
        return -1;
    }
    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
        close(fd);
        errno = EINVAL;
        return -1;
    }
    return fd;
}

/**
 * Schreibt asynchron einen Log-Eintrag in den gemeinsamen Ringpuffer.
 * Der Eintrag enthält Zeitstempel, Client-IP, Worker-Index und den
 * (teilweise sanitisierten) Request-Puffer.
 *
 * Der Schreibindex wird atomar reserviert. Ist der Ringpuffer voll,
 * wird der Eintrag verworfen.
 *
 * @param worker_index  Index des aktuellen Workers
 * @param ip            Client-IP als String
 * @param buf           Request-Daten
 * @param n             Länge der Request-Daten
 */
static void add_log_buffer_async(int worker_index, const char *ip, const char *buf, int n) 
{
    if (!shared_log) return;
    unsigned int r_idx = atomic_load(&shared_log->read_index);
    unsigned int w_idx = atomic_load(&shared_log->write_index);
    if (w_idx - r_idx >= RING_BUFFER_SIZE) {
        return;
    }
    unsigned int current_index;
    do {
        current_index = atomic_load(&shared_log->write_index);
        if (current_index - atomic_load(&shared_log->read_index) >= RING_BUFFER_SIZE) {
            return;
        }
    } while (!atomic_compare_exchange_weak(&shared_log->write_index, &current_index, current_index + 1));
    int slot = current_index & (RING_BUFFER_SIZE - 1);
    time_t t = time(NULL);
    char ts[32];
    struct tm *tm_info = localtime(&t);
    if (tm_info) strftime(ts, sizeof(ts), "%F %T", tm_info);
    else strncpy(ts, "0000-00-00 00:00:00", sizeof(ts));
    int current_len = snprintf(shared_log->log_buffer[slot], MAX_LOG_LINE, "[%s] [%s] [%d] ", ts, ip, worker_index);
    if (current_len < 0) current_len = 0;
    if (current_len >= MAX_LOG_LINE) current_len = MAX_LOG_LINE - 1;
    for (int i = 0; i < n && current_len < (MAX_LOG_LINE - 1); ++i) {
        unsigned char c = (unsigned char)buf[i];
        if (c >= 32 && c <= 126) {
            shared_log->log_buffer[slot][current_len++] = c;
        } else {
            if (current_len + 5 < MAX_LOG_LINE) {
                current_len += snprintf(&shared_log->log_buffer[slot][current_len], 6, " 0x%02X", c);
            } else {
                break;
            }
        }
    }
    shared_log->log_buffer[slot][current_len] = '\0';
}

/**
 * Gibt die Root-Privilegien ab und wechselt zum angegebenen Benutzer.
 * Setzt zusätzlich PR_SET_DUMPABLE=0 und PR_SET_NO_NEW_PRIVS=1.
 *
 * @param user  Benutzername, zu dem gewechselt werden soll
 */
static void drop_privileges(const char *user) 
{
    if (getuid() != 0) {
        return;
    }
    struct passwd *pw = getpwnam(user);
    if (!pw) {
        fprintf(stderr, "User '%s' nicht gefunden\n", user);
        exit(EXIT_FAILURE);
    }
    if (initgroups(pw->pw_name, pw->pw_gid) != 0) {
        perror("initgroups");
        exit(EXIT_FAILURE);
    }
    if (setresgid(pw->pw_gid, pw->pw_gid, pw->pw_gid) != 0) {
        perror("setresgid");
        exit(EXIT_FAILURE);
    }
    if (setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid) != 0) {
        perror("setresuid");
        exit(EXIT_FAILURE);
    }
    prctl(PR_SET_DUMPABLE, 0);
    prctl(PR_SET_NO_NEW_PRIVS, 1);
    prctl(PR_SET_NAME, "server_worker");
}

/**
 * Führt einen konstantzeitigen Vergleich durch (gegen Timing-Angriffe).
 *
 * Der Input wird auf eine volle 4-KiB-Page aufgefüllt und dann
 * bitweise mit dem Vergleichswert (ebenfalls 4 KiB) verglichen.
 *
 * @param input       Beliebiger String (darf kürzer als 0x1000 sein)
 * @param input_len   Länge von input (ohne '\0')
 * @param compare_to  Muss immer genau 0x1000 Bytes groß sein
 * @return            0 bei Gleichheit, 1 bei Ungleichheit
 */
__attribute__((noinline, optimize("O0")))
int constant_time_compare(const char *input, size_t input_len, const char *compare_to)
{
    if (!input || !compare_to || input_len > 0xFFF) {
        return 1;
    }
    char input_page[0x1000];
    memset(input_page, 0, sizeof(input_page));
    if (input_len > 0) {
        size_t copy_len = (input_len > 0x1000) ? 0x1000 : input_len;
        memcpy(input_page, input, copy_len);
    }
    volatile uint64_t diff = 0;
    const uint64_t *a = (const uint64_t *)input_page;
    const uint64_t *b = (const uint64_t *)compare_to;
    for (size_t i = 0; i < MASKU_64BITXORGATTER; ++i) {
        uint64_t va = a[i];
        uint64_t vb = b[i];
        #if !IS_BIG_ENDIAN
        va = __builtin_bswap64(va);
        vb = __builtin_bswap64(vb);
        #endif
        diff |= (va ^ vb);
    }
    __asm__ __volatile__("" : : "r"(diff) : "memory");
    return (diff != 0);
}

#if USE_SSL
/**
 * Schreibt den kompletten Puffer über eine SSL-Verbindung
 * (handelt partielle Writes und WANT_READ/WANT_WRITE ab).
 *
 * @return  Anzahl der geschriebenen Bytes oder -1 bei Fehler
 */
static ssize_t full_write_ssl(SSL *ssl, const void *buf, size_t len) 
{
    const unsigned char *p = buf;
    size_t left = len;
    while (left > 0) {
        int w = SSL_write(ssl, p, (int)left);
        if (w <= 0) {
            int err = SSL_get_error(ssl, w);
            if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
                continue;
            }
            return -1;
        }
        left -= (size_t)w;
        p += w;
    }
    return (ssize_t)len;
}

#else
/**
 * Schreibt den kompletten Puffer auf einen Filedescriptor
 * (handelt partielle Writes und EINTR ab).
 *
 * @return  Anzahl der geschriebenen Bytes oder -1 bei Fehler
 */
static ssize_t full_write_fd(int fd, const void *buf, size_t len) 
{
    const unsigned char *p = buf;
    size_t left = len;
    while (left > 0) {
        ssize_t w = write(fd, p, left);
        if (w < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        left -= (size_t)w;
        p += w;
    }
    return (ssize_t)len;
}

#endif

#if USE_SSL
/**
 * Liest Daten von einer SSL-Verbindung.
 * Bei WANT_READ/WANT_WRITE wird 0 zurückgegeben (Caller kann erneut versuchen).
 *
 * @return  Anzahl gelesener Bytes, 0 bei Retry, -1 bei Fehler
 */
static ssize_t full_read_ssl(SSL *ssl, void *buf, size_t size) 
{
    int r = SSL_read(ssl, buf, (int)size);
    if (r <= 0) {
        int err = SSL_get_error(ssl, r);
        if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
            return 0; /* caller may retry */
        }
        return -1;
    }
    return (ssize_t)r;
}
#else
/**
 * Liest Daten von einem Filedescriptor (handelt EINTR ab).
 *
 * @return  Anzahl gelesener Bytes oder -1 bei Fehler
 */
static ssize_t full_read_fd(int fd, void *buf, size_t size) 
{
    ssize_t r;
    do {
        r = read(fd, buf, size);
    } while (r < 0 && errno == EINTR);
    return r;
}

#endif

/**
 * Bestimmt den Content-Typ eines Endpoints anhand der Dateiendung.
 *
 * @param index_endpoint  Index des Endpoints
 * @return                MEME_TYPE_DOWNLOAD, MEME_TYPE_WEBP,
 *                        MEME_TYPE_TEXT oder MEME_TYPE_FORBIDDEN
 */
static Meme_type_enum_e detect_meme_type(size_t index_endpoint) 
{
    if (index_endpoint == IXXC_ENDPOINTS_COUNT) return MEME_TYPE_FORBIDDEN;
    const char *filename = IXXC_ENDPOINTS[index_endpoint].endpoint;
    if (!filename) return MEME_TYPE_FORBIDDEN;
    const char *dot = strrchr(filename, '.');
    if (dot) {
        if (strcmp(dot, ".webp") == 0) {
            return MEME_TYPE_WEBP;
        }
        if ((strcmp(dot, ".png") == 0) || (strcmp(dot, ".pdf") == 0) || (strcmp(dot, ".odt") == 0) || (strcmp(dot, ".odg") == 0) || (strcmp(dot, ".docx") == 0)) {
            return MEME_TYPE_DOWNLOAD;
        }
    }
    return MEME_TYPE_TEXT;
}

/**
 * Erzeugt den passenden HTTP-Header (und ggf. den 403-Body)
 * für den angegebenen Endpoint.
 *
 * Der Caller ist für das Freigeben von *header verantwortlich.
 *
 * @param header  wird mit dem allozierten Header-String befüllt
 * @param hlen    Länge des Header-Strings
 * @param idx     Endpoint-Index
 * @param len     Content-Length (nur bei echten Endpoints relevant)
 * @return        0 bei Erfolg, -1 bei Fehler (löst Shutdown aus)
 */
static int get_header_and_meme(char **header, int *hlen, size_t idx, size_t len)
{
    size_t alloc = 0;
    *header = NULL;
    *hlen = 0;
    switch (detect_meme_type(idx)) {
        case MEME_TYPE_DOWNLOAD:
            alloc = IXXC_SERVER_HEADER_DOWNLOAD_LEN + 1;
            *header = malloc(alloc);
            if (!*header) {
		    goto error;
	    }
            *hlen = snprintf(*header, alloc, IXXC_SERVER_HEADER_DOWNLOAD, len);
            break;
        case MEME_TYPE_TEXT:
            alloc = IXXC_SERVER_HEADER_LEN + 1;
            *header = malloc(alloc);
            if (!*header) { 
		    goto error;
	    }
            *hlen = snprintf(*header, alloc, IXXC_SERVER_HEADER, len);
            break;
        case MEME_TYPE_FORBIDDEN:
            alloc = IXXC_FORBIDDEN_LEN + 1;
            *header = malloc(alloc);
            if (!*header) {
		    goto error;
	    }
            *hlen = snprintf(*header, alloc, IXXC_FORBIDDEN);
            break;
        case MEME_TYPE_WEBP:
            alloc = IXXC_SERVER_HEADER_WEBP_LEN + 1;
            *header = malloc(alloc);
            if (!*header) {
		    goto error;
	    }
            *hlen = snprintf(*header, alloc, IXXC_SERVER_HEADER_WEBP, len);
            break;
        default:
            goto error;
    }
    return 0;
    error:
	return -1;
}

#if USE_SSL
/**
 * Sendet den kompletten Response (Header + Body) über eine SSL-Verbindung.
 * Bei 403 wird nur der Header (der den Body bereits enthält) gesendet.
 *
 * @param ssl  SSL-Verbindung
 * @param idx  Endpoint-Index (IXXC_ENDPOINTS_COUNT = 403)
 */
static void send_endpoint_ssl(SSL *ssl, size_t idx)
{
    if (idx > (size_t)NR_OF_ENDPOINTS) return;
    size_t len = 0;
    if (idx < (size_t)NR_OF_ENDPOINTS) {
        len = attr_html_len[idx];
        if (len == 0 || len > MAX_HTML) return;
    }
    char *header = NULL;
    int hlen = 0;
    if (get_header_and_meme(&header, &hlen, idx, len) != 0) return;
    if (full_write_ssl(ssl, header, (size_t)hlen) < 0) {
        free(header);
        return;
    }
    if (idx < (size_t)NR_OF_ENDPOINTS) {
        full_write_ssl(ssl, (const void *)attr_html_mem[idx], len);
    }
    free(header);
}
#else
/**
 * Sendet den kompletten Response (Header + Body) über einen Socket.
 * Bei 403 wird nur der Header (der den Body bereits enthält) gesendet.
 *
 * @param fd   Client-Socket
 * @param idx  Endpoint-Index (IXXC_ENDPOINTS_COUNT = 403)
 */
static void send_endpoint_plain(int fd, size_t idx)
{
    if (idx > (size_t)NR_OF_ENDPOINTS) return;
    size_t len = 0;
    if (idx < (size_t)NR_OF_ENDPOINTS) {
        len = attr_html_len[idx];
        if (len == 0 || len > MAX_HTML) return;
    }
    char *header = NULL;
    int hlen = 0;
    if (get_header_and_meme(&header, &hlen, idx, len) != 0) return;
    if (full_write_fd(fd, header, (size_t)hlen) < 0) {
        free(header);
        return;
    }
    if (idx < (size_t)NR_OF_ENDPOINTS) {
        full_write_fd(fd, (const void *)attr_html_mem[idx], len);
    }
    free(header);
}
#endif

/**
 * Findet die Länge des Request-Pfads (bis zum zweiten Leerzeichen).
 * Wird als input_len für den constant-time-Vergleich verwendet.
 *
 * @param buf  Request-Puffer
 * @param len  Länge des Puffers
 * @return     Länge des Pfads oder 0 bei Fehler
 */
static size_t find_endpoint_length(const char *buf, size_t len)
{
    if (!buf || len == 0) {
        return 0;
    }
    size_t i = 0;
    while (i < len && buf[i] != ' ' && buf[i] != '\r' && buf[i] != '\n') {
        ++i;
    }
    if (i >= len || buf[i] != ' ') {
        return 0;
    }
    ++i;
    size_t path_start = i;
    while (i < len && buf[i] != ' ' && buf[i] != '\r' && buf[i] != '\n') {
        ++i;
    }
    if (i >= len || buf[i] != ' ') {
        return 0;
    }
    if (i - path_start == 1 && buf[path_start] == '/') {
        return i;
    }
    if (buf[i - 1] == '/') {
        return i - 1;
    }
    return i;
}

/**
 * Behandelt eine einzelne Client-Verbindung:
 * liest den Request, schreibt ihn in den Log-Ringpuffer,
 * sucht den passenden Endpoint und sendet die Antwort.
 *
 * @param worker_index  Index des aktuellen Workers
 * @param cfd           Client-Socket
 * @param caddr         Client-Adresse
 * @param ctx           (nur bei SSL) SSL-Kontext
 */
static void handle_connection_fd(int worker_index, int cfd, struct sockaddr_in *caddr
    #if USE_SSL
    , SSL_CTX *ctx
    #endif
) {
    char ip[INET_ADDRSTRLEN] = "unknown";
    inet_ntop(AF_INET, &caddr->sin_addr, ip, sizeof(ip));
    #if USE_SSL
    SSL *ssl = NULL;
    ssl = SSL_new(ctx);
    if (!ssl) {
        ERR_print_errors_fp(stderr);
        close(cfd);
        return;
    }
    SSL_set_fd(ssl, cfd);
    if (SSL_accept(ssl) <= 0) {
        ERR_print_errors_fp(stderr);
        SSL_free(ssl);
        close(cfd);
        return;
    }
    #endif
    char buf[MAX_READ];
    for (int i = 0; i < MAX_READ; ++i) {
        buf[i] = '\0';
    }
    ssize_t r;
    #if USE_SSL
    r = full_read_ssl(ssl, buf, sizeof(buf) - 1);
    #else
    r = full_read_fd(cfd, buf, sizeof(buf) - 1);
    #endif
    if (r <= 0) {
    #if USE_SSL
        if (ssl) {
            SSL_shutdown(ssl);
            SSL_free(ssl);
            return;
        }
    #endif
        close(cfd);
        return;
    }
    buf[r] = '\0';
    add_log_buffer_async(worker_index, ip, buf, (int)r);
    int is_http = (strstr(buf, "HTTP/1.1") != NULL || strstr(buf, "HTTP/1.0") != NULL);
    size_t index_endpoint;
    for (index_endpoint = 0; index_endpoint < IXXC_ENDPOINTS_COUNT; ++index_endpoint) {
        size_t end_buf_index = find_endpoint_length(buf, MAX_READ);
        int cmp_res = constant_time_compare(buf, end_buf_index, IXXC_ENDPOINTS[index_endpoint].endpoint);
        if (is_http && (cmp_res == 0)) {
            #if USE_SSL
                send_endpoint_ssl(ssl, index_endpoint);
            #else
                send_endpoint_plain(cfd, index_endpoint);
            #endif
            goto jmp_label_close_connection;
        }
    }
    /* Kein Endpoint gefunden → 403 */
    #if USE_SSL
        send_endpoint_ssl(ssl, index_endpoint);
    #else
        send_endpoint_plain(cfd, index_endpoint);
    #endif
    jmp_label_close_connection:
    #if USE_SSL
    if (ssl) {
        SSL_shutdown(ssl);
        SSL_free(ssl);
    }
    #endif
    close(cfd);
}

/**
 * Hauptschleife eines Worker-Prozesses.
 * Akzeptiert Verbindungen und übergibt sie an handle_connection_fd.
 *
 * @param i    Worker-Index
 * @param sfd  Listening-Socket
 * @param ctx  (nur bei SSL) SSL-Kontext
 */
void worker_loop(int i, int sfd
#if USE_SSL
    , SSL_CTX *ctx
#endif
) {
    for (;;) {
        struct sockaddr_in caddr;
        socklen_t len = sizeof(caddr);
        int cfd = accept4(sfd, (struct sockaddr*)&caddr, &len, SOCK_CLOEXEC);
        if (cfd < 0) {
            if (errno == EINTR) {
                printf("[Worker %d] Durch Signal unterbrochen. Beende worker_loop.\n", i);
                break;
            }
            sleep(1);
            continue;
        }
        struct timeval tv = { .tv_sec = 5, .tv_usec = 0 };
        setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(cfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        handle_connection_fd(i, cfd, &caddr
    #if USE_SSL
            , ctx
    #endif
        );
    }
    printf("[Worker %d] worker_loop sauber beendet.\n", i);
}

/**
 * Signal-Handler für SIGCHLD.
 * Räumt beendete Kindprozesse auf (verhindert Zombies).
 */
static void sigchld_handler(int signo) 
{
    (void)signo;
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

/**
 * Signal-Handler für SIGINT und SIGTERM im Parent-Prozess.
 * Setzt das Flag shutdown_requested, damit die Hauptschleife
 * die Worker beendet.
 */
void handle_parent_shutdown(int sig) 
{
    (void)sig;
    shutdown_requested = 1;
}

/**
 * Einstiegspunkt des Servers.
 *
 * - Initialisiert Endpoints und HTML-Daten
 * - richtet ggf. SSL-Kontext ein
 * - erstellt den Listening-Socket
 * - forkt die Worker-Prozesse
 * - schreibt asynchron die Logs aus dem Shared-Memory-Ringpuffer
 * - beendet alles sauber bei SIGINT/SIGTERM
 */
int main(void) 
{
    int log_writing_disabled = 0;
    signal(SIGINT, handle_parent_shutdown);
    signal(SIGTERM, handle_parent_shutdown);
    ixxc_init_endpoints();
    if (NR_OF_ENDPOINTS != IXXC_ENDPOINTS_COUNT) {
        fprintf(stderr, "NR_OF_ENDPOINTS != IXXC_ENDPOINTS_COUNT do you have current html_data.c\n");
        return 1;
    }
    if (init_html_attr() != 0) {
        fprintf(stderr, "init_html_attr failed\n");
        return 1;
    }
    #if USE_SSL
    SSL_library_init();
    OpenSSL_add_all_algorithms();
    SSL_load_error_strings();
    const SSL_METHOD *method = TLS_server_method();
    SSL_CTX *ctx = SSL_CTX_new(method);
    if (!ctx) {
        log_ssl_errors("SSL_CTX_new");
        exit(1);
    }
    SSL_CTX_set_min_proto_version(ctx, TLS1_3_VERSION);
    SSL_CTX_set_options(ctx, SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 |
                             SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1 |
                             SSL_OP_NO_COMPRESSION | SSL_OP_NO_RENEGOTIATION |
                             SSL_OP_CIPHER_SERVER_PREFERENCE);
    SSL_CTX_set_ciphersuites(ctx, "TLS_AES_256_GCM_SHA384:TLS_CHACHA20_POLY1305_SHA256");
    if (SSL_CTX_use_certificate_chain_file(ctx, CERT_PATH) <= 0 ||
        SSL_CTX_use_PrivateKey_file(ctx, KEY_PATH, SSL_FILETYPE_PEM) <= 0) {
        log_ssl_errors("Certificate/Key load");
        SSL_CTX_free(ctx);
        exit(1);
    }
    #endif
    int sfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sfd < 0) {
        perror("socket");
        exit(1);
    }
    int opt = 1;
    setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    #ifdef USE_ONLY_LOCALHOST
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(PORT),
        .sin_addr.s_addr = inet_addr("127.0.0.1")
    };
    #else
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(PORT),
        .sin_addr.s_addr = INADDR_ANY
    };
    #endif
    if (bind(sfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        exit(1);
    }
    if (listen(sfd, SOMAXCONN) < 0) {
        perror("listen");
        exit(1);
    }
    printf("Server gestartet → Port %d (%s)\n", PORT, USE_SSL ? "HTTPS" : "HTTP");
    signal(SIGCHLD, sigchld_handler);
    signal(SIGPIPE, SIG_IGN);
    printf("Init shared Log.");
    init_shared_log();
    printf("Starting forking worker\n");
    for (int i = 0; i < WORKERS; ++i) {
        pid_t p = fork();
        if (p < 0) {
            perror("fork");
            exit(1);
        } else if (p == 0) {
            signal(SIGTERM, SIG_DFL);
            drop_privileges(DROP_USER);
            worker_loop(i, sfd
            #if USE_SSL
                , ctx
            #endif
            );
            _exit(0);
        } else {
            worker_pids[i] = p;
        }
    }
    int log_fd = open_log_fd(LOG_FILE);
    while (1) {
        if (shared_log && log_fd >= 0 && !log_writing_disabled) {
            unsigned int r_idx = atomic_load(&shared_log->read_index);
            unsigned int w_idx = atomic_load(&shared_log->write_index);
            while (r_idx != w_idx) {
                int slot = r_idx & (RING_BUFFER_SIZE - 1);
                if (shared_log->log_buffer[slot][0] != '\0') {
                    struct stat st;
                    if (fstat(log_fd, &st) == 0 && (unsigned long)st.st_size >= MAX_LOG_SIZE) {
                        const char *msg =
                        "[CRITICAL] Logdatei hat MAX_LOG_SIZE (5 GB) erreicht. "
                        "Ab sofort wird nichts mehr geloggt.\n";
                        ssize_t bytes_written = write(log_fd, msg, strlen(msg));
                        if (bytes_written < 0) {
                            perror("Fehler beim Schreiben in die Logdatei.");
                        }
                        log_writing_disabled = 1;
                        shared_log->log_buffer[slot][0] = '\0';
                        r_idx++;
                        atomic_store(&shared_log->read_index, r_idx);
                        w_idx = atomic_load(&shared_log->write_index);
                        continue;
                    }
                    if (write(log_fd, shared_log->log_buffer[slot], strlen(shared_log->log_buffer[slot])) < 0) {
                        break;
                    }
                    if (write(log_fd, "\n", 1) < 0) {
                        break;
                    }
                    shared_log->log_buffer[slot][0] = '\0';
                }
                r_idx++;
                atomic_store(&shared_log->read_index, r_idx);
                w_idx = atomic_load(&shared_log->write_index);
            }
        }
        if (shutdown_requested == 1) {
            shutdown_all_workers_and_free_mmap();
            break;
        }
        struct timespec ts_sleep = {.tv_sec = 0, .tv_nsec = 100000000L}; // 100ms sleep
        nanosleep(&ts_sleep, NULL);
    }
    #if USE_SSL
    SSL_CTX_free(ctx);
    #endif
    printf("Server normal beendet\n");
    return 0;
}
