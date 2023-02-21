#include "compressors.h"
#include <stdio.h>
#include <assert.h>

#include <fpzip.h>
#include <zfp.h>
#include "../libs/bitshuffle/src/bitshuffle_core.h"
#include "../libs/spdp/spdp_11.h"
#include "../libs/sokol_time.h"

uint64_t g_time_filter, g_time_unfilter;
int g_count_filter, g_count_unfilter;

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
    default:
        return { 0 };
    }
}

std::vector<int> SpdpCompressor::GetLevels() const { return {0, 3, 6, 9}; }


template<typename T>
static void EncodeDeltaDif(T* data, size_t dataElems)
{
    T prev = 0;
    for (size_t i = 0; i < dataElems; ++i)
    {
        T v = *data;
        *data = v - prev;
        prev = v;
        ++data;
    }
}

template<typename T>
static void DecodeDeltaDif(T* data, size_t dataElems)
{
    T prev = 0;
    for (size_t i = 0; i < dataElems; ++i)
    {
        T v = *data;
        v = prev + v;
        *data = v;
        prev = v;
        ++data;
    }
}

template<typename T>
static void EncodeDeltaXor(T* data, size_t dataElems)
{
    T prev = 0;
    for (size_t i = 0; i < dataElems; ++i)
    {
        T v = *data;
        *data = v ^ prev;
        prev = v;
        ++data;
    }
}

template<typename T>
static void DecodeDeltaXor(T* data, size_t dataElems)
{
    T prev = 0;
    for (size_t i = 0; i < dataElems; ++i)
    {
        T v = *data;
        v = prev ^ v;
        *data = v;
        prev = v;
        ++data;
    }
}

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

static void Split8Delta(const uint8_t* src, uint8_t* dst, int channels, size_t planeElems)
{
#if 1 // B case
    uint8_t prev = 0;
    for (int ich = 0; ich < channels; ++ich)
    {
        const uint8_t* srcPtr = src + ich;
        for (size_t ip = 0; ip < planeElems; ++ip)
        {
            uint8_t v = *srcPtr;
            *dst = v - prev;
            prev = v;
            srcPtr += channels;
            dst += 1;
        }
    }
#endif
#if 0 // D case
    uint8_t prev = 0;
    for (int ich = 0; ich < channels; ++ich)
    {
        const uint8_t* srcPtr = src + ich;
        size_t ip = 0;

        // SIMD loop, 16 bytes at a time
        Bytes16 prev16 = SimdSet1(prev);
        for (; ip < planeElems / 16; ++ip)
        {
            // gather 16 bytes from source data
            Bytes16 v = SimdZero();
            v = SimdSetLane<0>(v, *srcPtr); srcPtr += channels;
            v = SimdSetLane<1>(v, *srcPtr); srcPtr += channels;
            v = SimdSetLane<2>(v, *srcPtr); srcPtr += channels;
            v = SimdSetLane<3>(v, *srcPtr); srcPtr += channels;
            v = SimdSetLane<4>(v, *srcPtr); srcPtr += channels;
            v = SimdSetLane<5>(v, *srcPtr); srcPtr += channels;
            v = SimdSetLane<6>(v, *srcPtr); srcPtr += channels;
            v = SimdSetLane<7>(v, *srcPtr); srcPtr += channels;
            v = SimdSetLane<8>(v, *srcPtr); srcPtr += channels;
            v = SimdSetLane<9>(v, *srcPtr); srcPtr += channels;
            v = SimdSetLane<10>(v, *srcPtr); srcPtr += channels;
            v = SimdSetLane<11>(v, *srcPtr); srcPtr += channels;
            v = SimdSetLane<12>(v, *srcPtr); srcPtr += channels;
            v = SimdSetLane<13>(v, *srcPtr); srcPtr += channels;
            v = SimdSetLane<14>(v, *srcPtr); srcPtr += channels;
            v = SimdSetLane<15>(v, *srcPtr); srcPtr += channels;
            // delta from previous
            Bytes16 delta = SimdSub(v, SimdConcat<15>(v, prev16));
            SimdStore(dst, delta);
            prev16 = v;
            dst += 16;
        }
        prev = SimdGetLane<15>(prev16);

        // any trailing leftover
        for (ip = ip * 16; ip < planeElems; ++ip)
        {
            uint8_t v = *srcPtr;
            *dst = v - prev;
            prev = v;

            srcPtr += channels;
            dst += 1;
        }
    }
#endif
}

