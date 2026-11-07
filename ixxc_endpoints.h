#ifndef IXXC_ENDPOINTS_H
#define IXXC_ENDPOINTS_H

#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>

/*
 * This is a web server and something like a tar archive in one.
 * When compiled, it runs with the frontend in the .rodata segment on every
 * Linux-x86-64 kernel/CPU architecture.
 *
 * source_file_path:  File that is compiled into the archive
 * endpoint:          The request (e.g. GET /) serves the file
 *                    that was compiled into the binary on this line.
 */
#pragma pack(push, 1)
typedef struct {
    char source_file_path[0xC00];
    char endpoint[0x1000];
    size_t len_endpoint;
} __attribute__((aligned(0x2000))) IXXC_Endpoint;
#pragma pack(pop)

#ifdef USE_TEST_ENDPOINTS
static IXXC_Endpoint IXXC_ENDPOINTS[] = {
    #include "header/test/test_endpoints.h"
};
#else
static IXXC_Endpoint IXXC_ENDPOINTS[] = {
    #include "header/prod/endpoints.h"
};
#endif

static const size_t IXXC_ENDPOINTS_COUNT = sizeof(IXXC_ENDPOINTS) / sizeof(IXXC_ENDPOINTS[0]);
static const size_t MASKU_64BITXORGATTER = 0x200;
static const size_t CCC_CMP_MASK_LEN = 0x1000;
static char **orig_strings = NULL;

static inline void ixxc_cleanup_endpoints(void)
{
    if (orig_strings != NULL) {
        for (size_t i = 0; i < IXXC_ENDPOINTS_COUNT; ++i) {
            if (orig_strings[i] != NULL) {
                free(orig_strings[i]);
                orig_strings[i] = NULL;
            }
        }
        free(orig_strings);
        orig_strings = NULL;
    }
}

static inline void print_endpoint_list(void) 
{
    printf("%s\n", "==============  = ENDPOINT LIST   ===================");
    for (size_t i = 0; i < IXXC_ENDPOINTS_COUNT; ++i) {
        printf("%zu: %s\n", i, IXXC_ENDPOINTS[i].endpoint);
    }
    printf("%s\n", "=====================================================");
}

/*
 * This ensures that every endpoint has the same buffer size,
 * so that later the dispatcher needs the same number of
 * machine cycles for each endpoint and contents in the archive web server
 * can be hidden from the internet.
 */
static inline void scale_up_endpoint_buffer(void)
{
    orig_strings = malloc(IXXC_ENDPOINTS_COUNT * sizeof(char *));
    if (orig_strings == NULL) {
        perror("malloc orig_strings");
        exit(1);
    }
    for (size_t i = 0; i < IXXC_ENDPOINTS_COUNT; ++i) {
        size_t orig_len = IXXC_ENDPOINTS[i].len_endpoint;
        orig_strings[i] = malloc(orig_len + 1);
        if (orig_strings[i] == NULL) {
            perror("malloc orig_strings[i]");
            exit(1);
        }
        memcpy(orig_strings[i], IXXC_ENDPOINTS[i].endpoint, orig_len);
        orig_strings[i][orig_len] = '\0';
        char *padded = malloc(0x1000);
        if (padded == NULL) {
            perror("malloc padded");
            exit(1);
        }
        volatile char *vp = (volatile char *)padded;
        for (size_t j = 0; j < 0x1000; ++j) {
            vp[j] = '\0';
        }
        memcpy(padded, orig_strings[i], orig_len);
        memcpy(IXXC_ENDPOINTS[i].endpoint, padded, 0x1000);
        free(padded);
    }
}

/*
 * MASKU_64BITXORGATTER is the number of uint64_t = 64-bit = 8-byte blocks.
 * CCC_CMP_MASK_LEN     is the value in bytes and not in 64-bit blocks.
 * These values are important for the constant-time dispatcher in the server file.
 */
static inline void ixxc_init_endpoints(void) 
{
    for (size_t i = 0; i < IXXC_ENDPOINTS_COUNT; ++i) {
        if (IXXC_ENDPOINTS[i].len_endpoint == 0) {
            IXXC_ENDPOINTS[i].len_endpoint = strlen(IXXC_ENDPOINTS[i].endpoint);
        }
    }
    print_endpoint_list();
    scale_up_endpoint_buffer();
}

#endif
