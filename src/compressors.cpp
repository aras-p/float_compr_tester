#include "compressors.h"
#include <stdio.h>

#include <fpzip.h>
#include <zfp.h>

#include <assert.h> // ndzip needs it
#include <ndzip/ndzip.hh>
#include <streamvbyte.h>
#include <streamvbytedelta.h>


template<typename T>
static void EncodeDeltaDif(T* data, size_t dataElems)
{
	T prev = 0;
	for (size_t i = 0; i < dataElems; ++i)
	{
		T v = *data;
		*data = v - prev;
		prev = v;
		++data;
	}
}

template<typename T>
static void DecodeDeltaDif(T* data, size_t dataElems)
{
	T prev = 0;
	for (size_t i = 0; i < dataElems; ++i)
	{
		T v = *data;
		v = prev + v;
		*data = v;
		prev = v;
		++data;
	}
}

template<typename T>
static void EncodeDeltaXor(T* data, size_t dataElems)
{
	T prev = 0;
	for (size_t i = 0; i < dataElems; ++i)
	{
		T v = *data;
		*data = v ^ prev;
		prev = v;
		++data;
	}
}

template<typename T>
static void DecodeDeltaXor(T* data, size_t dataElems)
{
	T prev = 0;
	for (size_t i = 0; i < dataElems; ++i)
	{
		T v = *data;
		v = prev ^ v;
		*data = v;
		prev = v;
		++data;
	}
}

template<typename T>
static void Transpose(const T* src, T* dst, int channels, int planeElems)
{
	for (int ich = 0; ich < channels; ++ich)
	{
		const T* ptr = src + ich;
		for (int ip = 0; ip < planeElems; ++ip)
		{
			*dst = *ptr;
			ptr += channels;
			dst += 1;
		}
	}
}

template<typename T>
static void UnTranspose(const T* src, T* dst, int channels, int planeElems)
{
	for (int ich = 0; ich < channels; ++ich)
	{
		T* ptr = dst + ich;
		for (int ip = 0; ip < planeElems; ++ip)
		{
			*ptr = *src;
			src += 1;
			ptr += channels;
		}
	}
}

static uint8_t* CompressionFilter(uint32_t filter, const float* data, int width, int height, int channels)
{
	uint8_t* tmp = (uint8_t*)data;
	if (filter != kFilterNone)
	{
		int planeElems = width * height;
		int dataSize = planeElems * channels * sizeof(float);
		tmp = new uint8_t[dataSize];
		if ((filter & kFilterSplitFloats) != 0)
		{
			int dataElems = planeElems * channels;
			Transpose(data, (float*)tmp, channels, planeElems);
			if ((filter & kFilterDeltaDiff) != 0) EncodeDeltaDif((uint32_t*)tmp, dataElems);
			if ((filter & kFilterDeltaXor) != 0) EncodeDeltaXor((uint32_t*)tmp, dataElems);
		}
		if ((filter & kFilterSplitBytes) != 0)
		{
			Transpose((uint8_t*)data, tmp, channels * sizeof(float), planeElems);
			if ((filter & kFilterDeltaDiff) != 0) EncodeDeltaDif((uint8_t*)tmp, dataSize);
			if ((filter & kFilterDeltaXor) != 0) EncodeDeltaXor((uint8_t*)tmp, dataSize);
		}
	}
	return tmp;
}

static void DecompressionFilter(uint32_t filter, uint8_t* tmp, float* data, int width, int height, int channels)
{
	if (filter != kFilterNone)
	{
		int planeElems = width * height;
		if ((filter & kFilterSplitFloats) != 0)
		{
			int dataElems = planeElems * channels;
			if ((filter & kFilterDeltaDiff) != 0) DecodeDeltaDif((uint32_t*)tmp, dataElems);
			if ((filter & kFilterDeltaXor) != 0) DecodeDeltaXor((uint32_t*)tmp, dataElems);
			UnTranspose((const float*)tmp, data, channels, planeElems);
		}
		if ((filter & kFilterSplitBytes) != 0)
		{
			int dataSize = planeElems * channels * sizeof(float);
			if ((filter & kFilterDeltaDiff) != 0) DecodeDeltaDif(tmp, dataSize);
			if ((filter & kFilterDeltaXor) != 0) DecodeDeltaXor(tmp, dataSize);
			UnTranspose(tmp, (uint8_t*)data, channels * sizeof(float), planeElems);
		}
		delete[] tmp;
	}
}


uint8_t* GenericCompressor::Compress(const float* data, int width, int height, int channels, size_t& outSize)
{
	size_t dataSize = width * height * channels * sizeof(float);
	uint8_t* tmp = CompressionFilter(m_Filter, data, width, height, channels);

	size_t bound = compress_calc_bound(dataSize, m_Format);
	uint8_t* cmp = new uint8_t[bound];
	outSize = compress_data(tmp, dataSize, cmp, bound, m_Format, m_Level);

	if (m_Filter != kFilterNone) delete[] tmp;
	return cmp;
}

