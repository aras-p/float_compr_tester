#include "compressors.h"
#include <stdio.h>
#include <assert.h>

#include <fpzip.h>
#include <zfp.h>
#include "../libs/spdp/spdp_11.h"
#include "../libs/sokol_time.h"

#if BUILD_WITH_NDZIP
#include <ndzip/ndzip.hh>
#endif // #if BUILD_WITH_NDZIP

#include <streamvbyte.h>
#include <streamvbytedelta.h>

#include <string>

#include "simd.h"


static std::vector<int> GetGenericLevelRange(CompressionFormat format)
{
    switch (format)
    {
    case kCompressionZstd:
        //return { -5, -3, -1, 1, 3, 5, 7, 9, 12, 15, 18, 22 };
        //return { -5, -3, -1, 1, 3, 5, 7, 9, 12, 15 }; // comp time under 3s
        return { -5, -1, 1, 5, 9 };
    case kCompressionLZ4:
        //return { -5, -1, 0, 1, 6, 9, 12 };
        //return { -5, -1, 0, 1, 6, 9 }; // comp time under 3s
        return { -5, 0, 1 };
    case kCompressionZlib:
        //return { 1, 3, 5, 6, 7, 9 };
        return { 1, 3, 5, 6, 7 }; // comp time under 3s
    case kCompressionBrotli:
        //return { 0, 1, 2, 4, 5, 6, 9, 10, 11 };
        return { 0, 1, 2, 4, 5 }; // comp time under 3s
    case kCompressionLibdeflate:
        //return { 1, 3, 5, 6, 9, 10, 12 };
        return { 1, 3, 5, 6 }; // comp time under 3s
    case kCompressionOoodleSelkie:
    case kCompressionOoodleMermaid:
    case kCompressionOoodleKraken:
        //return { -4, -3, -2, -1, 1, 2, 3, 4, 5, 6, 7, 8 };
        //return { -4, -3, -2, -1, 1, 2, 3, 4, 5 }; // comp time under 3s
        return { -4, -2, 1, 4 };
    case kCompressionBloscBLZ:
    case kCompressionBloscBLZ_Shuf:
    case kCompressionBloscBLZ_ShufDelta:
        return { 1, 3, 5, 7, 9 };
    case kCompressionBloscLZ4:
    case kCompressionBloscLZ4_Shuf:
    case kCompressionBloscLZ4_ShufDelta:
        return { 0, 1 };
    case kCompressionBloscZstd:
    case kCompressionBloscZstd_Shuf:
    case kCompressionBloscZstd_ShufDelta:
        // blosc levels 1..9 map to zstd levels 1,3,5,7,9,11,13,20,22
        return { 1, 3, 5 };

    default:
        return { 0 };
    }
}

std::vector<int> SpdpCompressor::GetLevels() const { return {0, 3, 6, 9}; }


template<typename T>
static void Split(const T* src, T* dst, int channels, int planeElems)
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
static void UnSplit(const T* src, T* dst, int channels, int planeElems)
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

uint8_t* GenericCompressor::Compress(int level, const float* data, int width, int height, int channels, size_t& outSize)
{
    size_t dataSize = width * height * channels * sizeof(float);
    size_t bound = compress_calc_bound(dataSize, m_Format);
    uint8_t* cmp = new uint8_t[bound];
    outSize = compress_data(data, dataSize, cmp, bound, m_Format, level, channels * sizeof(float));
    return cmp;
}

void GenericCompressor::Decompress(const uint8_t* cmp, size_t cmpSize, float* data, int width, int height, int channels)
{
    size_t dataSize = width * height * channels * sizeof(float);
    decompress_data(cmp, cmpSize, data, dataSize, m_Format);
}

static const char* kCompressionFormatNames[] = {
    "zstd",
    "lz4",
    "zlib",
    "brotli",
    "libdeflate",
    "oselkie",
    "omermaid",
    "okraken",
    "blosc",
    "blosc_lz4",
    "blosc_zstd",
    "blosc-s8",
    "blosc_lz4-s8",
    "blosc_zstd-s8",
    "blosc-s8d",
    "blosc_lz4-s8d",
    "blosc_zstd-s8d",
};
static_assert(sizeof(kCompressionFormatNames) / sizeof(kCompressionFormatNames[0]) == kCompressionCount);