static void UnSplit8Delta(uint8_t* src, uint8_t* dst, int channels, size_t planeElems)
{
#if 1
    // B case
    uint8_t prev = 0;
    for (int ich = 0; ich < channels; ++ich)
    {
        uint8_t* dstPtr = dst + ich;
        for (size_t ip = 0; ip < planeElems; ++ip)
        {
            uint8_t v = *src + prev;
            prev = v;
            *dstPtr = v;
            src += 1;
            dstPtr += channels;
        }
    }
#endif
#if 0
    // "d" case: combined delta+unsplit; SIMD prefix sum delta, unrolled scattered writes into destination
    uint8_t prev = 0;
    for (int ich = 0; ich < channels; ++ich)
    {
        uint8_t* dstPtr = dst + ich;
        size_t ip = 0;

        // SIMD loop, 16 bytes at a time
        Bytes16 prev16 = SimdSet1(prev);
        Bytes16 hibyte = SimdSet1(15);
        for (; ip < planeElems / 16; ++ip)
        {
            // load 16 bytes of filtered data
            Bytes16 v = SimdLoad(src);
            // un-delta via prefix sum
            prev16 = SimdAdd(SimdPrefixSum(v), SimdShuffle(prev16, hibyte));
            // scattered write into destination
            *dstPtr = SimdGetLane<0>(prev16); dstPtr += channels;
            *dstPtr = SimdGetLane<1>(prev16); dstPtr += channels;
            *dstPtr = SimdGetLane<2>(prev16); dstPtr += channels;
            *dstPtr = SimdGetLane<3>(prev16); dstPtr += channels;
            *dstPtr = SimdGetLane<4>(prev16); dstPtr += channels;
            *dstPtr = SimdGetLane<5>(prev16); dstPtr += channels;
            *dstPtr = SimdGetLane<6>(prev16); dstPtr += channels;
            *dstPtr = SimdGetLane<7>(prev16); dstPtr += channels;
            *dstPtr = SimdGetLane<8>(prev16); dstPtr += channels;
            *dstPtr = SimdGetLane<9>(prev16); dstPtr += channels;
            *dstPtr = SimdGetLane<10>(prev16); dstPtr += channels;
            *dstPtr = SimdGetLane<11>(prev16); dstPtr += channels;
            *dstPtr = SimdGetLane<12>(prev16); dstPtr += channels;
            *dstPtr = SimdGetLane<13>(prev16); dstPtr += channels;
            *dstPtr = SimdGetLane<14>(prev16); dstPtr += channels;
            *dstPtr = SimdGetLane<15>(prev16); dstPtr += channels;
            src += 16;
        }
        prev = SimdGetLane<15>(prev16);

        // any trailing leftover
        for (ip = ip * 16; ip < planeElems; ++ip)
        {
            uint8_t v = *src + prev;
            prev = v;
            *dstPtr = v;
            src += 1;
            dstPtr += channels;
        }
    }
#endif
}

// memcpy: winvs 3.6ms
// part 6 B: winvs 20.1ms ratio 3.945x
// split 2M, part 6 B: winvs 13.2ms ratio 3.939x
// K: scalar write (Part 6B), w/ K decoder: mac 11.4
// L: special 16ch case: winvs 7.4 mac 8.4

// https://fgiesen.wordpress.com/2013/07/09/simd-transposes-1/ and https://fgiesen.wordpress.com/2013/08/29/simd-transposes-2/
static void EvenOddInterleave16(const Bytes16* a, Bytes16* b)
{
    int bidx = 0;
    for (int i = 0; i < 8; ++i)
    {
        b[bidx] = SimdInterleaveL(a[i], a[i + 8]); bidx++;
        b[bidx] = SimdInterleaveR(a[i], a[i + 8]); bidx++;
    }
}

static void Transpose16x16(const Bytes16* a, Bytes16* b)
{
    Bytes16 tmp1[16], tmp2[16];
    EvenOddInterleave16((const Bytes16*)a, tmp1);
    EvenOddInterleave16(tmp1, tmp2);
    EvenOddInterleave16(tmp2, tmp1);
    EvenOddInterleave16(tmp1, (Bytes16*)b);
}

void TestFilter(const uint8_t* src, uint8_t* dst, int channels, size_t dataElems)
{
#if 0
    Split8Delta(src, dst, channels, dataElems);
#endif

#if 1
    // K: initial seq write (B from Part6), scalar, w/ K decoding: full test winvs 1451.1 mac 674.8
    for (int ich = 0; ich < channels; ++ich)
    {
        uint8_t prev = 0;
        const uint8_t* srcPtr = src + ich;
        for (size_t ip = 0; ip < dataElems; ++ip)
        {
            uint8_t v = *srcPtr;
            *dst = v - prev;
            prev = v;
            srcPtr += channels;
            dst += 1;
        }
    }
#endif

#if 0
    // L: special case for 16ch
    // winvs 7.4, full test 1395.2
    // mac   8.4, full test  661.4
    if (channels == 16)
    {
        int64_t ip = 0;
        Bytes16 chdata[16];
        Bytes16 xposed[16];
        Bytes16 prev16 = SimdZero();
        uint8_t* dstPtr = dst;
        // simd loop
        for (; ip < int64_t(dataElems) - 15; ip += 16)
        {
            for (int i = 0; i < 16; ++i)
            {
                Bytes16 v = SimdLoad(src); src += 16;
                chdata[i] = SimdSub(v, prev16);
                prev16 = v;
            }
            Transpose16x16(chdata, xposed);
            for (int i = 0; i < 16; ++i)
            {
                SimdStore(dstPtr + i * dataElems, xposed[i]);
            }
            dstPtr += 16;
        }
        // any trailing leftover
        alignas(16) uint8_t prev[16];
        SimdStoreA(prev, prev16);
        for (int ich = 0; ich < 16; ++ich)
        {
            dstPtr = dst + ich * dataElems + ip;
            const uint8_t* srcPtr = src + ich;
            for (size_t ii = ip; ii < dataElems; ++ii)
            {
                uint8_t v = *srcPtr;
                *dstPtr = v - prev[ich];
                prev[ich] = v;
                srcPtr += channels;
                dstPtr += 1;
            }
        }
    }
    else
    {
        // generic scalar
        for (int ich = 0; ich < channels; ++ich)
        {
            uint8_t prev = 0;
            const uint8_t* srcPtr = src + ich;
            for (size_t ip = 0; ip < dataElems; ++ip)
            {
                uint8_t v = *srcPtr;
                *dst = v - prev;
                prev = v;
                srcPtr += channels;
                dst += 1;
            }
        }
    }
#endif
}