void GenericCompressor::Decompress(const uint8_t* cmp, size_t cmpSize, float* data, int width, int height, int channels)
{
	size_t dataSize = width * height * channels * sizeof(float);
	uint8_t* tmp = (uint8_t*)data;
	if (m_Filter != kFilterNone) tmp = new uint8_t[dataSize];

	decompress_data(cmp, cmpSize, tmp, dataSize, m_Format);

	DecompressionFilter(m_Filter, tmp, data, width, height, channels);
}

static const char* kCompressionFormatNames[kCompressionCount] = {
	"zstd",
	"lz4",
};

void GenericCompressor::PrintName(size_t bufSize, char* buf) const
{
	const char* flag = "";
	if ((m_Filter & kFilterSplitFloats) != 0)
		flag = "sf";
	if ((m_Filter & kFilterSplitBytes) != 0)
		flag = "sb";
	const char* delta = "";
	if ((m_Filter & kFilterDeltaDiff) != 0)
		delta = "_dif";
	if ((m_Filter & kFilterDeltaXor) != 0)
		delta = "_xor";
	snprintf(buf, bufSize, "%s-%i%s%s", kCompressionFormatNames[m_Format], m_Level, flag, delta);
}

static uint8_t* CompressGeneric(CompressionFormat format, int level, uint8_t* data, size_t dataSize, size_t& outSize)
{
	if (format == kCompressionCount)
	{
		outSize = dataSize;
		return data;
	}
	size_t bound = compress_calc_bound(dataSize, format);
	uint8_t* cmp = new uint8_t[bound];
	outSize = compress_data(data, dataSize, cmp, bound, format, level);
	delete[] data;
	return cmp;
}

static uint8_t* DecompressGeneric(CompressionFormat format, int level, const uint8_t* cmp, size_t cmpSize, size_t& outSize)
{
	if (format == kCompressionCount)
	{
		outSize = cmpSize;
		return (uint8_t*)cmp;
	}
	size_t bound = decompress_calc_bound(cmp, cmpSize, format);
	uint8_t* decomp = new uint8_t[bound];
	outSize = decompress_data(cmp, cmpSize, decomp, bound, format);
	return decomp;
}


uint8_t* MeshOptCompressor::Compress(const float* data, int width, int height, int channels, size_t& outSize)
{
	uint8_t* tmp = CompressionFilter(m_Filter, data, width, height, channels);

	int stride = (m_Filter & kFilterSplitFloats) ? sizeof(float) : channels * sizeof(float);
	size_t dataSize = width * height * channels * sizeof(float);
	int vertexCount = dataSize / stride;
	size_t moBound = compress_meshopt_vertex_attribute_bound(vertexCount, stride);
	uint8_t* moCmp = new uint8_t[moBound];
	size_t moSize = compress_meshopt_vertex_attribute(tmp, vertexCount, stride, moCmp, moBound);
	if (m_Filter != kFilterNone) delete[] tmp;
	return CompressGeneric(m_Format, m_Level, moCmp, moSize, outSize);
}

void MeshOptCompressor::Decompress(const uint8_t* cmp, size_t cmpSize, float* data, int width, int height, int channels)
{
	size_t dataSize = width * height * channels * sizeof(float);

	int stride = (m_Filter & kFilterSplitFloats) ? sizeof(float) : channels * sizeof(float);
	int vertexCount = dataSize / stride;

	size_t decompSize;
	uint8_t* decomp = DecompressGeneric(m_Format, m_Level, cmp, cmpSize, decompSize);

	uint8_t* tmp = (uint8_t*)data;
	if (m_Filter != kFilterNone) tmp = new uint8_t[dataSize];
	decompress_meshopt_vertex_attribute(decomp, decompSize, vertexCount, stride, tmp);
	if (decomp != cmp) delete[] decomp;

	DecompressionFilter(m_Filter, tmp, data, width, height, channels);
}

void MeshOptCompressor::PrintName(size_t bufSize, char* buf) const
{
	if (m_Format == kCompressionCount)
		snprintf(buf, bufSize, "meshopt");
	else
		snprintf(buf, bufSize, "meshopt-%s-%i", kCompressionFormatNames[m_Format], m_Level);
}

uint8_t* FpzipCompressor::Compress(const float* data, int width, int height, int channels, size_t& outSize)
{
	size_t dataSize = width * height * channels * sizeof(float);
	size_t bound = dataSize; //@TODO: what's the max compression bound?
	uint8_t* cmp = new uint8_t[bound];
	FPZ* fpz = fpzip_write_to_buffer(cmp, bound);
	fpz->type = FPZIP_TYPE_FLOAT;
	fpz->prec = 0;
	fpz->nx = width;
	fpz->ny = height;
	fpz->nz = 1;
	fpz->nf = channels;
	size_t cmpSize = fpzip_write(fpz, data);
	fpzip_write_close(fpz);
	outSize = cmpSize;
	return cmp;
}