void GenericCompressor::PrintName(size_t bufSize, char* buf) const
{
    snprintf(buf, bufSize, "%s", kCompressionFormatNames[m_Format]);
}

void GenericCompressor::PrintVersion(size_t bufSize, char* buf) const
{
    compressor_get_version(m_Format, bufSize, buf);
}

std::vector<int> GenericCompressor::GetLevels() const
{
    return GetGenericLevelRange(m_Format);
}

static uint8_t* CompressGeneric(CompressionFormat format, int level, uint8_t* data, size_t dataSize, int stride, size_t& outSize)
{
    if (format == kCompressionCount)
    {
        outSize = dataSize;
        return data;
    }
    size_t bound = compress_calc_bound(dataSize, format);
    uint8_t* cmp = new uint8_t[bound + 4];
    *(uint32_t*)cmp = uint32_t(dataSize); // store orig size at start
    outSize = compress_data(data, dataSize, cmp + 4, bound, format, level, stride) + 4;
    delete[] data;
    return cmp;
}

static uint8_t* DecompressGeneric(CompressionFormat format, const uint8_t* cmp, size_t cmpSize, size_t& outSize)
{
    if (format == kCompressionCount)
    {
        outSize = cmpSize;
        return (uint8_t*)cmp;
    }
    uint32_t decSize = *(uint32_t*)cmp; // fetch orig size from start
    uint8_t* decomp = new uint8_t[decSize];
    outSize = decompress_data(cmp + 4, cmpSize - 4, decomp, decSize, format);
    return decomp;
}

uint8_t* MeshOptCompressor::Compress(int level, const float* data, int width, int height, int channels, size_t& outSize)
{
    int stride = channels * sizeof(float);
    size_t dataSize = width * height * channels * sizeof(float);
    int vertexCount = int(dataSize / stride);
    size_t moBound = compress_meshopt_vertex_attribute_bound(vertexCount, stride);
    uint8_t* moCmp = new uint8_t[moBound];
    size_t moSize = compress_meshopt_vertex_attribute(data, vertexCount, stride, moCmp, moBound);
    return CompressGeneric(m_Format, level, moCmp, moSize, channels * sizeof(float), outSize);
}

void MeshOptCompressor::Decompress(const uint8_t* cmp, size_t cmpSize, float* data, int width, int height, int channels)
{
    size_t dataSize = width * height * channels * sizeof(float);

    int stride = channels * sizeof(float);
    int vertexCount = int(dataSize / stride);

    size_t decompSize;
    uint8_t* decomp = DecompressGeneric(m_Format, cmp, cmpSize, decompSize);

    decompress_meshopt_vertex_attribute(decomp, decompSize, vertexCount, stride, data);
    if (decomp != cmp) delete[] decomp;
}

std::vector<int> MeshOptCompressor::GetLevels() const
{
    return GetGenericLevelRange(m_Format);
}

void MeshOptCompressor::PrintName(size_t bufSize, char* buf) const
{
    if (m_Format == kCompressionCount)
        snprintf(buf, bufSize, "meshopt");
    else
        snprintf(buf, bufSize, "meshopt-%s", kCompressionFormatNames[m_Format]);
}

void MeshOptCompressor::PrintVersion(size_t bufSize, char* buf) const
{
    meshopt_get_version(bufSize, buf);
}


uint8_t* FpzipCompressor::Compress(int level, const float* data, int width, int height, int channels, size_t& outSize)
{
    // without split-by-float, fpzip only achieves ~1.5x ratio;
    // with split it gets to 3.8x.
    uint32_t* split = new uint32_t[width * height * channels];
    Split<uint32_t>((const uint32_t*)data, split, channels, width * height);

    size_t dataSize = width * height * channels * sizeof(float);
    size_t bound = dataSize * 2; //@TODO: what's the max compression bound?
    uint8_t* cmp = new uint8_t[bound];
    FPZ* fpz = fpzip_write_to_buffer(cmp, bound);
    fpz->type = FPZIP_TYPE_FLOAT;
    fpz->prec = 0;
    fpz->nx = width;
    fpz->ny = height;
    fpz->nz = 1;
    fpz->nf = channels;
    size_t cmpSize = fpzip_write(fpz, split);
    fpzip_write_close(fpz);
    outSize = cmpSize;
    delete[] split;
    return cmp;
}