// memcpy: winvs 2.9ms
// part 6 B: winvs 21.5 (ratio 3.945x)
// part 6 D: winvs 18.8
// part 6 B + split 2M: winvs 9.1 (ratio 3.939x)
// G: seq write, scalar: winvs 32.1 mac 25.2
// H: seq write, ch=16 path, read   1b from streams: winvs 15.7
//    seq write, ch=16 path, read  16b from streams: winvs  8.6
//    seq write, ch=16 path, read  16b from streams, simd transpose: winvs 7.2
// I: seq write, ch=16 path, read 256b from streams, simd transpose: winvs 7.2 mac 5.2
// J: fetch+interleave groups of 4 channels to stack; then interleave+sum+store groups of 4 ch. winvs 6.8 mac 3.7
// K: semi-unify 16-ch and general paths of J. winvs 4.7 mac 3.7
void TestUnFilter(const uint8_t* src, uint8_t* dst, int channels, size_t dataElems)
{
    if ((channels % 4) != 0) // should never happen; our data is floats so channels will always be multiple of 4
    {
        assert(false);
        return;
    }

#if 0
    UnSplit8Delta((uint8_t*)src, dst, channels, dataElems);
#endif

#if 0
    // G: sequential write into dst; scattered read from all streams (no 2M split): winvs 33.6 mac 25.2
    const size_t kMaxChannels = 64;
    uint8_t prev[kMaxChannels] = {};
    uint8_t* dstPtr = dst;
    for (size_t ip = 0; ip < dataElems; ++ip)
    {
        const uint8_t* srcPtr = src + ip;
        for (int ich = 0; ich < channels; ++ich)
        {
            uint8_t v = *srcPtr + prev[ich];
            prev[ich] = v;
            *dstPtr = v;
            srcPtr += dataElems;
            dstPtr += 1;
        }
    }
#endif

#if 1
    // K:
    // winvs    16 4.6, 64 4.7, 256 4.7, 512 4.6, 1024 4.6, 2048 4.7, 4096 4.7
    // winclang 16 4.6          256 4.5           1024 4.7
    // mac                      256 3.7
    //
    // full test:
    // winvs    16 424.4, 64 423.7, 256 415.7
    // mac                          256 375.5

    const int kChunkBytes = 256;
    const int kChunkSimdSize = kChunkBytes / 16;
    const int kMaxChannels = 64;
    static_assert(kMaxChannels >= 16, "max channels can't be lower than simd width");
    static_assert((kChunkBytes % 16) == 0, "chunk bytes needs to be multiple of simd width");
    uint8_t* dstPtr = dst;
    int64_t ip = 0;
    alignas(16) uint8_t prev[kMaxChannels] = {};
    Bytes16 prev16 = SimdZero();
    for (; ip < int64_t(dataElems) - (kChunkBytes - 1); ip += kChunkBytes)
    {
        // read chunk of bytes from each channel
        Bytes16 chdata[kMaxChannels][kChunkSimdSize];
        const uint8_t* srcPtr = src + ip;
        // fetch data for groups of 4 channels, interleave
        // so that first in chdata is (a0b0c0d0 a1b1c1d1 a2b2c2d2 a3b3c3d3) etc.
        for (int ich = 0; ich < channels; ich += 4)
        {
            for (int item = 0; item < kChunkSimdSize; ++item)
            {
                Bytes16 d0 = SimdLoad(((const Bytes16*)(srcPtr)) + item);
                Bytes16 d1 = SimdLoad(((const Bytes16*)(srcPtr + dataElems)) + item);
                Bytes16 d2 = SimdLoad(((const Bytes16*)(srcPtr + dataElems * 2)) + item);
                Bytes16 d3 = SimdLoad(((const Bytes16*)(srcPtr + dataElems * 3)) + item);
                // interleaves like from https://fgiesen.wordpress.com/2013/08/29/simd-transposes-2/
                Bytes16 e0 = SimdInterleaveL(d0, d2); Bytes16 e1 = SimdInterleaveR(d0, d2);
                Bytes16 e2 = SimdInterleaveL(d1, d3); Bytes16 e3 = SimdInterleaveR(d1, d3);
                Bytes16 f0 = SimdInterleaveL(e0, e2); Bytes16 f1 = SimdInterleaveR(e0, e2);
                Bytes16 f2 = SimdInterleaveL(e1, e3); Bytes16 f3 = SimdInterleaveR(e1, e3);
                chdata[ich + 0][item] = f0;
                chdata[ich + 1][item] = f1;
                chdata[ich + 2][item] = f2;
                chdata[ich + 3][item] = f3;
            }
            srcPtr += 4 * dataElems;
        }

        if (channels == 16 && false)
        {
            // channels == 16 case is much simpler
            // read groups of data from stack, interleave, accumulate sum, store
            for (int item = 0; item < kChunkSimdSize; ++item)
            {
                for (int chgrp = 0; chgrp < 4; ++chgrp)
                {
                    Bytes16 a0 = chdata[chgrp][item];
                    Bytes16 a1 = chdata[chgrp + 4][item];
                    Bytes16 a2 = chdata[chgrp + 8][item];
                    Bytes16 a3 = chdata[chgrp + 12][item];
                    // now we want a 4x4 as-uint matrix transpose
                    Bytes16 b0 = SimdInterleave4L(a0, a2); Bytes16 b1 = SimdInterleave4R(a0, a2);
                    Bytes16 b2 = SimdInterleave4L(a1, a3); Bytes16 b3 = SimdInterleave4R(a1, a3);
                    Bytes16 c0 = SimdInterleave4L(b0, b2); Bytes16 c1 = SimdInterleave4R(b0, b2);
                    Bytes16 c2 = SimdInterleave4L(b1, b3); Bytes16 c3 = SimdInterleave4R(b1, b3);
                    // c0..c3 is what we should do accumulate sum on, and store
                    prev16 = SimdAdd(prev16, c0); SimdStore(dstPtr, prev16); dstPtr += 16;
                    prev16 = SimdAdd(prev16, c1); SimdStore(dstPtr, prev16); dstPtr += 16;
                    prev16 = SimdAdd(prev16, c2); SimdStore(dstPtr, prev16); dstPtr += 16;
                    prev16 = SimdAdd(prev16, c3); SimdStore(dstPtr, prev16); dstPtr += 16;
                }
            }
        }
        else
        {
            // general case: interleave data
            uint8_t cur[kMaxChannels * kChunkBytes];
            for (int ib = 0; ib < kChunkBytes; ++ib)
            {
                uint8_t* curPtr = cur + ib * kMaxChannels;
                for (int ich = 0; ich < channels; ich += 4)
                {
                    *(uint32_t*)curPtr = *(const uint32_t*)(((const uint8_t*)chdata) + ich * kChunkBytes + ib * 4);
                    curPtr += 4;
                }
            }
            // accumulate sum and store
            // the row address we want from "cur" is interleaved in a funky way due to 4-channels data fetch above.
            for (int item = 0; item < kChunkSimdSize; ++item)
            {
                for (int chgrp = 0; chgrp < 4; ++chgrp)
                {
                    uint8_t* curPtrStart = cur + (chgrp * kChunkSimdSize + item) * 4 * kMaxChannels;
                    for (int ib = 0; ib < 4; ++ib)
                    {
                        uint8_t* curPtr = curPtrStart;
                        // accumulate sum w/ SIMD
                        for (int ich = 0; ich < channels; ich += 16)
                        {
                            Bytes16 v = SimdAdd(SimdLoadA(&prev[ich]), SimdLoad(curPtr));
                            SimdStoreA(&prev[ich], v);
                            SimdStore(curPtr, v);
                            curPtr += 16;
                        }
                        // store as multiple of 4 bytes
                        // equivalent of memcpy, but we know we're multiple of 4
                        // bytes size always. msvc replaces the loop with actual
                        // memcpy call though :)
                        curPtr = curPtrStart;
                        for (int ich = 0; ich < channels; ich += 4)
                        {
                            *(uint32_t*)dstPtr = *(const uint32_t*)curPtr;
                            dstPtr += 4;
                            curPtr += 4;
                        }
                        curPtrStart += kMaxChannels;
                    }
                }
            }
        }
    }

    // scalar loop for any non-multiple-of-16 remainder
    if (channels == 16 && false)
    {
        for (; ip < int64_t(dataElems); ip++)
        {
            // read from each channel
            alignas(16) uint8_t chdata[16];
            const uint8_t* srcPtr = src + ip;
            for (int ich = 0; ich < 16; ++ich)
            {
                chdata[ich] = *srcPtr;
                srcPtr += dataElems;
            }
            // accumulate sum and write into destination
            prev16 = SimdAdd(prev16, SimdLoadA(chdata));
            SimdStore(dstPtr, prev16);
            dstPtr += 16;
        }
    }
    else
    {
        for (; ip < int64_t(dataElems); ip++)
        {
            const uint8_t* srcPtr = src + ip;
            for (int ich = 0; ich < channels; ++ich)
            {
                uint8_t v = *srcPtr + prev[ich];
                prev[ich] = v;
                *dstPtr = v;
                srcPtr += dataElems;
                dstPtr += 1;
            }
        }
    }
#endif
}

