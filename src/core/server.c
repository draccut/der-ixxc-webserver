#include "../../header/prod/common.h"
#include "../../header/prod/log.h"
#include "../../header/prod/process.h"
#include "../../header/prod/socket.h"
#include "../../header/prod/connection.h"
#include "../../header/prod/meme_types_handler.h"

int main(void)
{
    ixxc_init_endpoints();
    if (NR_OF_ENDPOINTS != IXXC_ENDPOINTS_COUNT) {
        fprintf(stderr, "NR_OF_ENDPOINTS != IXXC_ENDPOINTS_COUNT\n");
        return 1;
    }
    if (init_html_attr() != 0) {
        fprintf(stderr, "init_html_attr failed\n");
        return 1;
    }
    #if USE_SSL
    SSL_CTX *ctx = create_ssl_ctx_ixxc();
    if (!ctx) {
        ixxc_cleanup_endpoints();
        return 1;
    }
    #endif
    int sfd = create_listening_socket_ixxc();
    if (sfd < 0) {
        #if USE_SSL
        SSL_CTX_free(ctx);
        #endif
        ixxc_cleanup_endpoints();
        return 1;
    }
    printf("Server gestartet %s:%d (%s)\n",
           USE_SSL ? "https://ixxc.de" : "http://127.0.0.1",
           PORT,
           USE_SSL ? "HTTPS" : "HTTP");
    if (prepare_logging_subsystem_ixxc() != 0) {
        fprintf(stderr, "logging subsystem init failed\n");
        close(sfd);
        #if USE_SSL
        SSL_CTX_free(ctx);
        #endif
        ixxc_cleanup_endpoints();
        return 1;
    }
    pid_t logger_pid = bootstrap_children_ixxc(sfd
    #if USE_SSL
    , ctx
    #endif
    );
    int rc = parent_supervise_and_wait_ixxc(sfd, logger_pid
    #if USE_SSL
    , ctx
    #endif
    );
    #if USE_SSL
    SSL_CTX_free(ctx);
    #endif
    return rc;
}
