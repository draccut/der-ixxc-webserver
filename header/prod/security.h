#pragma once
#include <stddef.h>

int constant_time_compare(const char *input, size_t input_len, const char *compare_to);
void assembly_zero_buf(void *buf, size_t len);
