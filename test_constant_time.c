#include <stddef.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>

#include "ixxc_endpoints.h"

#if defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__)
#define IS_BIG_ENDIAN (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
#elif defined(__BIG_ENDIAN__) || defined(_BIG_ENDIAN)
#define IS_BIG_ENDIAN 1
#else
#define IS_BIG_ENDIAN 0
#endif

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
