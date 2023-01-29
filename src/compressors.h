#pragma once
#include "compression_helpers.h"
#include <stddef.h>

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
	virtual uint8_t* Compress(int level, const float* data, int width, int height, int channels, size_t& outSize) = 0;
	virtual void Decompress(const uint8_t* cmp, size_t cmpSize, float* data, int width, int height, int channels) = 0;
	virtual void GetLevelRange(int& outMin, int& outMax) const { outMin = 0; outMax = 0; }
	virtual bool ShouldSkipLevel(int level) const { return false; }
	virtual void PrintName(size_t bufSize, char* buf) const = 0;
	virtual uint32_t GetColor() const { return 0; }
	virtual const char* GetShapeString() const = 0;
};

struct GenericCompressor : public Compressor
{
	GenericCompressor(CompressionFormat format, uint32_t filter = kFilterNone) : m_Format(format), m_Filter(filter) {}
	virtual uint8_t* Compress(int level, const float* data, int width, int height, int channels, size_t& outSize);
	virtual void Decompress(const uint8_t* cmp, size_t cmpSize, float* data, int width, int height, int channels);
	virtual void GetLevelRange(int& outMin, int& outMax) const;
	virtual bool ShouldSkipLevel(int level) const { return m_Format == kCompressionZstd && level == 0; }
	virtual void PrintName(size_t bufSize, char* buf) const;
	virtual uint32_t GetColor() const;
	virtual const char* GetShapeString() const;
	CompressionFormat m_Format;
	uint32_t m_Filter;
};

struct MeshOptCompressor : public Compressor
{
	MeshOptCompressor(CompressionFormat format, uint32_t filter = kFilterNone) : m_Format(format), m_Filter(filter) {}
	virtual uint8_t* Compress(int level, const float* data, int width, int height, int channels, size_t& outSize);
	virtual void Decompress(const uint8_t* cmp, size_t cmpSize, float* data, int width, int height, int channels);
	virtual void GetLevelRange(int& outMin, int& outMax) const;
	virtual bool ShouldSkipLevel(int level) const { return m_Format == kCompressionZstd && level == 0; }
	virtual void PrintName(size_t bufSize, char* buf) const;
	virtual uint32_t GetColor() const;
	virtual const char* GetShapeString() const;
	CompressionFormat m_Format;
	uint32_t m_Filter;
};

struct FpzipCompressor : public Compressor
{
	virtual uint8_t* Compress(int level, const float* data, int width, int height, int channels, size_t& outSize);
	virtual void Decompress(const uint8_t* cmp, size_t cmpSize, float* data, int width, int height, int channels);
	virtual void PrintName(size_t bufSize, char* buf) const;
	virtual const char* GetShapeString() const { return "{type:'star', sides:4}"; }
	virtual uint32_t GetColor() const { return 0xdc74ff; } // purple
};

struct ZfpCompressor : public Compressor
{
	virtual uint8_t* Compress(int level, const float* data, int width, int height, int channels, size_t& outSize);
	virtual void Decompress(const uint8_t* cmp, size_t cmpSize, float* data, int width, int height, int channels);
	virtual void PrintName(size_t bufSize, char* buf) const;
	virtual const char* GetShapeString() const { return "{type:'star', sides:5}"; }
	virtual uint32_t GetColor() const { return 0xde5546; } // orange
};

struct NdzipCompressor : public Compressor
{
	virtual uint8_t* Compress(int level, const float* data, int width, int height, int channels, size_t& outSize);
	virtual void Decompress(const uint8_t* cmp, size_t cmpSize, float* data, int width, int height, int channels);
	virtual void PrintName(size_t bufSize, char* buf) const;
	virtual const char* GetShapeString() const { return "{type:'star', sides:6}"; }
	virtual uint32_t GetColor() const { return 0x00bfa7; } // cyan
};

struct StreamVByteCompressor : public Compressor
{
	StreamVByteCompressor(CompressionFormat format, bool delta) : m_Format(format), m_Delta(delta) {}
	virtual uint8_t* Compress(int level, const float* data, int width, int height, int channels, size_t& outSize);
	virtual void Decompress(const uint8_t* cmp, size_t cmpSize, float* data, int width, int height, int channels);
	virtual void GetLevelRange(int& outMin, int& outMax) const;
	virtual bool ShouldSkipLevel(int level) const { return m_Format == kCompressionZstd && level == 0; }
	virtual void PrintName(size_t bufSize, char* buf) const;
	virtual const char* GetShapeString() const { return "'square'"; }
	virtual uint32_t GetColor() const { return 0xffac8d; } // orange
	CompressionFormat m_Format;
	bool m_Delta;
};
