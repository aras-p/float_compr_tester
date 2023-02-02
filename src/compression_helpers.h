#pragma once

#include <stdint.h>
#include <stddef.h>

// mesh optimizer
size_t compress_meshopt_vertex_attribute_bound(int vertexCount, int vertexSize);
size_t compress_meshopt_vertex_attribute(const void* src, int vertexCount, int vertexSize, void* dst, size_t dstSize);
int decompress_meshopt_vertex_attribute(const void* src, size_t srcSize, int vertexCount, int vertexSize, void* dst);
void meshopt_get_version(size_t bufSize, char* buf);

// generic lossless compressors
enum CompressionFormat
{
	kCompressionZstd = 0,
	kCompressionLZ4,
	kCompressionZlib,
	kCompressionBrotli,
	kCompressionLibdeflate,
	kCompressionOoodleSelkie,
	kCompressionOoodleMermaid,
	kCompressionOoodleKraken,
	kCompressionCount
};
size_t compress_calc_bound(size_t srcSize, CompressionFormat format);
size_t compress_data(const void* src, size_t srcSize, void* dst, size_t dstSize, CompressionFormat format, int level);
size_t decompress_data(const void* src, size_t srcSize, void* dst, size_t dstSize, CompressionFormat format);
void compressor_get_version(CompressionFormat format, size_t bufSize, char* buf);
