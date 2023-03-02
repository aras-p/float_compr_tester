#include "compression_helpers.h"

#include <meshoptimizer.h>
#include <string.h>
#include <zstd.h>
#include <lz4.h>
#include <lz4hc.h>
#include <zlib.h>
#include <libdeflate.h>
#include <brotli/encode.h>
#include <brotli/decode.h>
#include <stdio.h>
#include <blosc2.h>

#if BUILD_WITH_OODLE
#include "oodle_wrapper.h"
#endif

size_t compress_meshopt_vertex_attribute_bound(int vertexCount, int vertexSize)
{
	return meshopt_encodeVertexBufferBound(vertexCount, vertexSize);
}
size_t compress_meshopt_vertex_attribute(const void* src, int vertexCount, int vertexSize, void* dst, size_t dstSize)
{
	return meshopt_encodeVertexBuffer((unsigned char*)dst, dstSize, src, vertexCount, vertexSize);
}
int decompress_meshopt_vertex_attribute(const void* src, size_t srcSize, int vertexCount, int vertexSize, void* dst)
{
	return meshopt_decodeVertexBuffer(dst, vertexCount, vertexSize, (const unsigned char*)src, srcSize);
}

void meshopt_get_version(size_t bufSize, char* buf)
{
	snprintf(buf, bufSize, "meshopt-%i.%i", MESHOPTIMIZER_VERSION/1000, (MESHOPTIMIZER_VERSION/10)%1000);
}


size_t compress_calc_bound(size_t srcSize, CompressionFormat format)
{
	if (srcSize == 0)
		return 0;
	switch (format)
	{
	case kCompressionZstd: return ZSTD_compressBound(srcSize);
	case kCompressionLZ4: return LZ4_compressBound(int(srcSize));
	case kCompressionZlib: return compressBound(uLong(srcSize));
	case kCompressionBrotli: return BrotliEncoderMaxCompressedSize(srcSize);
	case kCompressionLibdeflate:
	{
		libdeflate_compressor* c = libdeflate_alloc_compressor(12);
		size_t size = libdeflate_deflate_compress_bound(c, srcSize);
		libdeflate_free_compressor(c);
		return size;
	}
	case kCompressionBloscBLZ:
	case kCompressionBloscLZ4:
	case kCompressionBloscZstd:
	case kCompressionBloscBLZ_Shuf:
	case kCompressionBloscLZ4_Shuf:
	case kCompressionBloscZstd_Shuf:
	case kCompressionBloscBLZ_ShufDelta:
	case kCompressionBloscLZ4_ShufDelta:
	case kCompressionBloscZstd_ShufDelta:
		return srcSize + BLOSC2_MAX_OVERHEAD;
#	if BUILD_WITH_OODLE
	case kCompressionOoodleSelkie:
	case kCompressionOoodleMermaid:
	case kCompressionOoodleKraken:
		return oodle_compress_calc_bound(srcSize, format);
#	endif
	default: return 0;
	}	
}
size_t compress_data(const void* src, size_t srcSize, void* dst, size_t dstSize, CompressionFormat format, int level, int stride)
{
	if (srcSize == 0)
		return 0;
	switch (format)
	{
	case kCompressionZstd: return ZSTD_compress(dst, dstSize, src, srcSize, level);
	case kCompressionLZ4:
		if (level > 0)
			return LZ4_compress_HC((const char*)src, (char*)dst, (int)srcSize, (int)dstSize, level);
		return LZ4_compress_fast((const char*)src, (char*)dst, (int)srcSize, (int)dstSize, (level > 0 ? level : -level) * 10);
	case kCompressionZlib:
	{
		uLongf cmpSize = uLong(dstSize);
		int res = compress2((Bytef*)dst, &cmpSize, (const Bytef*)src, uLong(srcSize), level);
		if (res != Z_OK)
			cmpSize = 0;
		return cmpSize;
	}
	case kCompressionBrotli:
	{
		size_t cmpSize = dstSize;
		bool res = BrotliEncoderCompress(level, BROTLI_DEFAULT_WINDOW, BROTLI_DEFAULT_MODE, srcSize, (const uint8_t*)src, &cmpSize, (uint8_t*)dst);
		return cmpSize;
	}
	case kCompressionLibdeflate:
	{
		libdeflate_compressor* c = libdeflate_alloc_compressor(level);
		size_t size = libdeflate_deflate_compress(c, src, srcSize, dst, dstSize);
		libdeflate_free_compressor(c);
		return size;
	}
	case kCompressionBloscBLZ:
	case kCompressionBloscLZ4:
	case kCompressionBloscZstd:
	case kCompressionBloscBLZ_Shuf:
	case kCompressionBloscLZ4_Shuf:
	case kCompressionBloscZstd_Shuf:
	case kCompressionBloscBLZ_ShufDelta:
	case kCompressionBloscLZ4_ShufDelta:
	case kCompressionBloscZstd_ShufDelta:
	{
		blosc2_cparams params = BLOSC2_CPARAMS_DEFAULTS;
		if (format == kCompressionBloscBLZ || format == kCompressionBloscBLZ_Shuf || format == kCompressionBloscBLZ_ShufDelta)
			params.compcode = BLOSC_BLOSCLZ;
		if (format == kCompressionBloscLZ4 || format == kCompressionBloscLZ4_Shuf || format == kCompressionBloscLZ4_ShufDelta)
			params.compcode = level > 0 ? BLOSC_LZ4HC : BLOSC_LZ4;
		if (format == kCompressionBloscZstd || format == kCompressionBloscZstd_Shuf || format == kCompressionBloscZstd_ShufDelta)
			params.compcode = BLOSC_ZSTD;
		params.clevel = level;
		if (params.compcode == BLOSC_LZ4)
			params.clevel = 1;
		params.typesize = stride;
		//params.blocksize = (int)srcSize;
		//params.splitmode = BLOSC_NEVER_SPLIT;

		params.filters[BLOSC2_MAX_FILTERS - 1] = BLOSC_NOFILTER;
		if (format >= kCompressionBloscBLZ_Shuf && format <= kCompressionBloscZstd_ShufDelta)
			params.filters[BLOSC2_MAX_FILTERS - 1] = BLOSC_SHUFFLE;
		if (format >= kCompressionBloscBLZ_ShufDelta && format <= kCompressionBloscZstd_ShufDelta)
			params.filters[BLOSC2_MAX_FILTERS - 2] = BLOSC_DELTA;
		blosc2_context* ctx = blosc2_create_cctx(params);
		int size = blosc2_compress_ctx(ctx, src, (int)srcSize, dst, (int)dstSize);
		blosc2_free_ctx(ctx);
		if (size < 0)
			size = 0;
		return size;
	}
#	if BUILD_WITH_OODLE
	case kCompressionOoodleSelkie:
	case kCompressionOoodleMermaid:
	case kCompressionOoodleKraken:
		return oodle_compress_data(src, srcSize, dst, dstSize, format, level);
#	endif
	default: return 0;
	}
}
size_t decompress_data(const void* src, size_t srcSize, void* dst, size_t dstSize, CompressionFormat format)
{
	if (srcSize == 0)
		return 0;
	switch (format)
	{
	case kCompressionZstd: return ZSTD_decompress(dst, dstSize, src, srcSize);
	case kCompressionLZ4: return LZ4_decompress_safe((const char*)src, (char*)dst, (int)srcSize, (int)dstSize);
	case kCompressionZlib:
	{
		uLongf dstLen = uLong(dstSize);
		int res = uncompress((Bytef*)dst, &dstLen, (const Bytef*)src, uLong(srcSize));
		if (res != Z_OK)
			return 0;
		return dstLen;
	}
	case kCompressionBrotli:
	{
		size_t dstLen = dstSize;
		BrotliDecoderResult res = BrotliDecoderDecompress(srcSize, (const uint8_t*)src, &dstLen, (uint8_t*)dst);
		if (res != BROTLI_DECODER_RESULT_SUCCESS)
			return 0;
		return dstLen;
	}
	case kCompressionLibdeflate:
	{
		libdeflate_decompressor* c = libdeflate_alloc_decompressor();
		size_t gotSize = 0;
		libdeflate_result res = libdeflate_deflate_decompress(c, src, srcSize, dst, dstSize, &gotSize);
		libdeflate_free_decompressor(c);
		return gotSize;
	}
	case kCompressionBloscBLZ:
	case kCompressionBloscLZ4:
	case kCompressionBloscZstd:
	case kCompressionBloscBLZ_Shuf:
	case kCompressionBloscLZ4_Shuf:
	case kCompressionBloscZstd_Shuf:
	case kCompressionBloscBLZ_ShufDelta:
	case kCompressionBloscLZ4_ShufDelta:
	case kCompressionBloscZstd_ShufDelta:
	{
		blosc2_dparams params = BLOSC2_DPARAMS_DEFAULTS;
		blosc2_context* ctx = blosc2_create_dctx(params);
		int size = blosc2_decompress_ctx(ctx, src, (int)srcSize, dst, (int)dstSize);
		blosc2_free_ctx(ctx);
		if (size < 0)
			size = 0;
		return size;
	}
#	if BUILD_WITH_OODLE
	case kCompressionOoodleSelkie:
	case kCompressionOoodleMermaid:
	case kCompressionOoodleKraken:
		return oodle_decompress_data(src, srcSize, dst, dstSize, format);
#	endif
	default: return 0;
	}	
}

