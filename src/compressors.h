#pragma once
#include "compression_helpers.h"
#include <stddef.h>
#include <vector>

struct Compressor
{
	virtual ~Compressor() {}
	virtual uint8_t* Compress(int level, const float* data, int width, int height, int channels, size_t& outSize) = 0;
	virtual void Decompress(const uint8_t* cmp, size_t cmpSize, float* data, int width, int height, int channels) = 0;
	virtual std::vector<int> GetLevels() const { return {0}; }
	virtual void PrintName(size_t bufSize, char* buf) const = 0;
	virtual void PrintVersion(size_t bufSize, char* buf) const = 0;
};

struct GenericCompressor : public Compressor
{
	GenericCompressor(CompressionFormat format) : m_Format(format) {}
	virtual uint8_t* Compress(int level, const float* data, int width, int height, int channels, size_t& outSize);
	virtual void Decompress(const uint8_t* cmp, size_t cmpSize, float* data, int width, int height, int channels);
	virtual std::vector<int> GetLevels() const;
	virtual void PrintName(size_t bufSize, char* buf) const;
	virtual void PrintVersion(size_t bufSize, char* buf) const;
	CompressionFormat m_Format;
};

struct MeshOptCompressor : public Compressor
{
	MeshOptCompressor(CompressionFormat format) : m_Format(format) {}
	virtual uint8_t* Compress(int level, const float* data, int width, int height, int channels, size_t& outSize);
	virtual void Decompress(const uint8_t* cmp, size_t cmpSize, float* data, int width, int height, int channels);
	virtual std::vector<int> GetLevels() const;
	virtual void PrintName(size_t bufSize, char* buf) const;
	virtual void PrintVersion(size_t bufSize, char* buf) const;
	CompressionFormat m_Format;
};

struct FpzipCompressor : public Compressor
{
	virtual uint8_t* Compress(int level, const float* data, int width, int height, int channels, size_t& outSize);
	virtual void Decompress(const uint8_t* cmp, size_t cmpSize, float* data, int width, int height, int channels);
	virtual void PrintName(size_t bufSize, char* buf) const;
    virtual void PrintVersion(size_t bufSize, char* buf) const;
};

struct ZfpCompressor : public Compressor
{
	virtual uint8_t* Compress(int level, const float* data, int width, int height, int channels, size_t& outSize);
	virtual void Decompress(const uint8_t* cmp, size_t cmpSize, float* data, int width, int height, int channels);
	virtual void PrintName(size_t bufSize, char* buf) const;
    virtual void PrintVersion(size_t bufSize, char* buf) const;
};

struct SpdpCompressor : public Compressor
{
    virtual uint8_t* Compress(int level, const float* data, int width, int height, int channels, size_t& outSize);
    virtual void Decompress(const uint8_t* cmp, size_t cmpSize, float* data, int width, int height, int channels);
    virtual void PrintName(size_t bufSize, char* buf) const;
    virtual void PrintVersion(size_t bufSize, char* buf) const;
    virtual std::vector<int> GetLevels() const;
};

#if BUILD_WITH_NDZIP
struct NdzipCompressor : public Compressor
{
	virtual uint8_t* Compress(int level, const float* data, int width, int height, int channels, size_t& outSize);
	virtual void Decompress(const uint8_t* cmp, size_t cmpSize, float* data, int width, int height, int channels);
	virtual void PrintName(size_t bufSize, char* buf) const;
    virtual void PrintVersion(size_t bufSize, char* buf) const;
};
#endif // #if BUILD_WITH_NDZIP

struct StreamVByteCompressor : public Compressor
{
	StreamVByteCompressor(CompressionFormat format, bool split32, bool delta) : m_Format(format), m_Split32(split32), m_Delta(delta) {}
	virtual uint8_t* Compress(int level, const float* data, int width, int height, int channels, size_t& outSize);
	virtual void Decompress(const uint8_t* cmp, size_t cmpSize, float* data, int width, int height, int channels);
	virtual std::vector<int> GetLevels() const;
	virtual void PrintName(size_t bufSize, char* buf) const;
    virtual void PrintVersion(size_t bufSize, char* buf) const;
	CompressionFormat m_Format;
	bool m_Split32;
	bool m_Delta;
};
