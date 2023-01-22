#include "compressors.h"
#include <stdio.h>

uint8_t* GenericCompressor::Compress(const float* data, int width, int height, int channels, size_t& outSize)
{
	if (!m_SplitChannels)
	{
		size_t dataSize = width * height * channels * sizeof(float);
		size_t bound = compress_calc_bound(dataSize, m_Format);
		uint8_t* cmp = new uint8_t[bound];
		outSize = compress_data(data, dataSize, cmp, bound, m_Format, m_Level);
		return cmp;
	}
	else
	{
		size_t channelSize = width * height * sizeof(float);
		size_t channelBound = compress_calc_bound(channelSize, m_Format);
		size_t bound = channelBound * channels + 4 * channels;
		uint8_t* cmp = new uint8_t[bound];
		float* tmp = new float[width * height];
		outSize = 4 * channels;
		for (int ich = 0; ich < channels; ++ich)
		{
			const float* dataPtr = data + ich;
			for (int j = 0; j < width * height; ++j, dataPtr += channels)
				tmp[j] = *dataPtr;
			size_t cmpSize = compress_data(tmp, channelSize, cmp + outSize, channelBound, m_Format, m_Level);
			*(uint32_t*)(cmp + ich * 4) = cmpSize;
			outSize += cmpSize;
		}
		delete[] tmp;
		return cmp;
	}
}

void GenericCompressor::Decompress(const uint8_t* cmp, size_t cmpSize, float* data, int width, int height, int channels)
{
	if (!m_SplitChannels)
	{
		size_t dataSize = width * height * channels * sizeof(float);
		decompress_data(cmp, cmpSize, data, dataSize, m_Format);
	}
	else
	{
		size_t channelSize = width * height * sizeof(float);
		float* tmp = new float[width * height];
		const uint8_t* cmpPtr = cmp + channels * 4;
		for (int ich = 0; ich < channels; ++ich)
		{
			uint32_t cmpChannelSize = *(uint32_t*)(cmp + ich * 4);
			decompress_data(cmpPtr, cmpChannelSize, tmp, width * height * 4, m_Format);
			cmpPtr += cmpChannelSize;

			float* dataPtr = data + ich;
			for (int j = 0; j < width * height; ++j, dataPtr += channels)
				*dataPtr = tmp[j];
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
	snprintf(buf, bufSize, "%s-%i%s", kCompressionFormatNames[m_Format], m_Level, m_SplitChannels ? "-s" : "");
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
		snprintf(buf, bufSize, "meshopt-%s-%i%s", kCompressionFormatNames[m_Format], m_Level, m_SplitChannels ? "-s" : "");
}
