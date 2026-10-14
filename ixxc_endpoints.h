#ifndef IXXC_ENDPOINTS_H
#define IXXC_ENDPOINTS_H

#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>

/**
 * Dies ist ein Webserver und so etwas wie ein Tar-Archiv in einem.
 * Kompiliert läuft es mit dem Frontend im .rodata-Segment auf jedem
 * Linux-x86-64-Kernel/CPU-Architektur.
 *
 * source_file_path:  Datei, die in das Archiv kompiliert wird
 * endpoint:          Der Request (z. B. GET /) liefert die Datei aus,
 *                    die in dieser Zeile in das Binary kompiliert wurde.
 */
#pragma pack(push, 1)
typedef struct {
    char source_file_path[0xC00];
    char endpoint[0x1000];
    size_t len_endpoint;
} __attribute__((aligned(0x2000))) IXXC_Endpoint;
#pragma pack(pop)

#ifdef _USE_TEST_ENDPOINTS
static IXXC_Endpoint IXXC_ENDPOINTS[] = {
    #include "test_endpoints.h"
};
#else
static IXXC_Endpoint IXXC_ENDPOINTS[] = {
    #include "../frontends/endpoint_list.h"
};
#endif

static const size_t IXXC_ENDPOINTS_COUNT = sizeof(IXXC_ENDPOINTS) / sizeof(IXXC_ENDPOINTS[0]);
static const size_t MASKU_64BITXORGATTER = 0x200;
static const size_t CCC_CMP_MASK_LEN = 0x1000;
static char **orig_strings = NULL;

/**
 * Um Endpunkte nach außen hin verstecken zu können, dürfen keine Timing-Angriffe
 * möglich sein. Deshalb werden die Endpunkte (also die Keys
 * auf dem Server, um an die Daten des Archivs zu kommen) der Länge nach sortiert.
 * Später wird der Vergleich mit logischem XOR auf rohen Bytes durchgeführt.
 */
static inline int sort_desc_by_offset(void *bufToSort, size_t nrElements, size_t elementSize, size_t keyOffset) 
{
    if (bufToSort == NULL || nrElements <= 1 || elementSize == 0) {
        return -1;
    }
    if (keyOffset + sizeof(uint64_t) > elementSize) {
        return -1;
    }
    unsigned char *base = (unsigned char *)bufToSort;
    void *tmp = malloc(elementSize);
    if (tmp == NULL) {
        perror("malloc tmp");
        return -2;
    }
    for (size_t i = 0; i < nrElements - 1; ++i) {
        size_t maxIndex = i;
        uint64_t maxKey;
        {
            uint64_t k;
            memcpy(&k, base + i * elementSize + keyOffset, sizeof(k));
            maxKey = k;
        }
        for (size_t j = i + 1; j < nrElements; ++j) {
            uint64_t k;
            memcpy(&k, base + j * elementSize + keyOffset, sizeof(k));
            if (k > maxKey) {
                maxKey = k;
                maxIndex = j;
            }
        }
        if (maxIndex != i) {
            unsigned char *a = base + i * elementSize;
            unsigned char *b = base + maxIndex * elementSize;
            memcpy(tmp, a, elementSize);
            memcpy(a, b, elementSize);
            memcpy(b, tmp, elementSize);
        }
    }
    free(tmp);
    return 0;
}

/**
 * Auch in C sollte ein klares Ownership-Modell eingehalten werden.
 * Dadurch werden bei Speichertests Memory Leaks sofort erkennbar,
 * ohne dass zeitaufwändig analysiert werden muss,
 * welcher Code für welche Speicherbereiche verantwortlich ist.
 */
static inline void ixxc_cleanup_endpoints()
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

/**
 * Die Ausgabe verdeutlicht, dass die Endpunkte korrekt sortiert wurden und somit
 * kein fehlerhaftes Routing entstehen kann, nur weil ein kürzeres Endpunkt-Element
 * vor einem längeren stand.
 */
static inline void print_endpoint_list() 
{
    printf("%s\n", "==============  = ENDPOINT LIST   ===================");
    for (size_t i = 0; i < IXXC_ENDPOINTS_COUNT; ++i) {
        printf("%zu: %s\n", i, IXXC_ENDPOINTS[i].endpoint);
    }
    printf("%s\n", "=====================================================");
}

/**
 * Hier wird dafür gesorgt, dass jeder Endpunkt die gleiche Buffergröße hat,
 * damit später der Dispatcher für jeden Endpunkt die gleiche Anzahl an
 * Maschinenzyklen benötigt und man Inhalte im Archiv-Webserver gegenüber dem
 * Internet verstecken kann.
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

/**
 * MASKU_64BITXORGATTER ist die Anzahl der uint64_t = 64-Bit = 8-Byte-Blöcke.
 * CCC_CMP_MASK_LEN     ist der Wert in Bytes und nicht in 64-Bit-Blöcken.
 * Diese Werte sind wichtig für den zeitgleichen Dispatcher in der Server-Datei.
 */
static inline void ixxc_init_endpoints() 
{
    for (size_t i = 0; i < IXXC_ENDPOINTS_COUNT; ++i) {
        if (IXXC_ENDPOINTS[i].len_endpoint == 0) {
            IXXC_ENDPOINTS[i].len_endpoint = strlen(IXXC_ENDPOINTS[i].endpoint);
        }
    }
    if (sort_desc_by_offset((void *) IXXC_ENDPOINTS, IXXC_ENDPOINTS_COUNT, sizeof(IXXC_ENDPOINTS[0]), 0x1C00) != 0) printf("%s\n", "ERROR: cannot sort");
    print_endpoint_list();
    scale_up_endpoint_buffer();
}

#endif
