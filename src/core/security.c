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

#ifndef USE_ASSEMBLY_CODE

/*
 * Es überschreibt den Buffer zuverlässig aber inkrementiert immer nur um 8 Bit,
 * obwohl der Datenbus 64Bit pro mov haben könnte. Aber funktioniert erstmal.
 */
__attribute__((noinline, noclone, optimize("O0"), used))
void assembly_zero_buf(void *buf, size_t len)
{
    volatile char *vol_buf_ptr = (volatile char *)buf;
    for (size_t vol_buf_ptr_index = 0; vol_buf_ptr_index < len; ++vol_buf_ptr_index) {
        vol_buf_ptr[vol_buf_ptr_index] = '\0';
    }
}

#else

/*
 * Nullt einen Puffer per reinem Inline-Assembler.
 *
 * Absicherung gegen Veränderung Optimierung die Zeitkonstants sowie clearing
 * wegoptimieren können. Zudem soll nicht mit void* genullt werden um die follgen
 * 64Bit eines mov ausnutzen zu können.
 *
 * Register-Ownership und Wiederherstellung der C-Werte:
 *   Die Input-Operanden (%[buffer], %[length]) werden vom Compiler in Register
 *   gelegt.
 *
 *   rdi = Base-Pointer
 *   rsi = Länge
 *
 * Der Compiler sieht die Clobber-Liste(Register die genutzt werden) und emittiert
 * vor dem asm-Block die benötigten push Befehle um die Werte aus C auf den Stack zu pushen.
 * Dann sind die Register frei und können intern verwendet werden.
 * Eine konvenzion besagt in welcher reihenfolge die ursprünglichen werte mit POP vom stack
 * wieder in die Register gelangen.
 */
__attribute__((noinline, noclone, optimize("O0"), used))
void assembly_zero_buf(void *buf, size_t len)
{
    if (len == 0) return;
    asm volatile(
        "xorq   %%rax, %%rax\n\t"                   /* rax = 0                          */
        "xorq   %%rdi, %%rdi\n\t"                   /* rdi wird Base-Pointer            */
        "xorq   %%rsi, %%rsi\n\t"                   /* rsi wird Länge                   */
        "xorq   %%rcx, %%rcx\n\t"                   /* rcx = qwords-Zähler              */
        "xorq   %%rdx, %%rdx\n\t"
        "xorq   %%r8,  %%r8\n\t"                    /* r8  = geretteter Überhang        */
        "xorq   %%r9,  %%r9\n\t"                    /* r9  = Byte-Schleifen-Pointer     */
        "movq   %[buffer], %%rdi\n\t"               /* rdi = base bleibt gleich         */
        "movq   %[length], %%rsi\n\t"               /* rsi = original length            */
        "movq   %%rsi, %%rdx\n\t"
        "andq   $7, %%rdx\n\t"                      /* rdx = len & 7  (= % 8)           */
        "movq   %%rdx, %%r8\n\t"                    /* r8  = Überhang rest aus len % 8  */
        "shrq   $3, %%rsi\n\t"                      /* rsi = len / 8   (qwords)         */
        "testq  %%r8, %%r8\n\t"                     /* alles 64Bit Blöcke?              */
        "jz     .Lmain_zero%=\n\t"                  /* kein Überhang: jmp weiter        */
                                                    /* Überhang byte-für-byte von       */
                                                    /* niedriger zu hoher Adresse       */
        "movq   %%rdi, %%r9\n\t"                    /* r9 = laufende Low-Adresse zu buf */
        ".Lbyte_loop%=:\n\t"
        "movb   %%al, (%%r9)\n\t"                   /* *p = 0 */
        "incq   %%r9\n\t"
        "decq   %%rdx\n\t"                          /* rdx diente als Counter Kopie     */
        "jnz    .Lbyte_loop%=\n\t"

        /* ============================================================================ */
        /* Hauptteil: von hoher Adresse Richtung Base mit 8-Byte-Stores */
        /* ============================================================================ */
        ".Lmain_zero%=:\n\t"
        "movq   %%rsi, %%rcx\n\t"                   /* rcx = qwords Schleifenzähler     */
        "shlq   $3, %%rsi\n\t"                      /* rsi = qwords * 8                 */
        "addq   %%r8, %%rsi\n\t"                    /* + Überhang = original len        */
        "addq   %%rdi, %%rsi\n\t"                   /* rsi = base + len  (high address) */
        "testq  %%rcx, %%rcx\n\t"                   /* keine qwords dann fertig         */
        "jz     .Ldone%=\n\t"
        "leaq   (%%rdi, %%r8), %%rdx\n\t"
        ".Lqword_loop%=:\n\t"
        "subq   $8, %%rsi\n\t"                      /* eine 8-Byte-Slot nach unten      */
        "movq   %%rax, (%%rsi)\n\t"                 /* ganzes genulltes Register: write */
        "cmpq   %%rdx, %%rsi\n\t"                   /* pointer == Stop if base+rem      */
        "jne    .Lqword_loop%=\n\t"
        ".Ldone%=:\n\t"
        /* ============================================================================ */
        /* Liste der Register die der C-Compiler auf dem Stack zwischenspeicherte       */
        /* ============================================================================ */
        :
        : [buffer] "r" (buf),
                 [length] "r" (len)
                 : "rax", "rcx", "rdx", "rsi", "rdi", "r8", "r9",
                 "memory", "cc"
    );
}

#endif
