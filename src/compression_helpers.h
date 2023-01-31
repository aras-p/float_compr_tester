#pragma once

#include <stdint.h>

// mesh optimizer
int64_t compress_meshopt_index_buffer_bound(int indexCount, int vertexCount);
int64_t compress_meshopt_index_buffer(const uint32_t* indices, int indexCount, void* dst, int64_t dstSize);
int decompress_meshopt_index_buffer(const void* src, int64_t srcSize, int indexCount, int indexSize, void* dst);
int64_t compress_meshopt_vertex_attribute_bound(int vertexCount, int vertexSize);
int64_t compress_meshopt_vertex_attribute(const void* src, int vertexCount, int vertexSize, void* dst, int64_t dstSize);
int decompress_meshopt_vertex_attribute(const void* src, int64_t srcSize, int vertexCount, int vertexSize, void* dst);

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
int64_t compress_calc_bound(int64_t srcSize, CompressionFormat format);
int64_t compress_data(const void* src, int64_t srcSize, void* dst, int64_t dstSize, CompressionFormat format, int level);
int64_t decompress_data(const void* src, int64_t srcSize, void* dst, int64_t dstSize, CompressionFormat format);