void FpzipCompressor::Decompress(const uint8_t* cmp, size_t cmpSize, float* data, int width, int height, int channels)
{
    uint32_t* split = new uint32_t[width * height * channels];
    
    FPZ* fpz = fpzip_read_from_buffer(cmp);
    fpz->type = FPZIP_TYPE_FLOAT;
    fpz->prec = 0;
    fpz->nx = width;
    fpz->ny = height;
    fpz->nz = 1;
    fpz->nf = channels;
    fpzip_read(fpz, split);
    fpzip_read_close(fpz);
    UnSplit<uint32_t>(split, (uint32_t*)data, channels, width * height);
    delete[] split;
}

void FpzipCompressor::PrintName(size_t bufSize, char* buf) const
{
    snprintf(buf, bufSize, "fpzip-ls");
}

void FpzipCompressor::PrintVersion(size_t bufSize, char* buf) const
{
    snprintf(buf, bufSize, "fpz-%i.%i", FPZIP_VERSION_MAJOR, FPZIP_VERSION_MINOR);
}

uint8_t* ZfpCompressor::Compress(int level, const float* data, int width, int height, int channels, size_t& outSize)
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
    snprintf(buf, bufSize, "zfp-ls");
}

void ZfpCompressor::PrintVersion(size_t bufSize, char* buf) const
{
    snprintf(buf, bufSize, "zfp-%i.%i", ZFP_VERSION_MAJOR, ZFP_VERSION_MINOR);
}


uint8_t* SpdpCompressor::Compress(int level, const float* data, int width, int height, int channels, size_t& outSize)
{
    uint32_t* split = new uint32_t[width * height * channels];
    Split<uint32_t>((const uint32_t*)data, split, channels, width * height);

    size_t dataSize = width * height * channels * sizeof(float);
    size_t bound = spdp_compress_bound(dataSize);
    uint8_t* cmp = new uint8_t[bound + 1];
    cmp[0] = uint8_t(level);
    size_t cmpSize = spdp_compress(level, dataSize, (unsigned char*)split, cmp + 1);
    outSize = cmpSize + 1;
    delete[] split;
    return cmp;
}

void SpdpCompressor::Decompress(const uint8_t* cmp, size_t cmpSize, float* data, int width, int height, int channels)
{
    uint32_t* split = new uint32_t[width * height * channels];
    uint8_t level = cmp[0];
    spdp_decompress(level, cmpSize - 1, (byte_t*)cmp + 1, (byte_t*)split);
    UnSplit<uint32_t>(split, (uint32_t*)data, channels, width * height);
    delete[] split;
}

void SpdpCompressor::PrintName(size_t bufSize, char* buf) const
{
    snprintf(buf, bufSize, "spdp");
}

void SpdpCompressor::PrintVersion(size_t bufSize, char* buf) const
{
    snprintf(buf, bufSize, "spdp-1.1");
}


#if BUILD_WITH_NDZIP
uint8_t* NdzipCompressor::Compress(int level, const float* data, int width, int height, int channels, size_t& outSize)
{
    // without s32 split, only achieves 1.2x ratio; with split 2.5x
    uint32_t* split = new uint32_t[width * height * channels];
    Split<uint32_t>((const uint32_t*)data, split, channels, width * height);

    auto compressor = ndzip::make_compressor<float>(2, 1);

    ndzip::extent ext(2);
    ext[0] = width;
    ext[1] = height;
    size_t bound = (4 + ndzip::compressed_length_bound<float>(ext) * 4) * channels;
    uint8_t* cmp = new uint8_t[bound];
    size_t cmpSize = 0;
    for (int ich = 0; ich < channels; ++ich)
    {
        size_t chCmpSize = compressor->compress((const float*)split + ich * width * height, ext, (uint32_t*)(cmp + 4 + cmpSize)) * 4;
        *(uint32_t*)(cmp + cmpSize) = uint32_t(chCmpSize);
        cmpSize += chCmpSize + 4;
    }
    outSize = cmpSize;
    delete[] split;
    return cmp;
}

