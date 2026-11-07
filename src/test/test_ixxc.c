#define _POSIX_C_SOURCE 200809L

#include "../../ixxc_endpoints.h"

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

#define DEFAULT_BUFFER_SIZE 0x8000 /* Fallback-Buffersize */

#define MAX_FILE_SIZE 0x6400000 /* 100 MiB max heap size per file */

#define TARGET_URL "http://127.0.0.1:8080"

struct file_buf {
    uint8_t *data;
    size_t   len;
};

struct mem_writer {     /* Allows direct writing of received data into a preallocated buffer */
    uint8_t *buf;       /* Target buffer */
    size_t   capacity;  /* Maximum capacity */
    size_t   len;       /* Bytes already written */
    int      overflow;  /* 1 = buffer was too small */
};

/*
 * Extracts the basename (filename) from a path.
 * Example: /foo/bar/index.html becomes index.html
 * Returns "index" for empty or invalid paths.
 *
 * @param path    Input path
 * @param out     Output buffer for the filename
 * @param outlen  Size of the output buffer
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

/*
 * Extracts the path part from an HTTP endpoint string.
 * Example: "GET /endpoint" becomes "/endpoint"
 * If no space is found, the whole string is copied.
 *
 * @param endpoint  Endpoint string
 * @param out       Output buffer for the path
 * @param outlen    Size of the output buffer
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

/*
 * Loads an entire file into the heap.
 * The file may be up to MAX_FILE_SIZE bytes.
 *
 * @param path  Path to file
 * @param out   Will be filled with pointer and length
 * @return      1 on success, 0 on error
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

/*
 * libcurl Write-Callback.
 * Writes incoming data into the mem_writer buffer.
 * On overflow, sets the overflow flag, but
 * continues transfer returns total, so curl does not abort.
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

/*
 * Loads the content of a URL completely into the heap.
 *
 * @param url       Full URL
 * @param capacity  Maximum accepted response size
 * @param out       Will be filled with the received data
 * @return          1 on HTTP 200 + no overflow, else 0
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
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);
    if (res != CURLE_OK) {
        fprintf(stderr, "curl error for %s: %s\n", url, curl_easy_strerror(res));
        free(buf);
        return 0;
    }
    if (http_code != 200) {
        fprintf(stderr, "HTTP-error for %s: Status %ld\n", url, http_code);
        free(buf);
        return 0;
    }
    if (mw.overflow) {
        fprintf(stderr, "buffer to small %s (cap=%zu)\n", url, capacity);
        free(buf);
        return 0;
    }
    buf[mw.len] = '\0';
    out->data = buf;
    out->len  = mw.len;
    return 1;
}

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

/*
 * Checks if all source files listed in IXXC_ENDPOINTS
 * exist and are readable on the local filesystem.
 *
 * @param missing_count  Will be filled with the count of missing files
 * @return               Number of missing files
 *                       0 if all present
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

/*
 * Procedure:
 *  1. Check whether all local source files exist
 *  2. Load all local files completely into the heap
 *  3. Fetch all endpoints from the server and hold them in the heap as well
 *  4. Compare each local buffer with the remote buffer via XOR
 *  5. Output result in color and cleanup
 */
int main(void)
{
    printf("start test\n");
    size_t missing = 0;
    if (check_all_local_files(&missing) > 0) {
        fprintf(stderr, "%zu local files are missing. Aborting.\n", missing);
        return 1;
    }
    size_t n = IXXC_ENDPOINTS_COUNT;
    if (n == 0) {
        fprintf(stderr, "No endpoints available.\n");
        return 1;
    }
    struct file_buf *local_files  = calloc(n, sizeof(struct file_buf));
    struct file_buf *remote_files = calloc(n, sizeof(struct file_buf));
    char           **names        = calloc(n, sizeof(char *));
    if (!local_files || !remote_files || !names) {
        perror("calloc");
        return 1;
    }
    printf("\nLoading local files...\n");
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
            fprintf(stderr, "Could not load local file: %s\n", path);
            local_files[i].data = NULL;
            local_files[i].len  = 0;
        } else {
            printf("  lokal  %-40s  %zu Bytes\n", name, local_files[i].len);
        }
    }
    printf("\nFetch from server...\n");
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
            fprintf(stderr, "  Error fetch from %s\n", url);
            remote_files[i].data = NULL;
            remote_files[i].len  = 0;
        } else {
            printf("         -> %zu Bytes\n", remote_files[i].len);
        }
    }
    curl_global_cleanup();
    printf("\nXOR-Comparison results:\n");
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
    printf("\nfinish test: %d mismatch(es)\n", mismatches);
    return mismatches > 0 ? 1 : 0;
}