void FpzipCompressor::Decompress(const uint8_t* cmp, size_t cmpSize, float* data, int width, int height, int channels)
{
	FPZ* fpz = fpzip_read_from_buffer(cmp);
	fpz->type = FPZIP_TYPE_FLOAT;
	fpz->prec = 0;
	fpz->nx = width;
	fpz->ny = height;
	fpz->nz = 1;
	fpz->nf = channels;
	fpzip_read(fpz, data);
	fpzip_read_close(fpz);
}

void FpzipCompressor::PrintName(size_t bufSize, char* buf) const
{
	snprintf(buf, bufSize, "fpzip");
}

uint8_t* ZfpCompressor::Compress(const float* data, int width, int height, int channels, size_t& outSize)
{
	zfp_field field = {};
	field.type = zfp_type_float;
	field.nx = width;
	field.ny = height;
	field.sx = channels;
	field.sy = channels * width;

	zfp_stream* zfp = zfp_stream_open(NULL);
	zfp_stream_set_reversible(zfp);
	//const float kAccuracy = 0.1f;
	//zfp_stream_set_accuracy(zfp, kAccuracy);

	size_t bound = zfp_stream_maximum_size(zfp, &field) * channels;
	uint8_t* cmp = new uint8_t[bound];

	bitstream* stream = stream_open(cmp, bound);
	zfp_stream_set_bit_stream(zfp, stream);
	zfp_stream_rewind(zfp);

	outSize = 0;
	for (int ich = 0; ich < channels; ++ich)
	{
		field.data = (void*)(data + ich);
		outSize += zfp_compress(zfp, &field);
	}
	stream_close(stream);
	zfp_stream_close(zfp);
	return cmp;
}

void ZfpCompressor::Decompress(const uint8_t* cmp, size_t cmpSize, float* data, int width, int height, int channels)
{
	zfp_field field = {};
	field.type = zfp_type_float;
	field.nx = width;
	field.ny = height;
	field.sx = channels;
	field.sy = channels * width;
	zfp_stream* zfp = zfp_stream_open(NULL);
	zfp_stream_set_reversible(zfp);
	bitstream* stream = stream_open((void*)cmp, cmpSize);
	zfp_stream_set_bit_stream(zfp, stream);
	zfp_stream_rewind(zfp);
	for (int ich = 0; ich < channels; ++ich)
	{
		field.data = (void*)(data + ich);
		zfp_decompress(zfp, &field);
	}
	stream_close(stream);
	zfp_stream_close(zfp);
}

void ZfpCompressor::PrintName(size_t bufSize, char* buf) const
{
	snprintf(buf, bufSize, "zfp");
}

uint8_t* NdzipCompressor::Compress(const float* data, int width, int height, int channels, size_t& outSize)
{
	// ndzip seems to have trouble if we try to do channels*width*height 3 dimensions
	// (does not handle dimension size < 16?), so do a 2D case instead
	ndzip::extent ext(2);
	ext[0] = width * channels;
	ext[1] = height;
	auto compressor = ndzip::make_compressor<float>(2, 1);
	size_t bound = ndzip::compressed_length_bound<float>(ext) * 4;
	uint8_t* cmp = new uint8_t[bound];
	size_t cmpSize = compressor->compress(data, ext, (uint32_t*)cmp) * 4;
	outSize = cmpSize;
	return cmp;
}

void NdzipCompressor::Decompress(const uint8_t* cmp, size_t cmpSize, float* data, int width, int height, int channels)
{
	ndzip::extent ext(2);
	ext[0] = width * channels;
	ext[1] = height;
	auto decompressor = ndzip::make_decompressor<float>(2, 1);
	decompressor->decompress((const uint32_t*)cmp, data, ext);
}

void NdzipCompressor::PrintName(size_t bufSize, char* buf) const
{
	snprintf(buf, bufSize, "ndzip");
}

uint8_t* StreamVByteCompressor::Compress(const float* data, int width, int height, int channels, size_t& outSize)
{
	uint32_t dataElems = width * height * channels;
	size_t bound = streamvbyte_max_compressedbytes(dataElems);
	uint8_t* cmp = new uint8_t[bound];
	size_t cmpSize = 0;
	if (m_Delta)
		cmpSize = streamvbyte_delta_encode((const uint32_t*)data, dataElems, cmp, 0);
	else
		cmpSize = streamvbyte_encode((const uint32_t*)data, dataElems, cmp);

	return CompressGeneric(m_Format, m_Level, cmp, cmpSize, outSize);
}

void StreamVByteCompressor::Decompress(const uint8_t* cmp, size_t cmpSize, float* data, int width, int height, int channels)
{
	size_t decompSize;
	uint8_t* decomp = DecompressGeneric(m_Format, m_Level, cmp, cmpSize, decompSize);

	uint32_t dataElems = width * height * channels;
	if (m_Delta)
		streamvbyte_delta_decode(decomp, (uint32_t*)data, dataElems, 0);
	else
		streamvbyte_decode(decomp, (uint32_t*)data, dataElems);
	if (decomp != cmp) delete[] decomp;
}

void StreamVByteCompressor::PrintName(size_t bufSize, char* buf) const
{
	snprintf(buf, bufSize, m_Delta ? "streamvbyte_d" : "streamvbyte");
}
