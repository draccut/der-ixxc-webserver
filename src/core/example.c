#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

#define EXAMPLE_INDEX_HTML \
    "<!DOCTYPE html>\n" \
    "<html lang=\"en\">\n" \
    "<head>\n" \
    "<meta charset=\"UTF-8\">\n" \
    "<title>IXXC Web Server Example</title>\n" \
    "<link rel=\"stylesheet\" href=\"style.css\">\n" \
    "</head>\n" \
    "<body>\n" \
    "<h1>Welcome to IXXC Web Server Example</h1>\n" \
    "<p>This is the index page served directly from embedded C code.</p><a href=\"crawling/index.html\">files includes by crawling</a>\n" \
    "<script src=\"script.js\"></script>\n" \
    "</body>\n" \
    "</html>\n"

#define EXAMPLE_STYLE_CSS \
    "body {\n" \
    "  font-family: sans-serif;\n" \
    "  background: #f5f5f5;\n" \
    "  margin: 0;\n" \
    "  padding: 2em;\n" \
    "}\n" \
    "h1 {\n" \
    "  color: #2c3e50;\n" \
    "}\n" \
    "p {\n" \
    "  color: #444;\n" \
    "}\n"

#define EXAMPLE_SCRIPT_JS \
    "document.addEventListener('DOMContentLoaded', function() {\n" \
    "  console.log('IXXC Web Server Example loaded!');\n" \
    "});\n"

#define CRAWLING_INDEX_HTML \
    "<!DOCTYPE html>\n" \
    "<html lang=\"en\">\n" \
    "<head>\n" \
    "<meta charset=\"UTF-8\">\n" \
    "<title>Crawling Section</title>\n" \
    "<link rel=\"stylesheet\" href=\"crawling/style.css\">\n" \
    "</head>\n" \
    "<body>\n" \
    "<h1>Crawling Example</h1>\n" \
    "<p>This is the crawling section index page.</p>\n" \
    "</body>\n" \
    "</html>\n"

#define CRAWLING_STYLE_CSS \
    "body {\n" \
    "  font-family: monospace;\n" \
    "  background: #222;\n" \
    "  color: #eee;\n" \
    "  margin: 0;\n" \
    "  padding: 2em;\n" \
    "}\n" \
    "h1 {\n" \
    "  color: #bada55;\n" \
    "}\n" \
    "p {\n" \
    "  color: #bbb;\n" \
    "}\n"

#define OUTPUT_PATH_EXAMPLE "build/example"
#define OUTPUT_PATH_EXAMPLE_CRAWLING "build/example/crawling"
#define OUTPUT_PATH_ENDPOINT_FILE OUTPUT_PATH_EXAMPLE "/endpoints.h"

#define ENDPOINT_FILE \
    "{\"build/example/index.html\",          \"GET /\",                                                                                  0}," "\n" \
    "{\"build/example/style.css\",           \"GET /style.css\",                                                                         0}," "\n" \
    "{\"build/example/script.js\",           \"GET /script.js\",                                                                         0}," "\n"

void write_file(const char *path, const char *content);

void write_file(const char *path, const char *content)
{
    FILE *f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "Failed to open %s for writing\n", path);
        exit(EXIT_FAILURE);
    }
    fputs(content, f);
    fclose(f);
}

int main(void) {
    write_file(OUTPUT_PATH_EXAMPLE "/index.html", EXAMPLE_INDEX_HTML);
    write_file(OUTPUT_PATH_EXAMPLE "/style.css", EXAMPLE_STYLE_CSS);
    write_file(OUTPUT_PATH_EXAMPLE "/script.js", EXAMPLE_SCRIPT_JS);
    write_file(OUTPUT_PATH_EXAMPLE_CRAWLING "/index.html", CRAWLING_INDEX_HTML);
    write_file(OUTPUT_PATH_EXAMPLE_CRAWLING "/style.css", CRAWLING_STYLE_CSS);
    write_file(OUTPUT_PATH_ENDPOINT_FILE, ENDPOINT_FILE);
    return 0;
}
