#include "../../header/prod/common.h"
#include "../../header/prod/connection.h"
#include "../../header/prod/security.h"
#include "../../header/prod/log.h"
#include "../../header/prod/meme_types_handler.h"

char   *attr_html_mem[NR_OF_ENDPOINTS];
size_t  attr_html_len[NR_OF_ENDPOINTS];

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
        if (html_starts[i] > html_total_bytes || html_lengths[i] > html_total_bytes || html_starts[i] + html_lengths[i] > html_total_bytes) {
            fprintf(stderr, "init_html_attr: bad range at %d\n", i);
            return -1;
        }
        attr_html_mem[i] = (char *)&html_data[html_starts[i]];
        attr_html_len[i] = (size_t)html_lengths[i];
        sum += html_lengths[i];
    }
    if (sum != html_total_bytes) {
        fprintf(stderr, "init_html_attr: sanity check failed\n");
        return -1;
    }
    initialized = 1;
    return 0;
}

#if USE_SSL

static ssize_t full_write_ssl(SSL *ssl, const void *buf, size_t len)
{
    const unsigned char *p = buf;
    size_t left = len;
    while (left > 0) {
        int w = SSL_write(ssl, p, (int)left);
        if (w <= 0) {
            int err = SSL_get_error(ssl, w);
            if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) continue;
            return -1;
        }
        left -= (size_t)w;
        p += w;
    }
    return (ssize_t)len;
}

static ssize_t full_read_ssl(SSL *ssl, void *buf, size_t size)
{
    int r = SSL_read(ssl, buf, (int)size);
    if (r <= 0) {
        int err = SSL_get_error(ssl, r);
        if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) return 0;
        return -1;
    }
    return (ssize_t)r;
}

void send_endpoint_ssl(SSL *ssl, size_t idx)
{
    if (idx > (size_t)NR_OF_ENDPOINTS) return;
    size_t len = 0;
    if (idx < (size_t)NR_OF_ENDPOINTS) {
        len = attr_html_len[idx];
        if (len == 0 || len > MAX_HTML) return;
    }
    char *header = NULL;
    size_t hlen = 0;
    get_header_and_meme(&header, &hlen, idx, len);
    full_write_ssl(ssl, header, (size_t)hlen);
    if (idx < (size_t)NR_OF_ENDPOINTS) {
        full_write_ssl(ssl, attr_html_mem[idx], len);
    }
}

#else

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

static ssize_t full_read_fd(int fd, void *buf, size_t size)
{
    ssize_t r;
    do {
        r = read(fd, buf, size);
    } while (r < 0 && errno == EINTR);
    return r;
}

void send_endpoint_plain(int fd, size_t idx)
{
    if (idx > (size_t)NR_OF_ENDPOINTS) return;
    size_t len = 0;
    if (idx < (size_t)NR_OF_ENDPOINTS) {
        len = attr_html_len[idx];
        if (len == 0 || len > MAX_HTML) return;
    }
    char *header = NULL;
    size_t hlen = 0;
    get_header_and_meme(&header, &hlen, idx, len);
    full_write_fd(fd, header, (size_t)hlen);
    if (idx < (size_t)NR_OF_ENDPOINTS) {
        full_write_fd(fd, attr_html_mem[idx], len);
    }
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

void handle_connection_fd(int worker_index, int cfd, struct sockaddr_in *caddr
#if USE_SSL
, SSL_CTX *ctx
#endif
) {
    char ip[INET_ADDRSTRLEN] = "unknown";
    inet_ntop(AF_INET, &caddr->sin_addr, ip, sizeof(ip));
    #if USE_SSL
    SSL *ssl = SSL_new(ctx);
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
    assembly_zero_buf(buf, sizeof(buf));
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
        if (is_http && constant_time_compare(buf, end_buf_index, IXXC_ENDPOINTS[index_endpoint].endpoint) == 0) {
            #if USE_SSL
            send_endpoint_ssl(ssl, index_endpoint);
            #else
            send_endpoint_plain(cfd, index_endpoint);
            #endif
            goto done;
        }
    }
    #if USE_SSL
    send_endpoint_ssl(ssl, index_endpoint);
    #else
    send_endpoint_plain(cfd, index_endpoint);
    #endif
    done:
    #if USE_SSL
    if (ssl) {
        SSL_shutdown(ssl);
        SSL_free(ssl);
    }
    #endif
    close(cfd);
}