// WinVS
// memcpy:                   cmp  3.6ms dec  2.9ms
// 
// Part 6 B:   zstd1 3.945x, cmp 20.1ms dec 21.5ms
// Part 6 D:   zstd1 3.945x, cmp 17.5ms dec 18.8ms
// 
// Split into chunks, using attempt B from part 6:
// split   1k: zstd1 3.460x, cmp 12.1ms dec  8.6ms
// split   2k: zstd1 3.543x, cmp 12.9ms dec  8.3ms
// split   4k: zstd1 3.596x, cmp 13.2ms dec  8.4ms
// split   8k: zstd1 3.627x, cmp 12.7ms dec  8.3ms
// split  16k: zstd1 3.645x, cmp 12.8ms dec  8.7ms
// split  32k: zstd1 3.658x, cmp 12.9ms dec  9.1ms
// split  64k: zstd1 3.663x, cmp 12.9ms dec  8.8ms
// split 128k: zstd1 3.676x, cmp 12.9ms dec  9.2ms
// split 256k: zstd1 3.703x, cmp 13.0ms dec  9.7ms
// split 512k: zstd1 3.757x, cmp 12.9ms dec  9.0ms
// split   1M: zstd1 3.870x, cmp 13.2ms dec  8.9ms
// split   2M: zstd1 3.939x, cmp 13.2ms dec  9.1ms <--
// split   4M: zstd1 3.943x, cmp 13.4ms dec  9.8ms
// split   8M: zstd1 3.943x, cmp 13.3ms dec  9.6ms
// split  16M: zstd1 3.945x, cmp 14.0ms dec  9.7ms
// split  32M: zstd1 3.945x, cmp 17.6ms dec 16.8ms
// split  64M: zstd1 3.945x, cmp 19.2ms dec 20.1ms
// split 128M: zstd1 3.945x, cmp 20.5ms dec 19.4ms
//
// Split into chunks, using attempt D from part 6:
// split  16k: zstd1 3.645x, cmp  9.1ms dec 7.4ms
// split  64k: zstd1 3.663x, cmp  9.3ms dec 7.3ms
// split 256k: zstd1 3.703x, cmp  9.4ms dec 7.5ms
// split   1M: zstd1 3.870x, cmp  9.5ms dec 7.8ms
// split   2M: zstd1 3.939x, cmp  9.6ms dec 7.7ms <--
// split   4M: zstd1 3.943x, cmp  9.7ms dec 7.9ms

