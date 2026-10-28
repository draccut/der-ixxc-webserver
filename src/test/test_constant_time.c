#define _POSIX_C_SOURCE 199309L

#include <stddef.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>

#include "../../ixxc_endpoints.h"
#include "../../header/prod/security.h"

uint64_t get_time_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

/**
 * ixxc_init_endpoints();          sortiert + ruft scale_up_endpoint_buffer() auf
 */
int main(void)
{
    printf("Initialisiere Endpunkte ...\n");
    ixxc_init_endpoints();
    printf("\nStarte Zeitmessung: 10.000 Vergleiche für jedes Endpunkt-Paar.\n");
    const int N_RUNS = 10000;
    printf("IXXC_ENDPOINTS_COUNT: %zu\n", IXXC_ENDPOINTS_COUNT);
    printf("CCC_CMP_MASK_LEN:     %zu\n", CCC_CMP_MASK_LEN);
    double times[IXXC_ENDPOINTS_COUNT][IXXC_ENDPOINTS_COUNT];
    int    result[IXXC_ENDPOINTS_COUNT][IXXC_ENDPOINTS_COUNT];
    memset(times,  0, sizeof(times));
    memset(result, 0, sizeof(result));
    for (size_t r = 0; r < N_RUNS; ++r) {
        for (size_t i = 0; i < IXXC_ENDPOINTS_COUNT; ++i) {
            for (size_t j = 0; j < IXXC_ENDPOINTS_COUNT; ++j) {
                const char *input      = orig_strings[j];
                size_t      input_len  = strlen(orig_strings[j]);
                const char *compare_to = IXXC_ENDPOINTS[i].endpoint;
                uint64_t start = get_time_ns();
                volatile int res = constant_time_compare(input, input_len, compare_to);
                (void)res;
                uint64_t end = get_time_ns();
                times[i][j] += (end - start);
                result[i][j] = (i == j);
            }
        }
    }
    for (size_t i = 0; i < IXXC_ENDPOINTS_COUNT; ++i) {
        for (size_t j = 0; j < IXXC_ENDPOINTS_COUNT; ++j) {
            times[i][j] /= N_RUNS;
        }
    }
    double min_avg = times[0][0];
    double max_avg = times[0][0];
    int    min_res = result[0][0];
    int    max_res = result[0][0];
    for (size_t i = 0; i < IXXC_ENDPOINTS_COUNT; ++i) {
        for (size_t j = 0; j < IXXC_ENDPOINTS_COUNT; ++j) {
            if (times[i][j] < min_avg) {
                min_avg = times[i][j];
                min_res = result[i][j];
            }
            if (times[i][j] > max_avg) {
                max_avg = times[i][j];
                max_res = result[i][j];
            }
        }
    }
    printf("min result: %d  value %.2f ns\n", min_res, min_avg);
    printf("max result: %d  value %.2f ns\n", max_res, max_avg);
    return 0;
}
