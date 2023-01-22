#include "compressors.h"
#include <stdio.h>

#include <fpzip.h>
#include <zfp.h>


uint8_t* GenericCompressor::Compress(const float* data, int width, int height, int channels, size_t& outSize)
{
	size_t planeElems = width * height;
	size_t dataElems = planeElems * channels;
	size_t dataSize = dataElems * sizeof(float);
	uint8_t* tmp = (uint8_t*)data;
	if (m_Flags != kFlagNone)
	{
		tmp = new uint8_t[dataSize];
		if ((m_Flags & kFlagSplitFloats) != 0)
		{
			float* dst = (float*)tmp;
			for (int ich = 0; ich < channels; ++ich)
			{
				const float* src = data + ich;
				for (int ip = 0; ip < planeElems; ++ip)
				{
					*dst = *src;
					src += channels;
					dst += 1;
				}
			}
		}
		if ((m_Flags & kFlagSplitBytes) != 0)
		{
			int stride = channels * sizeof(float);
			uint8_t* dst = tmp;
			for (int is = 0; is < stride; ++is)
			{
				const uint8_t* src = (const uint8_t*)data + is;
				for (int ip = 0; ip < planeElems; ++ip)
				{
					*dst = *src;
					src += stride;
					dst += 1;
				}
			}
		}
	}

	size_t bound = compress_calc_bound(dataSize, m_Format);
	uint8_t* cmp = new uint8_t[bound];
	outSize = compress_data(tmp, dataSize, cmp, bound, m_Format, m_Level);

	if (m_Flags != kFlagNone)
		delete[] tmp;
	return cmp;
}

void GenericCompressor::Decompress(const uint8_t* cmp, size_t cmpSize, float* data, int width, int height, int channels)
{
	size_t planeElems = width * height;
	size_t dataElems = planeElems * channels;
	size_t dataSize = dataElems * sizeof(float);
	uint8_t* tmp = (uint8_t*)data;
	if (m_Flags != kFlagNone)
		tmp = new uint8_t[dataSize];

	decompress_data(cmp, cmpSize, tmp, dataSize, m_Format);

	if (m_Flags != kFlagNone)
	{
		if ((m_Flags & kFlagSplitFloats) != 0)
		{
			const float* src = (const float*)tmp;
			for (int ich = 0; ich < channels; ++ich)
			{
				float* dst = data + ich;
				for (int ip = 0; ip < planeElems; ++ip)
				{
					*dst = *src;
					src += 1;
					dst += channels;
				}
			}
		}
		if ((m_Flags & kFlagSplitBytes) != 0)
		{
			int stride = channels * sizeof(float);
			const uint8_t* src = tmp;
			for (int is = 0; is < stride; ++is)
			{
				uint8_t* dst = (uint8_t*)data + is;
				for (int ip = 0; ip < planeElems; ++ip)
				{
					*dst = *src;
					src += 1;
					dst += stride;
				}
			}
		}
		delete[] tmp;
	}
}

static const char* kCompressionFormatNames[kCompressionCount] = {
	"zstd",
	"lz4",
};

void GenericCompressor::PrintName(size_t bufSize, char* buf) const
{
	const char* flag = "";
	if ((m_Flags & kFlagSplitFloats) != 0)
		flag = "sf";
	if ((m_Flags & kFlagSplitBytes) != 0)
		flag = "sb";
	snprintf(buf, bufSize, "%s-%i%s", kCompressionFormatNames[m_Format], m_Level, flag);
}

uint8_t* MeshOptCompressor::Compress(const float* data, int width, int height, int channels, size_t& outSize)
{
	int stride = channels * sizeof(float);
	int vertexCount = width * height;
	size_t moBound = compress_meshopt_vertex_attribute_bound(vertexCount, stride);
	uint8_t* moCmp = new uint8_t[moBound];
	size_t moSize = compress_meshopt_vertex_attribute(data, vertexCount, stride, moCmp, moBound);
	if (m_Format == kCompressionCount)
	{
		outSize = moSize;
		return moCmp;
	}

	size_t bound = compress_calc_bound(moSize, m_Format);
	uint8_t* cmp = new uint8_t[bound];
	outSize = compress_data(moCmp, moSize, cmp, bound, m_Format, m_Level);
	delete[] moCmp;
	return cmp;
}

void MeshOptCompressor::Decompress(const uint8_t* cmp, size_t cmpSize, float* data, int width, int height, int channels)
{
	int stride = channels * sizeof(float);
	int vertexCount = width * height;
	if (m_Format == kCompressionCount)
	{
		decompress_meshopt_vertex_attribute(cmp, cmpSize, vertexCount, stride, data);
	}
	else
	{
		size_t dataSize = width * height * channels * sizeof(float);
		uint8_t* tmp = new uint8_t[dataSize];
		size_t tmpSize = decompress_data(cmp, cmpSize, tmp, dataSize, m_Format);
		decompress_meshopt_vertex_attribute(tmp, tmpSize, vertexCount, stride, data);
		delete[] tmp;
	}
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
