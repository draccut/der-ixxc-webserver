#pragma once
#include "common.h"

int create_listening_socket_ixxc(void);

#if USE_SSL
SSL_CTX *create_ssl_ctx_ixxc(void);
#endif

