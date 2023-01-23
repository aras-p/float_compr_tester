#pragma once
#include "compression_helpers.h"

enum Filter {
	kFilterNone = 0,
	kFilterSplitFloats = (1 << 0),
	kFilterSplitBytes = (1 << 1),
	kFilterDeltaDiff = (1 << 2),
	kFilterDeltaXor = (1 << 3),
};

struct Compressor
{
	virtual ~Compressor() {}
	virtual uint8_t* Compress(const float* data, int width, int height, int channels, size_t& outSize) = 0;
	virtual void Decompress(const uint8_t* cmp, size_t cmpSize, float* data, int width, int height, int channels) = 0;
	virtual void PrintName(size_t bufSize, char* buf) const = 0;
};

struct GenericCompressor : public Compressor
{
	GenericCompressor(CompressionFormat format, int level, uint32_t filter = kFilterNone) : m_Format(format), m_Level(level), m_Filter(filter) {}
	virtual uint8_t* Compress(const float* data, int width, int height, int channels, size_t& outSize);
	virtual void Decompress(const uint8_t* cmp, size_t cmpSize, float* data, int width, int height, int channels);
	virtual void PrintName(size_t bufSize, char* buf) const;
	CompressionFormat m_Format;
	int m_Level;
	uint32_t m_Filter;
};

struct MeshOptCompressor : public Compressor
{
	MeshOptCompressor(CompressionFormat format, int level, uint32_t filter = kFilterNone) : m_Format(format), m_Level(level), m_Filter(filter) {}
	virtual uint8_t* Compress(const float* data, int width, int height, int channels, size_t& outSize);
	virtual void Decompress(const uint8_t* cmp, size_t cmpSize, float* data, int width, int height, int channels);
	virtual void PrintName(size_t bufSize, char* buf) const;
	CompressionFormat m_Format;
	int m_Level;
	uint32_t m_Filter;
};

struct FpzipCompressor : public Compressor
{
	virtual uint8_t* Compress(const float* data, int width, int height, int channels, size_t& outSize);
	virtual void Decompress(const uint8_t* cmp, size_t cmpSize, float* data, int width, int height, int channels);
	virtual void PrintName(size_t bufSize, char* buf) const;
};

struct ZfpCompressor : public Compressor
{
	virtual uint8_t* Compress(const float* data, int width, int height, int channels, size_t& outSize);
	virtual void Decompress(const uint8_t* cmp, size_t cmpSize, float* data, int width, int height, int channels);
	virtual void PrintName(size_t bufSize, char* buf) const;
};

struct NdzipCompressor : public Compressor
{
	virtual uint8_t* Compress(const float* data, int width, int height, int channels, size_t& outSize);
	virtual void Decompress(const uint8_t* cmp, size_t cmpSize, float* data, int width, int height, int channels);
	virtual void PrintName(size_t bufSize, char* buf) const;
};