void NdzipCompressor::Decompress(const uint8_t* cmp, size_t cmpSize, float* data, int width, int height, int channels)
{
    auto decompressor = ndzip::make_decompressor<float>(2, 1);
    ndzip::extent ext(2);
    ext[0] = width;
    ext[1] = height;
    uint32_t* split = new uint32_t[width * height * channels];
    for (int ich = 0; ich < channels; ++ich)
    {
        uint32_t chCmpSize = *(const uint32_t*)cmp;
        cmp += 4;
        decompressor->decompress((const uint32_t*)cmp, (float*)split + ich * width * height, ext);
        cmp += chCmpSize;
    }
    UnSplit<uint32_t>(split, (uint32_t*)data, channels, width * height);
    delete[] split;
}

void NdzipCompressor::PrintName(size_t bufSize, char* buf) const
{
    snprintf(buf, bufSize, "ndzip");
}
void NdzipCompressor::PrintVersion(size_t bufSize, char* buf) const
{
    snprintf(buf, bufSize, "ndzip-2022.07");
}
#endif // #if BUILD_WITH_NDZIP


// notes:
// svbyte followed by general purpose compressor: nah, just loses to only the general purpose one on both ratio & perf.
// svbyte_delta loses to general svbyte, as well as with an additional compressor
// svbyte_s32 does not affect general svbyte.
// BUT! svbyte_s32_delta is actually interesting.
uint8_t* StreamVByteCompressor::Compress(int level, const float* data, int width, int height, int channels, size_t& outSize)
{
    uint32_t* split = nullptr;
    if (m_Split32)
    {
        split = new uint32_t[width * height * channels];
        Split<uint32_t>((const uint32_t*)data, split, channels, width * height);
    }

    uint32_t dataElems = width * height * channels;
    size_t bound = streamvbyte_max_compressedbytes(dataElems);
    uint8_t* cmp = new uint8_t[bound];
    size_t cmpSize = 0;
    if (m_Delta)
        cmpSize = streamvbyte_delta_encode(split ? split : (const uint32_t*)data, dataElems, cmp, 0);
    else
        cmpSize = streamvbyte_encode(split ? split : (const uint32_t*)data, dataElems, cmp);
    delete[] split;

    return CompressGeneric(m_Format, level, cmp, cmpSize, channels * sizeof(float), outSize);
}

void StreamVByteCompressor::Decompress(const uint8_t* cmp, size_t cmpSize, float* data, int width, int height, int channels)
{
    size_t decompSize;
    uint8_t* decomp = DecompressGeneric(m_Format, cmp, cmpSize, decompSize);

    uint32_t* split = nullptr;
    if (m_Split32)
    {
        split = new uint32_t[width * height * channels];
    }
    uint32_t dataElems = width * height * channels;
    if (m_Delta)
        streamvbyte_delta_decode(decomp, split ? split : (uint32_t*)data, dataElems, 0);
    else
        streamvbyte_decode(decomp, split ? split : (uint32_t*)data, dataElems);
    if (split)
    {
        UnSplit<uint32_t>(split, (uint32_t*)data, channels, width * height);
        delete[] split;
    }
    if (decomp != cmp) delete[] decomp;
}

std::vector<int> StreamVByteCompressor::GetLevels() const
{
    return GetGenericLevelRange(m_Format);
}

void StreamVByteCompressor::PrintName(size_t bufSize, char* buf) const
{
    if (m_Format == kCompressionCount)
        snprintf(buf, bufSize, "svbyte%s%s", m_Split32 ? "_s32" : "", m_Delta ? "_d" : "");
    else
        snprintf(buf, bufSize, "svbyte-%s%s%s", kCompressionFormatNames[m_Format], m_Split32 ? "_s32" : "", m_Delta ? "_d" : "");
}

void StreamVByteCompressor::PrintVersion(size_t bufSize, char* buf) const
{
    snprintf(buf, bufSize, "svbyte-2023.02");
}
