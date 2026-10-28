#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "../../header/prod/meme_types_handler.h"

#define BUFFER_CAP 0x1000

static const char *out_h = "build/generated/html_data.h";
static const char *out_c = "build/generated/html_data.c";
static const char *out_h_header  = "build/generated/header_data.h";
static const char *out_c_header  = "build/generated/header_data.c";

/**
 * header_data.h
 */
static void write_header_data_h(const char *path,
                                const size_t lengths[],
                                size_t total)
{
    FILE *h = fopen(path, "w");
    if (!h) { perror("fopen header_data.h"); exit(EXIT_FAILURE); }

    fprintf(h, "/* %s - auto-generated; do not edit */\n", path);
    fprintf(h, "#ifndef HEADER_DATA_H\n#define HEADER_DATA_H\n\n");
    fprintf(h, "#include <stddef.h>\n#include <stdint.h>\n\n");
    fprintf(h, "#define HEADER_NR_OF_ENDPOINTS %zu\n", (size_t)IXXC_ENDPOINTS_COUNT);
    fprintf(h, "#define HEADER_TOTAL_BYTES %zu\n", total);

    for (size_t i = 0; i < (size_t)IXXC_ENDPOINTS_COUNT; ++i)
        fprintf(h, "#define HEADER_LENGTH_%zu %zu\n", i, lengths[i]);

    fprintf(h, "\n");
    fprintf(h, "#include <assert.h>\n");
    fprintf(h, "static_assert(HEADER_TOTAL_BYTES == (");
    for (size_t i = 0; i < (size_t)IXXC_ENDPOINTS_COUNT; ++i) {
        fprintf(h, "HEADER_LENGTH_%zu", i);
        if (i + 1 < (size_t)IXXC_ENDPOINTS_COUNT) fprintf(h, " + ");
    }
    fprintf(h, "), \"header_data: HEADER_TOTAL_BYTES != sum(HEADER_LENGTH_*)\");\n\n");

    fprintf(h, "extern const char   header_data[];\n");
    fprintf(h, "extern const size_t header_starts[%zu];\n", (size_t)IXXC_ENDPOINTS_COUNT);
    fprintf(h, "extern const size_t header_lengths[%zu];\n", (size_t)IXXC_ENDPOINTS_COUNT);
    fprintf(h, "extern const size_t header_total_bytes;\n\n");

    fprintf(h,
            "void get_header_and_meme(char **http_header,\n"
            "                         size_t *http_header_length,\n"
            "                         size_t index_endpoint,\n"
            "                         size_t archived_file_size);\n\n");
    fprintf(h, "#endif /* HEADER_DATA_H */\n");
    fclose(h);
}

/**
 * header_data.c  -  Bytes + starts/lengths + rodata-Accessor
 * Speichert die Header ohne erzwungenes NUL; Länge steht in header_lengths.
 * Wenn du %-s brauchst: beim Generieren extra '\\0' anhängen und length+1.
 */
static void write_header_data_c(const char *path,
                                char **headers,
                                const size_t lengths[],
                                const size_t starts[],
                                size_t total)
{
    (void)total;
    FILE *c = fopen(path, "w");
    if (!c) { perror("fopen header_data.c"); exit(EXIT_FAILURE); }

    fprintf(c, "/* %s - auto-generated; do not edit */\n", path);
    fprintf(c, "#include \"header_data.h\"\n\n");

    fprintf(c, "const char header_data[] = {\n    ");
    size_t global_count = 0;
    for (size_t i = 0; i < (size_t)IXXC_ENDPOINTS_COUNT; ++i) {
        const unsigned char *p = (const unsigned char *)headers[i];
        size_t n = lengths[i];
        for (size_t j = 0; j < n; ++j) {
            fprintf(c, "0x%02X, ", p[j]);
            global_count++;
            if (global_count % 12 == 0) fprintf(c, "\n    ");
        }
    }
    fprintf(c, "\n};\n\n");

    fprintf(c, "const size_t header_starts[%zu] = { ", (size_t)IXXC_ENDPOINTS_COUNT);
    for (size_t i = 0; i < (size_t)IXXC_ENDPOINTS_COUNT; ++i) {
        fprintf(c, "%zu", starts[i]);
        if (i + 1 < (size_t)IXXC_ENDPOINTS_COUNT) fprintf(c, ", ");
    }
    fprintf(c, " };\n");

    fprintf(c, "const size_t header_lengths[%zu] = { ", (size_t)IXXC_ENDPOINTS_COUNT);
    for (size_t i = 0; i < (size_t)IXXC_ENDPOINTS_COUNT; ++i) {
        fprintf(c, "%zu", lengths[i]);
        if (i + 1 < (size_t)IXXC_ENDPOINTS_COUNT) fprintf(c, ", ");
    }
    fprintf(c, " };\n\n");
    fprintf(c, "const size_t header_total_bytes = (size_t)HEADER_TOTAL_BYTES;\n\n");
    fprintf(c,
            "void get_header_and_meme(char **http_header,\n"
            "                         size_t *http_header_length,\n"
            "                         size_t index_endpoint,\n"
            "                         size_t archived_file_size)\n"
            "{\n"
            "    (void)archived_file_size;\n"
            "    if (!http_header || !http_header_length)\n"
            "        return;\n"
            "    if (index_endpoint >= (size_t)HEADER_NR_OF_ENDPOINTS) {\n"
            "        *http_header = NULL;\n"
            "        *http_header_length = 0;\n"
            "        return;\n"
            "    }\n"
            "    *http_header = (char *)&header_data[header_starts[index_endpoint]];\n"
            "    *http_header_length = header_lengths[index_endpoint];\n"
            "}\n");

    fclose(c);
}