// Mac:
// Part6D:
// I(256):       cmp 11.6 dec 5.2 ratio 3.945
//   split  64k: cmp 11.1 dec 5.6 ratio 3.664
//   split 256k: cmp 11.5 dec 4.9 ratio 3.703
//   split   1M: cmp 11.2 dec 4.6 ratio 3.870
//   split   2M: cmp 11.2 dec 4.9 ratio 3.939 <--
//   split   4M: cmp 11.4 dec 5.7 ratio 3.943
// K(256):
//   split 256k: cmp 11.2 dec 3.1
//   split   2M: cmp 11.3 dec 3.4
// L:            cmp  8.3 dec 3.1
//   split   2M: cmp  8.3 dec 2.9

const size_t kSplitChunkSize = 128 * 1024 * 1024;
static void TestFilterWithSplit(const uint8_t* src, uint8_t* dst, int channels, size_t dataElems)
{
    const size_t elemsPerChunk = kSplitChunkSize / channels;
    const size_t chunkSize = elemsPerChunk * channels;
    for (size_t de = 0; de < dataElems; de += elemsPerChunk)
    {
        TestFilter(src, dst, channels, de + elemsPerChunk > dataElems ? dataElems - de : elemsPerChunk);
        src += chunkSize;
        dst += chunkSize;
    }
}
static void TestUnFilterWithSplit(uint8_t* src, uint8_t* dst, int channels, size_t dataElems)
{
    const size_t elemsPerChunk = kSplitChunkSize / channels;
    const size_t chunkSize = elemsPerChunk * channels;
    for (size_t de = 0; de < dataElems; de += elemsPerChunk)
    {
        TestUnFilter(src, dst, channels, de + elemsPerChunk > dataElems ? dataElems - de : elemsPerChunk);
        src += chunkSize;
        dst += chunkSize;
    }
}


static uint32_t rotl(uint32_t x, int s)
{
    return (x << s) | (x >> (32 - s));
}
static uint32_t rotr(uint32_t x, int s)
{
    return (x >> s) | (x << (32 - s));
}

