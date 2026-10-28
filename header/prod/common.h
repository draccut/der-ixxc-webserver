#pragma once

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
#include <stddef.h>

#include "../../ixxc_endpoints.h"
#include "../../build/generated/html_data.h"
#include "../../build/generated/header_data.h"

#define USE_HTTP
//#define USE_PRODUCTION_MODE
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
#define KEY_PATH  "/etc/letsencrypt/live/ixxc.de/privkey.pem"

#ifndef USE_PRODUCTION_MODE
    #define LOG_FILE_MACRO "my_server_access.log"
#else
    #define LOG_FILE_MACRO "/var/log/ixxc_server.log"
#endif

#define DROP_USER              "ixxc_server"
#define MAX_READ               0xFFF
#define WORKERS                0x4
#define RING_BUFFER_SIZE       0x100
#define MAX_LOG_LINE           0x2000
#define MAX_HTML               (8UL << 20)
#define MAX_LOG_SIZE           (5000000000ULL)

#if (RING_BUFFER_SIZE & (RING_BUFFER_SIZE - 1)) != 0
    #error "RING_BUFFER_SIZE must be a power of two"
#endif
#if RING_BUFFER_SIZE < 2
    #error "RING_BUFFER_SIZE must be at least 2"
#endif

typedef struct {
    char log_buffer[RING_BUFFER_SIZE][MAX_LOG_LINE];
    _Atomic unsigned int write_index;
    _Atomic unsigned int read_index;
} shared_log_t;

extern shared_log_t *shared_log;
extern volatile sig_atomic_t shutdown_requested;
extern volatile sig_atomic_t shutdown_requested_logger;
extern pid_t worker_pids[WORKERS];
extern char  *attr_html_mem[];
extern size_t attr_html_len[];

static const char * const LOG_FILE = LOG_FILE_MACRO;

