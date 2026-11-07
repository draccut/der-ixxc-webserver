#include "../../header/prod/common.h"
#include "../../header/prod/security.h"

#ifndef MASKU_64BITXORGATTER
#define MASKU_64BITXORGATTER (0x1000 / sizeof(uint64_t))
#endif

#if defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__)
#define IS_BIG_ENDIAN (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
#elif defined(__BIG_ENDIAN__) || defined(_BIG_ENDIAN)
#define IS_BIG_ENDIAN 1
#else
#define IS_BIG_ENDIAN 0
#endif

/*
 * Performs a constant-time comparison protection against timing attacks
 *
 * The input is padded to a full 4-KiB page and then bitwise compared with the
 * reference value also 4 KiB
 *
 * @param input       Arbitrary string can be shorter than 0x1000 bytes
 * @param input_len   Length of input
 * @param compare_to  Must always be exactly 0x1000 bytes in size
 * @return            0 if equal, 1 if not equal
 */
__attribute__((noinline, optimize("O0")))
int constant_time_compare(const char *input, size_t input_len, const char *compare_to)
{
    if (!input || input_len > 0xFFF) {
        input_len = 0;
    }
    char input_page[0x1000];
    assembly_zero_buf(input_page, sizeof(input_page));
    if (input_len > 0) {
        memcpy(input_page, input, input_len);
    }
    volatile uint64_t diff = 0;
    const uint64_t *a = (const uint64_t *)input_page;
    const uint64_t *b = (const uint64_t *)compare_to;
    for (size_t i = 0; i < MASKU_64BITXORGATTER; ++i) {
        volatile uint64_t va = a[i];
        volatile uint64_t vb = b[i];
        #if !IS_BIG_ENDIAN
        va = __builtin_bswap64(va);
        vb = __builtin_bswap64(vb);
        #endif
        diff |= (va ^ vb);
    }
    __asm__ __volatile__("" : : "r"(diff) : "memory");
    return (diff != 0);
}

/*
 * Reliably zeroes the buffer
 */
__attribute__((noinline, noclone, optimize("O0"), used))
void assembly_zero_buf(void *buf, size_t len)
{
    volatile char *vol_buf_ptr = (volatile char *)buf;
    for (size_t vol_buf_ptr_index = 0; vol_buf_ptr_index < len; ++vol_buf_ptr_index) {
        vol_buf_ptr[vol_buf_ptr_index] = '\0';
    }
}
