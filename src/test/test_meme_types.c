#include "../../src/core/meme_types_handler.c"

#include <assert.h>
#include <stdio.h>

/**
 * is_download_meme_type erkennt bekannte Endungen.
 */
static void test_is_download_meme_type(void)
{
    assert(is_download_meme_type(".pdf") == 1);
    assert(is_download_meme_type(".png") == 1);
    assert(is_download_meme_type(".docx") == 1);
    assert(is_download_meme_type(".odt") == 1);
    assert(is_download_meme_type(".odg") == 1);
    assert(is_download_meme_type(".html") == 0);
    assert(is_download_meme_type(".webp") == 0);
    assert(is_download_meme_type(NULL) == 0);
    puts("[OK] test_is_download_meme_type");
}

static void test_get_header_and_meme_forbidden(void)
{
    char *header = NULL;
    size_t hlen = 0;
    int rc = get_header_and_meme(&header, &hlen, IXXC_ENDPOINTS_COUNT, 0);
    assert(rc == 0);
    assert(header != NULL);
    assert(hlen > 0);
    assert(strstr(header, "403") != NULL);
    assert(strstr(header, "Forbidden") != NULL);
    free(header);
    puts("[OK] test_get_header_and_meme_forbidden");
}

/**
 * Test: NULL-Parameter werden abgewiesen.
 */
static void test_get_header_and_meme_null_args(void)
{
    char *header = NULL;
    size_t hlen = 0;
    assert(get_header_and_meme(NULL, &hlen, 0, 0) == -1);
    assert(get_header_and_meme(&header, NULL, 0, 0) == -1);
    puts("[OK] test_get_header_and_meme_null_args");
}

/**
 * Test: Header enthält Content-Length und erwarteten Status.
 */
static void test_header_content_length_format(void)
{
    char *header = NULL;
    size_t hlen = 0;
    if (IXXC_ENDPOINTS_COUNT == 0) {
        puts("[SKIP] test_header_content_length_format (no endpoints)");
        return;
    }
    if (get_header_and_meme(&header, &hlen, 0, 12345) != 0) {
        puts("[FAIL] allocation/header");
        abort();
    }
    assert(header != NULL);
    assert(strstr(header, "HTTP/1.1 200 OK") != NULL ||
    strstr(header, "403") != NULL);
    if (strstr(header, "200 OK") != NULL) {
        assert(strstr(header, "12345") != NULL);
    }
    free(header);
    puts("[OK] test_header_content_length_format");
}

int main(void)
{
    puts("=== meme_types_handler _TEST ===");
    test_is_download_meme_type();
    test_get_header_and_meme_null_args();
    test_get_header_and_meme_forbidden();
    test_header_content_length_format();
    puts("=== all tests passed ===");
    return 0;
}