/**
 * Dateigröße ermitteln für Längen und Start *ptr + offset
 */
static size_t file_len(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return (size_t)-1;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return (size_t)-1; }
    long l = ftell(f);
    fclose(f);
    if (l < 0) return (size_t)-1;
    return (size_t)l;
}

/**
 * Header: Deklarationen + Präprozessor-Defines + _Static_assert
 * Summe HTML_LENGTH_* == HTML_TOTAL_BYTES zur Compile-Zeit.
 */
static void write_header_file(const char *path, size_t lengths[], size_t total)
{
    FILE *h = fopen(path, "w");
    if (!h) {
        perror("fopen header"); exit(EXIT_FAILURE);
    }
    fprintf(h, "/* %s - declarations and compile-time check (auto-generated) */\n", path);
    fprintf(h, "#ifndef HTML_DATA_H\n#define HTML_DATA_H\n\n");
    fprintf(h, "#include <stddef.h>\n\n");
    fprintf(h, "/* sizes generated by generator */\n");
    fprintf(h, "#define NR_OF_ENDPOINTS %zu\n", IXXC_ENDPOINTS_COUNT);
    fprintf(h, "#define HTML_TOTAL_BYTES %zu\n", total);
    for (size_t i = 0; i < IXXC_ENDPOINTS_COUNT; ++i) {
        fprintf(h, "#define HTML_LENGTH_%zu %zu\n", i, lengths[i]);
    }
    fprintf(h, "\n");
    fprintf(h, "/* Compile-time sanity check: sum of lengths must equal total bytes */\n");
    fprintf(h, "_Static_assert(HTML_TOTAL_BYTES == (");
    for (size_t i = 0; i < IXXC_ENDPOINTS_COUNT; ++i) {
        fprintf(h, "HTML_LENGTH_%zu", i);
        if (i + 1 < IXXC_ENDPOINTS_COUNT) fprintf(h, " + ");
    }
    fprintf(h, "), \"html_data: HTML_TOTAL_BYTES does not equal sum of HTML_LENGTH_* values\");\n\n");
    fprintf(h, "extern const unsigned char html_data[];\n");
    fprintf(h, "extern const size_t html_starts[%zu];\n", IXXC_ENDPOINTS_COUNT);
    fprintf(h, "extern const size_t html_lengths[%zu];\n", IXXC_ENDPOINTS_COUNT);
    fprintf(h, "extern const size_t html_total_bytes;\n\n");
    fprintf(h, "void getSizeHtmlByIndex(size_t *html_len);\n");
    fprintf(h, "void getStartOffsetByIndex(size_t *start_offset);\n");
    fprintf(h, "void getHtml(size_t start, size_t end, char *resbuf);\n\n");
    fprintf(h, "#endif /* HTML_DATA_H */\n");
    fclose(h);
}

/**
 * C-File: konkateniertes html_data[]-Byte-Array + starts/lengths + Accessor-Implementierungen.
 */
