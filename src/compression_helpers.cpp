#include "compression_helpers.h"

#include <meshoptimizer.h>
#include <string.h>
#include <zstd.h>
#include <lz4.h>


int64_t compress_meshopt_index_buffer_bound(int indexCount, int vertexCount)
{
	meshopt_encodeIndexVersion(1);
	return meshopt_encodeIndexBufferBound(indexCount, vertexCount);
}
int64_t compress_meshopt_index_buffer(const uint32_t* indices, int indexCount, void* dst, int64_t dstSize)
{
	meshopt_encodeIndexVersion(1);
	return meshopt_encodeIndexBuffer((unsigned char*)dst, dstSize, indices, indexCount);
}
int decompress_meshopt_index_buffer(const void* src, int64_t srcSize, int indexCount, int indexSize, void* dst)
{
	return meshopt_decodeIndexBuffer(dst, indexCount, indexSize, (const unsigned char*)src, srcSize);
}

int64_t compress_meshopt_vertex_attribute_bound(int vertexCount, int vertexSize)
{
	return meshopt_encodeVertexBufferBound(vertexCount, vertexSize);
}
int64_t compress_meshopt_vertex_attribute(const void* src, int vertexCount, int vertexSize, void* dst, int64_t dstSize)
{
	return meshopt_encodeVertexBuffer((unsigned char*)dst, dstSize, src, vertexCount, vertexSize);
}
int decompress_meshopt_vertex_attribute(const void* src, int64_t srcSize, int vertexCount, int vertexSize, void* dst)
{
	return meshopt_decodeVertexBuffer(dst, vertexCount, vertexSize, (const unsigned char*)src, srcSize);
}

int64_t compress_calc_bound(int64_t srcSize, CompressionFormat format)
{
	if (srcSize == 0)
		return 0;
	switch (format)
	{
	case kCompressionZstd: return ZSTD_compressBound(srcSize);
	case kCompressionLZ4: return LZ4_compressBound(srcSize);
	default: return -1;
	}	
}
int64_t compress_data(const void* src, int64_t srcSize, void* dst, int64_t dstSize, CompressionFormat format, int level)
{
	if (srcSize == 0)
		return 0;
	switch (format)
	{
	case kCompressionZstd: return ZSTD_compress(dst, dstSize, src, srcSize, level);
	case kCompressionLZ4: return LZ4_compress_default((const char*)src, (char*)dst, srcSize, dstSize);
	default: return -1;
	}
}
int64_t decompress_calc_bound(const void* src, int64_t srcSize, CompressionFormat format)
{
	if (srcSize == 0)
		return 0;
	switch (format)
	{
	case kCompressionZstd: return ZSTD_getFrameContentSize(src, srcSize);
	case kCompressionLZ4: return -1; // LZ4 does not know decompressed size; user must track that themselves
	default: return -1;
	}	
}
int64_t decompress_data(const void* src, int64_t srcSize, void* dst, int64_t dstSize, CompressionFormat format)
{
	if (srcSize == 0)
		return 0;
	switch (format)
	{
	case kCompressionZstd: return ZSTD_decompress(dst, dstSize, src, srcSize);
	case kCompressionLZ4: return LZ4_decompress_safe((const char*)src, (char*)dst, srcSize, dstSize);
	default: return -1;
	}	
}