static void RotateLeft(const uint32_t* src, uint32_t* dst, size_t dataElems)
{
    for (size_t i = 0; i < dataElems; ++i)
        *dst++ = rotl(*src++, 1);
}
static void RotateRight(const uint32_t* src, uint32_t* dst, size_t dataElems)
{
    for (size_t i = 0; i < dataElems; ++i)
        *dst++ = rotr(*src++, 1);
}



static uint8_t* CompressionFilter(uint32_t filter, const float* data, int width, int height, int channels)
{
    uint8_t* tmp = (uint8_t*)data;
    if (filter != kFilterNone)
    {
        uint64_t t0 = stm_now();
        bool extraSpaceForRotation = ((filter & kFilterRot1) != 0) && (filter != kFilterRot1);
        const int planeElems = width * height;
        const int dataSize = planeElems * channels * sizeof(float);
        tmp = new uint8_t[dataSize * (extraSpaceForRotation ? 2 : 1)];
        const int dataElems = planeElems * channels;
        if (filter & kFilterRot1)
        {
            RotateLeft((uint32_t*)data, (uint32_t*)(tmp + (extraSpaceForRotation ? dataSize : 0)), dataElems);
            if (extraSpaceForRotation)
                data = (const float*)(tmp + dataSize);
        }

        if ((filter & kFilterSplit32) != 0)
        {
            Split((uint32_t*)data, (uint32_t*)tmp, channels, planeElems);
            if ((filter & kFilterDeltaDiff) != 0) EncodeDeltaDif((uint32_t*)tmp, dataElems);
            if ((filter & kFilterDeltaXor) != 0) EncodeDeltaXor((uint32_t*)tmp, dataElems);
        }
        if ((filter & kFilterSplit8) != 0)
        {
            Split((const uint8_t*)data, tmp, channels * sizeof(float), planeElems);
            if ((filter & kFilterDeltaDiff) != 0) EncodeDeltaDif((uint8_t*)tmp, dataSize);
            if ((filter & kFilterDeltaXor) != 0) EncodeDeltaXor((uint8_t*)tmp, dataSize);
        }
        if (filter & kFilterSplit8Delta)
            Split8Delta((const uint8_t*)data, tmp, channels * sizeof(float), planeElems);
        if (filter & kFilterTest)
            TestFilterWithSplit((const uint8_t*)data, tmp, channels * sizeof(float), planeElems);
        if ((filter & kFilterBitShuffle) != 0)
        {
            bshuf_bitshuffle(data, tmp, planeElems, channels * sizeof(float), 0);
            if ((filter & kFilterDeltaDiff) != 0) EncodeDeltaDif((uint8_t*)tmp, dataSize);
            if ((filter & kFilterDeltaXor) != 0) EncodeDeltaXor((uint8_t*)tmp, dataSize);
        }
        g_time_filter += stm_since(t0);
        ++g_count_filter;
    }
    return tmp;
}

static void DecompressionFilter(uint32_t filter, uint8_t* tmp, float* data, int width, int height, int channels)
{
    if (filter != kFilterNone)
    {
        uint64_t t0 = stm_now();
        const int planeElems = width * height;
        const int dataElems = planeElems * channels;
        const int dataSize = planeElems * channels * sizeof(float);
        bool extraSpaceForRotation = ((filter & kFilterRot1) != 0) && (filter != kFilterRot1);
        uint8_t* dstData = (uint8_t*)data;
        if (extraSpaceForRotation)
            dstData = tmp + dataSize;

        if ((filter & kFilterSplit32) != 0)
        {
            if ((filter & kFilterDeltaDiff) != 0) DecodeDeltaDif((uint32_t*)tmp, dataElems);
            if ((filter & kFilterDeltaXor) != 0) DecodeDeltaXor((uint32_t*)tmp, dataElems);
            UnSplit((const uint32_t*)tmp, (uint32_t*)dstData, channels, planeElems);
        }
        if ((filter & kFilterSplit8) != 0)
        {
            if ((filter & kFilterDeltaDiff) != 0) DecodeDeltaDif(tmp, dataSize);
            if ((filter & kFilterDeltaXor) != 0) DecodeDeltaXor(tmp, dataSize);
            UnSplit(tmp, dstData, channels * sizeof(float), planeElems);
        }
        if (filter & kFilterSplit8Delta)
            UnSplit8Delta(tmp, dstData, channels * sizeof(float), planeElems);
        if (filter & kFilterTest)
            TestUnFilterWithSplit(tmp, dstData, channels * sizeof(float), planeElems);
        if ((filter & kFilterBitShuffle) != 0)
        {
            if ((filter & kFilterDeltaDiff) != 0) DecodeDeltaDif(tmp, dataSize);
            if ((filter & kFilterDeltaXor) != 0) DecodeDeltaXor(tmp, dataSize);
            bshuf_bitunshuffle(tmp, dstData, planeElems, channels * sizeof(float), 0);
        }
        if (filter & kFilterRot1)
        {
            RotateRight((const uint32_t*)(extraSpaceForRotation ? dstData : tmp), (uint32_t*)data, dataElems);
        }

        delete[] tmp;
        g_time_unfilter += stm_since(t0);
        ++g_count_unfilter;
    }
}