static void write_c_file(const char *path, size_t starts[], size_t lengths[], size_t total)
{
    (void)total;
    FILE *c = fopen(path, "w");
    if (!c) {
        perror("fopen c");
        exit(EXIT_FAILURE);
    }
    fprintf(c, "/* %s - data + implementations (auto-generated) */\n", path);
    fprintf(c, "#include \"html_data.h\"\n");
    fprintf(c, "#include <string.h>\n\n");
    fprintf(c, "const unsigned char html_data[] = {\n    ");
    unsigned char buf[BUFFER_CAP];
    size_t global_count = 0;
    for (size_t i = 0; i < IXXC_ENDPOINTS_COUNT; ++i) {
        FILE *in = fopen(IXXC_ENDPOINTS[i].source_file_path, "rb");
        if (!in) { perror("fopen input"); fclose(c); exit(EXIT_FAILURE); }
        size_t r;
        while ((r = fread(buf, 1, BUFFER_CAP, in)) > 0) {
            for (size_t j = 0; j < r; ++j) {
                fprintf(c, "0x%02X, ", buf[j]);
                global_count++;
                if (global_count % 12 == 0) fprintf(c, "\n    ");
            }
        }
        fclose(in);
    }
    fprintf(c, "\n};\n\n");
    fprintf(c, "const size_t html_starts[%zu] = { ", IXXC_ENDPOINTS_COUNT);
    for (size_t i = 0; i < IXXC_ENDPOINTS_COUNT; ++i) {
        fprintf(c, "%zu", starts[i]);
        if (i + 1 < IXXC_ENDPOINTS_COUNT) fprintf(c, ", ");
    }
    fprintf(c, " };\n");
    fprintf(c, "const size_t html_lengths[%zu] = { ", IXXC_ENDPOINTS_COUNT);
    for (size_t i = 0; i < IXXC_ENDPOINTS_COUNT; ++i) {
        fprintf(c, "%zu", lengths[i]);
        if (i + 1 < IXXC_ENDPOINTS_COUNT) fprintf(c, ", ");
    }
    fprintf(c, " };\n\n");
    fprintf(c, "const size_t html_total_bytes = (size_t)HTML_TOTAL_BYTES;\n\n");
    fprintf(c,
            "void getSizeHtmlByIndex(size_t *html_len) {\n"
            "    for (int i = 0; i < %zu; ++i) html_len[i] = html_lengths[i];\n"
            "}\n\n", IXXC_ENDPOINTS_COUNT);
    fprintf(c,
            "void getStartOffsetByIndex(size_t *start_offset) {\n"
            "    for (int i = 0; i < %zu; ++i) start_offset[i] = html_starts[i];\n"
            "}\n\n", IXXC_ENDPOINTS_COUNT);
    fprintf(c,
            "void getHtml(size_t start, size_t end, char *resbuf) {\n"
            "    if (start >= html_total_bytes || end > html_total_bytes || end <= start) { resbuf[0] = '\\0'; return; }\n"
            "    size_t len = end - start;\n"
            "    memcpy(resbuf, &html_data[start], len);\n"
            "    resbuf[len] = '\\0';\n"
            "}\n");
    fclose(c);
}

int main(void)
{
    ixxc_init_endpoints();

    size_t html_lengths[IXXC_ENDPOINTS_COUNT];
    size_t html_starts[IXXC_ENDPOINTS_COUNT];
    size_t html_total = 0;

    char **header_buffer = calloc(IXXC_ENDPOINTS_COUNT, sizeof(char *));
    size_t *http_header_lengths = calloc(IXXC_ENDPOINTS_COUNT, sizeof(size_t));

    /* WICHTIG: zuerst allokieren */
    size_t *IXXC_Archieved_File_Sizes = calloc(IXXC_ENDPOINTS_COUNT, sizeof(size_t));

    if (!header_buffer || !http_header_lengths || !IXXC_Archieved_File_Sizes) {
        perror("calloc");
        return EXIT_FAILURE;
    }

    for (size_t i = 0; i < (size_t)IXXC_ENDPOINTS_COUNT; ++i) {
        size_t l = file_len(IXXC_ENDPOINTS[i].source_file_path);
        if (l == (size_t)-1) {
            fprintf(stderr, "Fehler: kann '%s' nicht lesen\n",
                    IXXC_ENDPOINTS[i].source_file_path);
            return EXIT_FAILURE;
        }
        IXXC_Archieved_File_Sizes[i] = l;   /* jetzt gültig */
        html_lengths[i] = l;
        html_starts[i]  = html_total;
        html_total     += l;
    }
    pub_detect_meme_type(header_buffer, http_header_lengths, IXXC_Archieved_File_Sizes);
    size_t hdr_starts[IXXC_ENDPOINTS_COUNT];
    size_t hdr_total = 0;

    for (size_t i = 0; i < (size_t)IXXC_ENDPOINTS_COUNT; ++i) {
        if (!header_buffer[i]) {
            fprintf(stderr, "Header %zu fehlt\n", i);
            return EXIT_FAILURE;
        }
        /* phlen kommt aus snprintf; Fallback nur zur Sicherheit */
        if (http_header_lengths[i] == 0)
            http_header_lengths[i] = strlen(header_buffer[i]);

        hdr_starts[i] = hdr_total;
        hdr_total    += http_header_lengths[i];
    }

    for (size_t i = 0; i < (size_t)IXXC_ENDPOINTS_COUNT; ++i) {
        printf("=== header[%zu] (%zu bytes) ===\n%.*s\n\n",
               i, http_header_lengths[i],
               (int)http_header_lengths[i], header_buffer[i]);
    }

    write_header_data_h(out_h_header, http_header_lengths, hdr_total);
    write_header_data_c(out_c_header, header_buffer,
                        http_header_lengths, hdr_starts, hdr_total);
    write_header_file(out_h, html_lengths, html_total);
    write_c_file(out_c, html_starts, html_lengths, html_total);

    printf("Erzeugt: %s / %s (HEADER_TOTAL_BYTES=%zu)\n",
           out_h_header, out_c_header, hdr_total);

    for (size_t i = 0; i < (size_t)IXXC_ENDPOINTS_COUNT; ++i)
        free(header_buffer[i]);
    free(header_buffer);
    free(http_header_lengths);
    free(IXXC_Archieved_File_Sizes);
    IXXC_Archieved_File_Sizes = NULL;

    ixxc_cleanup_endpoints();
    return EXIT_SUCCESS;
}
