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
	enum Flags {
		kFlagNone = 0,
		kFlagSplitFloats = (1<<0),
		kFlagSplitBytes = (1<<1),
	};
	GenericCompressor(CompressionFormat format, int level, uint32_t flags = kFlagNone) : m_Format(format), m_Level(level), m_Flags(flags) {}
	virtual uint8_t* Compress(const float* data, int width, int height, int channels, size_t& outSize);
	virtual void Decompress(const uint8_t* cmp, size_t cmpSize, float* data, int width, int height, int channels);
	virtual void PrintName(size_t bufSize, char* buf) const;
	CompressionFormat m_Format;
	int m_Level;
	uint32_t m_Flags;
};

struct MeshOptCompressor : public Compressor
{
	MeshOptCompressor(CompressionFormat format, int level) : m_Format(format), m_Level(level) {}
	virtual uint8_t* Compress(const float* data, int width, int height, int channels, size_t& outSize);
	virtual void Decompress(const uint8_t* cmp, size_t cmpSize, float* data, int width, int height, int channels);
	virtual void PrintName(size_t bufSize, char* buf) const;
	CompressionFormat m_Format;
	int m_Level;
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