uint8_t* GenericCompressor::Compress(int level, const float* data, int width, int height, int channels, size_t& outSize)
{
    size_t dataSize = width * height * channels * sizeof(float);
    uint8_t* tmp = CompressionFilter(m_Filter, data, width, height, channels);

    size_t bound = compress_calc_bound(dataSize, m_Format);
    uint8_t* cmp = new uint8_t[bound];
    outSize = compress_data(tmp, dataSize, cmp, bound, m_Format, level);

    if (m_Filter != kFilterNone) delete[] tmp;
    return cmp;
}

void GenericCompressor::Decompress(const uint8_t* cmp, size_t cmpSize, float* data, int width, int height, int channels)
{
    size_t dataSize = width * height * channels * sizeof(float);
    uint8_t* tmp = (uint8_t*)data;
    bool extraSpaceForRotation = ((m_Filter & kFilterRot1) != 0) && (m_Filter != kFilterRot1);
    if (m_Filter != kFilterNone) tmp = new uint8_t[dataSize * (extraSpaceForRotation ? 2 : 1)];

    decompress_data(cmp, cmpSize, tmp, dataSize, m_Format);

    DecompressionFilter(m_Filter, tmp, data, width, height, channels);
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
};
static_assert(sizeof(kCompressionFormatNames) / sizeof(kCompressionFormatNames[0]) == kCompressionCount);

static std::string GetFilterName(uint32_t filter)
{
    std::string split = "";
    if ((filter & kFilterSplit32) != 0) split += "-s32";
    if ((filter & kFilterSplit8) != 0) split += "-s8";
    if ((filter & kFilterBitShuffle) != 0) split += "-s1";
    if ((filter & kFilterRot1) != 0) split += "-r1";
    std::string delta = "";
    if ((filter & kFilterDeltaDiff) != 0) delta += "-dif";
    if ((filter & kFilterDeltaXor) != 0) delta += "-xor";
    if ((filter & kFilterSplit8Delta) != 0) split += "-s8dif";
    if ((filter & kFilterTest) != 0) split += "-tst";
    return split + delta;
}

void GenericCompressor::PrintName(size_t bufSize, char* buf) const
{
    snprintf(buf, bufSize, "%s%s", kCompressionFormatNames[m_Format], GetFilterName(m_Filter).c_str());
}

void GenericCompressor::PrintVersion(size_t bufSize, char* buf) const
{
    compressor_get_version(m_Format, bufSize, buf);
}

/*
'#04640e', green
'#0c9618',
'#12b520',
'#3fd24c',
'#b0b0b0', gray
'#00ecdf', cyan
'#00bfa7',
'#00786a',
purple:
'#e0b7cc',
'#d57292',
'#a66476',
'#6d525b',
orange:
'#ffac8d', 
'#ff6454',
'#de5546',
'#a64436',
blue:
'#006fb1',
'#0094ef',
'#00b2ff',
'#49ddff',
purple:
'#ffb0ff', 
'#dc74ff',
'#8a4b9d',
*/

uint32_t GenericCompressor::GetColor() const
{
    // https://www.w3schools.com/colors/colors_picker.asp
    bool faded = (m_Filter & kFilterTest) == 0;
    if (m_Format == kCompressionZstd) return faded ? 0x90d596 : 0x0c9618; // green
    if (m_Format == kCompressionLZ4) return faded ? 0xd9d18c : 0xb19f00; // yellow
    if (m_Format == kCompressionZlib) return faded ? 0x8cd9cf : 0x00bfa7; // cyan
    if (m_Format == kCompressionLibdeflate) return 0x00786a; // cyan
    if (m_Format == kCompressionBrotli) return faded ? 0xd19a94 : 0xde5546; // orange
    // purple
    if (m_Format == kCompressionOoodleSelkie)   return 0xffb0ff;
    if (m_Format == kCompressionOoodleMermaid)  return 0xdc74ff;
    if (m_Format == kCompressionOoodleKraken)   return faded ? 0xc4b6c9 : 0x8a4b9d; // dark purple regular: 0x8a4b9d lighter: 0xc4b6c9
    return 0;
}

static const char* GetGenericShape(uint filter)
{
    if (filter == 0) return "'circle', lineDashStyle: [4, 2]";
    if (filter & kFilterTest) return "{type:'circle'}, pointSize: 12, lineWidth: 3";
    if (filter & kFilterSplit8Delta) return "{type:'square', rotation: 45}, pointSize: 8";
    if ((filter & kFilterSplit8) && (filter & kFilterDeltaDiff)) return "{type:'square', rotation: 45}, pointSize: 8";
    if ((filter & kFilterSplit8) && (filter & kFilterDeltaXor))  return "{type:'star', sides:4, dent: 0.5}, pointSize: 8";
    if ((filter & kFilterSplit8)) return "'square', pointSize: 8, lineDashStyle: [4, 2]";
    if ((filter & kFilterSplit32) && (filter & kFilterDeltaDiff)) return "{type:'triangle', rotation: 30}, pointSize: 10";
    if ((filter & kFilterSplit32) && (filter & kFilterDeltaXor))  return "{type:'triangle', rotation: -30}, pointSize: 10";
    if ((filter & kFilterSplit32)) return "'triangle', pointSize: 10, lineDashStyle: [4, 2]";
    return "'circle'";
}

