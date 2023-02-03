#pragma once
#include "compression_helpers.h"
#include <stddef.h>
#include <vector>

enum Filter {
	kFilterNone = 0,
	kFilterSplit32 = (1 << 0),
	kFilterSplit8 = (1 << 1),
	kFilterBitShuffle = (1 << 2),
	kFilterDeltaDiff = (1 << 3),
	kFilterDeltaXor = (1 << 4),
	kFilterRot1 = (1 << 5),
};

struct Compressor
{
	virtual ~Compressor() {}
	virtual uint8_t* Compress(int level, const float* data, int width, int height, int channels, size_t& outSize) = 0;
	virtual void Decompress(const uint8_t* cmp, size_t cmpSize, float* data, int width, int height, int channels) = 0;
	virtual std::vector<int> GetLevels() const { return {0}; }
	virtual void PrintName(size_t bufSize, char* buf) const = 0;
	virtual void PrintVersion(size_t bufSize, char* buf) const = 0;
	virtual uint32_t GetColor() const { return 0; }
	virtual const char* GetShapeString() const = 0;
};

struct GenericCompressor : public Compressor
{
	GenericCompressor(CompressionFormat format, uint32_t filter = kFilterNone) : m_Format(format), m_Filter(filter) {}
	virtual uint8_t* Compress(int level, const float* data, int width, int height, int channels, size_t& outSize);
	virtual void Decompress(const uint8_t* cmp, size_t cmpSize, float* data, int width, int height, int channels);
	virtual std::vector<int> GetLevels() const;
	virtual void PrintName(size_t bufSize, char* buf) const;
	virtual void PrintVersion(size_t bufSize, char* buf) const;
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
	virtual std::vector<int> GetLevels() const;
	virtual void PrintName(size_t bufSize, char* buf) const;
	virtual void PrintVersion(size_t bufSize, char* buf) const;
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
    virtual void PrintVersion(size_t bufSize, char* buf) const;
    virtual const char* GetShapeString() const;
	virtual uint32_t GetColor() const { return 0x7e1b1b; } // dark red
};

struct ZfpCompressor : public Compressor
{
	virtual uint8_t* Compress(int level, const float* data, int width, int height, int channels, size_t& outSize);
	virtual void Decompress(const uint8_t* cmp, size_t cmpSize, float* data, int width, int height, int channels);
	virtual void PrintName(size_t bufSize, char* buf) const;
    virtual void PrintVersion(size_t bufSize, char* buf) const;
    virtual const char* GetShapeString() const;
	virtual uint32_t GetColor() const { return 0xd74242; } // mid red
};

struct SpdpCompressor : public Compressor
{
    virtual uint8_t* Compress(int level, const float* data, int width, int height, int channels, size_t& outSize);
    virtual void Decompress(const uint8_t* cmp, size_t cmpSize, float* data, int width, int height, int channels);
    virtual void PrintName(size_t bufSize, char* buf) const;
    virtual void PrintVersion(size_t bufSize, char* buf) const;
    virtual const char* GetShapeString() const;
    virtual std::vector<int> GetLevels() const;
    virtual uint32_t GetColor() const { return 0xe06c6c; } // light red
};

#if BUILD_WITH_NDZIP
struct NdzipCompressor : public Compressor
{
	virtual uint8_t* Compress(int level, const float* data, int width, int height, int channels, size_t& outSize);
	virtual void Decompress(const uint8_t* cmp, size_t cmpSize, float* data, int width, int height, int channels);
	virtual void PrintName(size_t bufSize, char* buf) const;
    virtual void PrintVersion(size_t bufSize, char* buf) const;
	virtual const char* GetShapeString() const;
	virtual uint32_t GetColor() const { return 0xf0a820; } // light yellow
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
	virtual const char* GetShapeString() const;
	virtual uint32_t GetColor() const { return 0xff6454; } // orange
	CompressionFormat m_Format;
	bool m_Split32;
	bool m_Delta;
};
