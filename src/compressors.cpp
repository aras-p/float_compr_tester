#include "compressors.h"
#include <stdio.h>

uint8_t* GenericCompressor::Compress(const float* data, int width, int height, int channels, size_t& outSize)
{
	size_t dataSize = width * height * channels * sizeof(float);
	size_t bound = compress_calc_bound(dataSize, m_Format);
	uint8_t* cmp = new uint8_t[bound];
	outSize = compress_data(data, dataSize, cmp, bound, m_Format, m_Level);	
	return cmp;
}

void GenericCompressor::Decompress(const uint8_t* cmp, size_t cmpSize, float* data, int width, int height, int channels)
{
	size_t dataSize = width * height * channels * sizeof(float);
	decompress_data(cmp, cmpSize, data, dataSize, m_Format);
}

static const char* kCompressionFormatNames[kCompressionCount] = {
	"zstd",
	"lz4",
};

void GenericCompressor::PrintName(size_t bufSize, char* buf) const
{
	snprintf(buf, bufSize, "%s-%i", kCompressionFormatNames[m_Format], m_Level);
}
