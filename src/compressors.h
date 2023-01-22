#pragma once
#include "compression_helpers.h"


struct Compressor
{
	virtual ~Compressor() {}
	virtual uint8_t* Compress(const float* data, int width, int height, int channels, size_t& outSize) = 0;
	virtual void Decompress(const uint8_t* cmp, size_t cmpSize, float* data, int width, int height, int channels) = 0;
	virtual void PrintName(size_t bufSize, char* buf) const = 0;
};

struct GenericCompressor : public Compressor
{
	GenericCompressor(CompressionFormat format, int level, bool splitChannels) : m_Format(format), m_Level(level), m_SplitChannels(splitChannels) {}
	virtual uint8_t* Compress(const float* data, int width, int height, int channels, size_t& outSize);
	virtual void Decompress(const uint8_t* cmp, size_t cmpSize, float* data, int width, int height, int channels);
	virtual void PrintName(size_t bufSize, char* buf) const;
	CompressionFormat m_Format;
	int m_Level;
	bool m_SplitChannels;
};

struct MeshOptCompressor : public Compressor
{
	MeshOptCompressor(CompressionFormat format, int level, bool splitChannels) : m_Format(format), m_Level(level), m_SplitChannels(splitChannels) {}
	virtual uint8_t* Compress(const float* data, int width, int height, int channels, size_t& outSize);
	virtual void Decompress(const uint8_t* cmp, size_t cmpSize, float* data, int width, int height, int channels);
	virtual void PrintName(size_t bufSize, char* buf) const;
	CompressionFormat m_Format;
	int m_Level;
	bool m_SplitChannels;
};
