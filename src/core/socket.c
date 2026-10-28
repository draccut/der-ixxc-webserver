#include "../../header/prod/common.h"
#include "../../header/prod/socket.h"

int create_listening_socket_ixxc(void)
{
    int sfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sfd < 0) {
        perror("socket");
        return -1;
    }
    int opt = 1;
    setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    #ifdef USE_ONLY_LOCALHOST
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port   = htons(PORT),
        .sin_addr.s_addr = inet_addr("127.0.0.1")
    };
    #else
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port   = htons(PORT),
        .sin_addr.s_addr = INADDR_ANY
    };
    #endif
    if (bind(sfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(sfd);
        return -1;
    }
    if (listen(sfd, SOMAXCONN) < 0) {
        perror("listen");
        close(sfd);
        return -1;
    }
    return sfd;
}

#if USE_SSL

static void log_ssl_errors(const char *ctx)
{
    fprintf(stderr, "[SSL ERR] %s\n", ctx);
    ERR_print_errors_fp(stderr);
}

SSL_CTX *create_ssl_ctx_ixxc(void)
{
    SSL_library_init();
    OpenSSL_add_all_algorithms();
    SSL_load_error_strings();
    SSL_CTX *ctx = SSL_CTX_new(TLS_server_method());
    if (!ctx) {
        log_ssl_errors("SSL_CTX_new");
        return NULL;
    }
    SSL_CTX_set_min_proto_version(ctx, TLS1_3_VERSION);
    SSL_CTX_set_options(ctx,
        SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 |
        SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1 |
        SSL_OP_NO_COMPRESSION | SSL_OP_NO_RENEGOTIATION |
        SSL_OP_CIPHER_SERVER_PREFERENCE);
    SSL_CTX_set_ciphersuites(ctx, "TLS_AES_256_GCM_SHA384:TLS_CHACHA20_POLY1305_SHA256");
    if (SSL_CTX_use_certificate_chain_file(ctx, CERT_PATH) <= 0 || SSL_CTX_use_PrivateKey_file(ctx, KEY_PATH, SSL_FILETYPE_PEM) <= 0) {
        log_ssl_errors("Certificate/Key load");
        SSL_CTX_free(ctx);
        return NULL;
    }
    return ctx;
}
#endif
