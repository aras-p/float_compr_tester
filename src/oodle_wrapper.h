#pragma once

#include "compression_helpers.h"

#if BUILD_WITH_OODLE
void oodle_init();
size_t oodle_compress_calc_bound(size_t srcSize, CompressionFormat format);
size_t oodle_compress_data(const void* src, size_t srcSize, void* dst, size_t dstSize, CompressionFormat format, int level);
size_t oodle_decompress_data(const void* src, size_t srcSize, void* dst, size_t dstSize, CompressionFormat format);
void oodle_compressor_get_version(CompressionFormat format, size_t bufSize, char* buf);
#endif
