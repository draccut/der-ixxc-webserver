#pragma once

#include <stddef.h>

#include "../../ixxc_endpoints.h"

void pub_detect_meme_type(char **http_header,
                          size_t *http_header_lengths,
                          const size_t *file_sizes);