void compressor_get_version(CompressionFormat format, size_t bufSize, char* buf)
{
	switch (format) {
	case kCompressionZstd: snprintf(buf, bufSize, "zstd-%s", ZSTD_versionString()); break;
	case kCompressionLZ4: snprintf(buf, bufSize, "lz4-%s", LZ4_versionString()); break;
	case kCompressionZlib: snprintf(buf, bufSize, "zlib-%s", ZLIB_VERSION); break;
	case kCompressionBrotli: snprintf(buf, bufSize, "brotli-%i.%i.%i", BrotliDecoderVersion() >> 24, (BrotliDecoderVersion() >> 12) & 0xFFF, BrotliDecoderVersion() & 0xFFF); break;
	case kCompressionLibdeflate: snprintf(buf, bufSize, "libdeflate-%s", LIBDEFLATE_VERSION_STRING); break;
	case kCompressionBloscBLZ:
	case kCompressionBloscLZ4:
	case kCompressionBloscZstd:
	case kCompressionBloscBLZ_Shuf:
	case kCompressionBloscLZ4_Shuf:
	case kCompressionBloscZstd_Shuf:
	case kCompressionBloscBLZ_ShufDelta:
	case kCompressionBloscLZ4_ShufDelta:
	case kCompressionBloscZstd_ShufDelta:
		snprintf(buf, bufSize, "blosc-%s", BLOSC2_VERSION_STRING);
		break;

#	if BUILD_WITH_OODLE
	case kCompressionOoodleSelkie:
	case kCompressionOoodleMermaid:
	case kCompressionOoodleKraken:
		oodle_compressor_get_version(format, bufSize, buf); break;
#	endif
	default:
		snprintf(buf, bufSize, "Unknown-%i", format);
	}
}
