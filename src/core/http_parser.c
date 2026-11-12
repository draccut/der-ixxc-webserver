#include "../../header/prod/common.h"
#include "../../header/prod/http_parser.h"

/*
 * Returns the effective path length from a raw HTTP request line.
 * Skips the method, measures up to the second space or end of buffer,
 * and strips a trailing '/' except for the root path '/'
 * Used for constant-time comparison against a fixed buffer.
 *
 * Examples:
 *   "GET /api/users HTTP/1.1"    ->  14   ("GET /api/users")
 *   "GET /api/users/ HTTP/1.1"   ->  14   (trailing '/' removed)
 *
 * @param buf  Pointer to the request buffer
 * @return     Effective path length, or 0 on invalid input
 */
size_t find_endpoint_length(const char *buf)
{
    const size_t len = MAX_READ;
    if (!buf) {
        return 0;
    }
    size_t i = 0;
    while (i < len && buf[i] != ' ') {
        if (buf[i] == '\0') {
            return i;
        }
        ++i;
    }
    if (i >= (len - 1)) {
        return (len - 1);
    }
    ++i;
    size_t path_start = i;
    while (i < len && buf[i] != ' ') {
        if (buf[i] == '\0') {
            if (buf[i - 1] == '/') {
                return (i - 1);
            } else {
                return i;
            }
        }
        ++i;
    }
    if (i >= (len - 1)) {
        return (len - 1);
    }
    if (buf[i - 2] != ' ' && buf[i - 1] == '/' && i > path_start) {
        return (i - 1);
    } else {
        return i;
    }
}
