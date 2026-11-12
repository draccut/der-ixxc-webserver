#include "../../src/core/http_parser.c"

static void test_null_buffer(void)
{
    assert(find_endpoint_length(NULL) == 0);
    printf("PASS: test_null_buffer\n");
}

static void test_empty_string(void)
{
    assert(find_endpoint_length("") == 0);
    printf("PASS: test_empty_string\n");
}

static void test_no_space_at_all(void)
{
    const char *buf = "KEINLEERZEICHEN";
    size_t result = find_endpoint_length(buf);
    assert(result == 15);
    printf("PASS: test_no_space_at_all (result=%zu)\n", result);
}

static void test_only_one_space(void)
{
    const char *buf = "GET /api/users";
    size_t result = find_endpoint_length(buf);
    assert(result == 14);
    printf("PASS: test_only_one_space (result=%zu)\n", result);
}

static void test_only_one_space_with_trailing_slash(void)
{
    const char *buf = "GET /api/users/";
    size_t result = find_endpoint_length(buf);
    assert(result == 14);
    printf("PASS: test_only_one_space (result=%zu)\n", result);
}

static void test_normal_no_trailing_slash(void)
{
    const char *buf = "GET /api/users HTTP/1.1";
    size_t result = find_endpoint_length(buf);
    assert(result == 14);
    printf("PASS: test_normal_no_trailing_slash (result=%zu)\n", result);
}

static void test_normal_trailing_slash(void)
{
    const char *buf = "GET /api/users/ HTTP/1.1";
    size_t result = find_endpoint_length(buf);
    assert(result == 14);
    printf("PASS: test_trailing_slash (result=%zu)\n", result);
}


static void test_root_path_with_trailing_slash_only(void)
{
    const char *buf = "GET /";
    size_t result = find_endpoint_length(buf);
    assert(result == 4);
    printf("PASS: test_root_path_with_trailing_slash_only (result=%zu)\n", result);
}

static void test_post_with_slash(void)
{
    const char *buf = "POST /";
    size_t result = find_endpoint_length(buf);
    assert(result == 5);
    printf("PASS: test_post_with_slash (result=%zu)\n", result);
}

static void test_short_buffer(void)
{
    const char *buf = "AB";
    size_t result = find_endpoint_length(buf);
    assert(result == 2);
    printf("PASS: test_short_buffer (result=%zu)\n", result);
}

int main(void)
{
    test_null_buffer();
    test_empty_string();
    test_no_space_at_all();
    test_only_one_space();
    test_only_one_space_with_trailing_slash();
    test_normal_no_trailing_slash();
    test_normal_trailing_slash();
    test_root_path_with_trailing_slash_only();
    test_post_with_slash();
    test_short_buffer();
    printf("\n pass all tests\n");
    return 0;
}
