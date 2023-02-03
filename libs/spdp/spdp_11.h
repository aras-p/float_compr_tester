#pragma once

#include <stddef.h>

typedef unsigned char byte_t;

#ifdef __cplusplus
extern "C" {
#endif

size_t spdp_compress_bound(size_t size);
size_t spdp_compress(const byte_t level, const size_t length, byte_t* const buf1, byte_t* const buf2);
void spdp_decompress(const byte_t level, const size_t length, byte_t* const buf2, byte_t* const buf1);

#ifdef __cplusplus
}
#endif
