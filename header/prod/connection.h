#pragma once

#include "common.h"

int init_html_attr(void);

void handle_connection_fd(int worker_index, int cfd, struct sockaddr_in *caddr
#if USE_SSL
, SSL_CTX *ctx
#endif
);

#if USE_SSL
void send_endpoint_ssl(SSL *ssl, size_t idx);
#else
void send_endpoint_plain(int fd, size_t idx);
#endif