const char* GenericCompressor::GetShapeString() const
{
    return GetGenericShape(m_Filter);
}

std::vector<int> GenericCompressor::GetLevels() const
{
    return GetGenericLevelRange(m_Format);
}


static uint8_t* CompressGeneric(CompressionFormat format, int level, uint8_t* data, size_t dataSize, size_t& outSize)
{
    if (format == kCompressionCount)
    {
        outSize = dataSize;
        return data;
    }
    size_t bound = compress_calc_bound(dataSize, format);
    uint8_t* cmp = new uint8_t[bound + 4];
    *(uint32_t*)cmp = uint32_t(dataSize); // store orig size at start
    outSize = compress_data(data, dataSize, cmp + 4, bound, format, level) + 4;
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
    uint8_t* tmp = CompressionFilter(m_Filter, data, width, height, channels);

    int stride = (m_Filter & kFilterSplit32) ? sizeof(float) : channels * sizeof(float);
    size_t dataSize = width * height * channels * sizeof(float);
    int vertexCount = int(dataSize / stride);
    size_t moBound = compress_meshopt_vertex_attribute_bound(vertexCount, stride);
    uint8_t* moCmp = new uint8_t[moBound];
    size_t moSize = compress_meshopt_vertex_attribute(tmp, vertexCount, stride, moCmp, moBound);
    if (m_Filter != kFilterNone) delete[] tmp;
    return CompressGeneric(m_Format, level, moCmp, moSize, outSize);
}

void MeshOptCompressor::Decompress(const uint8_t* cmp, size_t cmpSize, float* data, int width, int height, int channels)
{
    size_t dataSize = width * height * channels * sizeof(float);

    int stride = (m_Filter & kFilterSplit32) ? sizeof(float) : channels * sizeof(float);
    int vertexCount = int(dataSize / stride);

    size_t decompSize;
    uint8_t* decomp = DecompressGeneric(m_Format, cmp, cmpSize, decompSize);

    uint8_t* tmp = (uint8_t*)data;
    bool extraSpaceForRotation = ((m_Filter & kFilterRot1) != 0) && (m_Filter != kFilterRot1);
    if (m_Filter != kFilterNone) tmp = new uint8_t[dataSize * (extraSpaceForRotation ? 2 : 1)];
    decompress_meshopt_vertex_attribute(decomp, decompSize, vertexCount, stride, tmp);
    if (decomp != cmp) delete[] decomp;

    DecompressionFilter(m_Filter, tmp, data, width, height, channels);
}

std::vector<int> MeshOptCompressor::GetLevels() const
{
    return GetGenericLevelRange(m_Format);
}

void MeshOptCompressor::PrintName(size_t bufSize, char* buf) const
{
    std::string filter = GetFilterName(m_Filter);
    if (m_Format == kCompressionCount)
        snprintf(buf, bufSize, "meshopt%s", filter.c_str());
    else
        snprintf(buf, bufSize, "meshopt-%s%s", kCompressionFormatNames[m_Format], filter.c_str());
}

void MeshOptCompressor::PrintVersion(size_t bufSize, char* buf) const
{
    meshopt_get_version(bufSize, buf);
}


uint32_t MeshOptCompressor::GetColor() const
{
    // blue
    bool faded = true;
    if (m_Format == kCompressionZstd) return faded ? 0xb0d7e8 : 0x00b2ff;
    if (m_Format == kCompressionLZ4) return 0x49ddff;
    if (m_Format == kCompressionOoodleKraken) return 0x0094ef;
    return faded ? 0x79b2d2 : 0x006fb1;
}

const char* MeshOptCompressor::GetShapeString() const
{
    if (m_Format == kCompressionCount) return "'circle', pointSize: 20";
    return "'circle', lineDashStyle: [4, 2]";
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

const char* FpzipCompressor::GetShapeString() const { return "{type:'star', sides:5}, pointSize: 20"; }
const char* ZfpCompressor::GetShapeString() const { return "{type:'star', sides:4}, pointSize: 20"; }
const char* SpdpCompressor::GetShapeString() const { return "{type:'star', sides:6}, pointSize: 12, lineWidth: 3"; }
#if BUILD_WITH_NDZIP
const char* NdzipCompressor::GetShapeString() const { return "{type:'star', sides:7}, pointSize: 20"; }
#endif
const char* StreamVByteCompressor::GetShapeString() const
{
    if (m_Format != kCompressionCount)
        return "{type:'star', sides:3}, pointSize: 14, lineWidth: 3";
    return "{type:'star', sides:3}, pointSize: 20";
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

    return CompressGeneric(m_Format, level, cmp, cmpSize, outSize);
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
