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


#if defined(__x86_64__) || defined(_M_X64)
#	define CPU_ARCH_X64 1
#	include <emmintrin.h> // sse2
#	include <tmmintrin.h> // sse3
#	include <smmintrin.h> // sse4.1
#endif
#if defined(__aarch64__) || defined(_M_ARM64)
#	define CPU_ARCH_ARM64 1
#	include <arm_neon.h>
#endif


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

#if CPU_ARCH_X64
// https://gist.github.com/rygorous/4212be0cd009584e4184e641ca210528
static inline __m128i prefix_sum_u8(__m128i x)
{
    x = _mm_add_epi8(x, _mm_slli_epi64(x, 8));
    x = _mm_add_epi8(x, _mm_slli_epi64(x, 16));
    x = _mm_add_epi8(x, _mm_slli_epi64(x, 32));
    x = _mm_add_epi8(x, _mm_shuffle_epi8(x, _mm_setr_epi8(-1, -1, -1, -1, -1, -1, -1, -1, 7, 7, 7, 7, 7, 7, 7, 7)));
    return x;
}
#endif // #if CPU_ARCH_X64
#if CPU_ARCH_ARM64
// straight-up port to NEON of the above; no idea if this is efficient at all, yolo!
static inline uint8x16_t prefix_sum_u8(uint8x16_t x)
{
    x = vaddq_u8(x, vshlq_u64(x, vdupq_n_u64(8)));
    x = vaddq_u8(x, vshlq_u64(x, vdupq_n_u64(16)));
    x = vaddq_u8(x, vshlq_u64(x, vdupq_n_u64(32)));
    alignas(16) uint8_t tbl[16] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 7, 7, 7, 7, 7, 7, 7, 7};
    x = vaddq_u8(x, vqtbl1q_u8(x, vld1q_u8(tbl)));
    return x;
}
#endif // #if CPU_ARCH_ARM64

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
    uint8_t prev = 0;
    for (int ich = 0; ich < channels; ++ich)
    {
        const uint8_t* srcPtr = src + ich;
        size_t ip = 0;

#	    if CPU_ARCH_X64
        // SSE simd loop, 16 bytes at a time
        __m128i prev16 = _mm_set1_epi8(prev);
        for (; ip < planeElems / 16; ++ip)
        {
            // gather 16 bytes from source data
            __m128i v = _mm_set1_epi8(0);
            v = _mm_insert_epi8(v, *srcPtr, 0); srcPtr += channels; // sse4.1
            v = _mm_insert_epi8(v, *srcPtr, 1); srcPtr += channels;
            v = _mm_insert_epi8(v, *srcPtr, 2); srcPtr += channels;
            v = _mm_insert_epi8(v, *srcPtr, 3); srcPtr += channels;
            v = _mm_insert_epi8(v, *srcPtr, 4); srcPtr += channels;
            v = _mm_insert_epi8(v, *srcPtr, 5); srcPtr += channels;
            v = _mm_insert_epi8(v, *srcPtr, 6); srcPtr += channels;
            v = _mm_insert_epi8(v, *srcPtr, 7); srcPtr += channels;
            v = _mm_insert_epi8(v, *srcPtr, 8); srcPtr += channels;
            v = _mm_insert_epi8(v, *srcPtr, 9); srcPtr += channels;
            v = _mm_insert_epi8(v, *srcPtr, 10); srcPtr += channels;
            v = _mm_insert_epi8(v, *srcPtr, 11); srcPtr += channels;
            v = _mm_insert_epi8(v, *srcPtr, 12); srcPtr += channels;
            v = _mm_insert_epi8(v, *srcPtr, 13); srcPtr += channels;
            v = _mm_insert_epi8(v, *srcPtr, 14); srcPtr += channels;
            v = _mm_insert_epi8(v, *srcPtr, 15); srcPtr += channels;
            // delta from previous
            __m128i delta = _mm_sub_epi8(v, _mm_alignr_epi8(v, prev16, 15)); // sse3
            _mm_storeu_si128((__m128i*)dst, delta);
            prev16 = v;
            dst += 16;
        }
        prev = _mm_extract_epi8(prev16, 15); // sse4.1
#       endif // if CPU_ARCH_X64

#       if CPU_ARCH_ARM64
        // NEON simd loop, 16 bytes at a time
        uint8x16_t prev16 = vdupq_n_u8(prev);
        for (; ip < planeElems / 16; ++ip)
        {
            // gather 16 bytes from source data
            uint8x16_t v = vdupq_n_u8(0);
            v = vsetq_lane_u8(*srcPtr, v, 0); srcPtr += channels;
            v = vsetq_lane_u8(*srcPtr, v, 1); srcPtr += channels;
            v = vsetq_lane_u8(*srcPtr, v, 2); srcPtr += channels;
            v = vsetq_lane_u8(*srcPtr, v, 3); srcPtr += channels;
            v = vsetq_lane_u8(*srcPtr, v, 4); srcPtr += channels;
            v = vsetq_lane_u8(*srcPtr, v, 5); srcPtr += channels;
            v = vsetq_lane_u8(*srcPtr, v, 6); srcPtr += channels;
            v = vsetq_lane_u8(*srcPtr, v, 7); srcPtr += channels;
            v = vsetq_lane_u8(*srcPtr, v, 8); srcPtr += channels;
            v = vsetq_lane_u8(*srcPtr, v, 9); srcPtr += channels;
            v = vsetq_lane_u8(*srcPtr, v, 10); srcPtr += channels;
            v = vsetq_lane_u8(*srcPtr, v, 11); srcPtr += channels;
            v = vsetq_lane_u8(*srcPtr, v, 12); srcPtr += channels;
            v = vsetq_lane_u8(*srcPtr, v, 13); srcPtr += channels;
            v = vsetq_lane_u8(*srcPtr, v, 14); srcPtr += channels;
            v = vsetq_lane_u8(*srcPtr, v, 15); srcPtr += channels;

            // delta from previous
            uint8x16_t delta = vsubq_u8(v, vextq_u8(prev16, v, 15));
            vst1q_u8(dst, delta);
            prev16 = v;
            dst += 16;
        }
        prev = vgetq_lane_u8(prev16, 15);
#       endif // if CPU_ARCH_ARM64

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
}

static void UnSplit8Delta(uint8_t* src, uint8_t* dst, int channels, size_t planeElems)
{
#if 0
    // "e" case: two pass: delta with SIMD prefix sum, followed by sequential unsplit into destination
    // first pass: decode delta
    const size_t dataSize = planeElems * channels;
    uint8_t* ptr = src;
    size_t ip = 0;
    uint8_t prev = 0;
#	if CPU_ARCH_X64
    // SSE simd loop, 16 bytes at a time
    __m128i prev16 = _mm_set1_epi8(0);
    __m128i hibyte = _mm_set1_epi8(15);
    for (; ip < dataSize / 16; ++ip)
    {
        __m128i v = _mm_loadu_si128((const __m128i*)ptr);
        // un-delta via prefix sum
        prev16 = _mm_add_epi8(prefix_sum_u8(v), _mm_shuffle_epi8(prev16, hibyte));
        _mm_storeu_si128((__m128i*)ptr, prev16);
        ptr += 16;
    }
    prev = _mm_extract_epi8(prev16, 15); // sse4.1
#   endif // if CPU_ARCH_X64
    
#   if CPU_ARCH_ARM64
    // NEON simd loop, 16 bytes at a time
    uint8x16_t prev16 = vdupq_n_u8(prev);
    uint8x16_t hibyte = vdupq_n_u8(15);
    //alignas(16) uint8_t scatter[16];
    for (; ip < dataSize / 16; ++ip)
    {
        // load 16 bytes of filtered data
        uint8x16_t v = vld1q_u8(ptr);
        // un-delta via prefix sum
        prev16 = vaddq_u8(prefix_sum_u8(v), vqtbl1q_u8(prev16, hibyte));
        vst1q_u8(ptr, prev16);
        ptr += 16;
    }
    prev = vgetq_lane_u8(prev16, 15);
#   endif // if CPU_ARCH_ARM64

    // any trailing leftover
    for (ip = ip * 16; ip < dataSize; ++ip)
    {
        uint8_t v = *ptr + prev;
        prev = v;
        *ptr = v;
        ptr += 1;
    }

    // second pass: un-split; sequential write into destination
    uint8_t* dstPtr = dst;
    for (int ip = 0; ip < planeElems; ++ip)
    {
        const uint8_t* srcPtr = src + ip;
        for (int ich = 0; ich < channels; ++ich)
        {
            uint8_t v = *srcPtr;
            *dstPtr = v;
            srcPtr += planeElems;
            dstPtr += 1;
        }
    }
#endif

#if 1
    // "d" case: combined delta+unsplit; SIMD prefix sum delta, unrolled scattered writes into destination
    uint8_t prev = 0;
    for (int ich = 0; ich < channels; ++ich)
    {
        uint8_t* dstPtr = dst + ich;
        size_t ip = 0;

#	    if CPU_ARCH_X64
        // SSE simd loop, 16 bytes at a time
        __m128i prev16 = _mm_set1_epi8(prev);
        __m128i hibyte = _mm_set1_epi8(15);
        //alignas(16) uint8_t scatter[16];
        for (; ip < planeElems / 16; ++ip)
        {
            // load 16 bytes of filtered data
            __m128i v = _mm_loadu_si128((const __m128i*)src);
            // un-delta via prefix sum
            prev16 = _mm_add_epi8(prefix_sum_u8(v), _mm_shuffle_epi8(prev16, hibyte));
            // scattered write into destination
            //_mm_store_si128((__m128i*)scatter, prev16);
            //for (int lane = 0; lane < 16; ++lane)
            //{
            //    *dstPtr = scatter[lane];
            //    dstPtr += channels;
            //}
            *dstPtr = _mm_extract_epi8(prev16, 0); dstPtr += channels;
            *dstPtr = _mm_extract_epi8(prev16, 1); dstPtr += channels;
            *dstPtr = _mm_extract_epi8(prev16, 2); dstPtr += channels;
            *dstPtr = _mm_extract_epi8(prev16, 3); dstPtr += channels;
            *dstPtr = _mm_extract_epi8(prev16, 4); dstPtr += channels;
            *dstPtr = _mm_extract_epi8(prev16, 5); dstPtr += channels;
            *dstPtr = _mm_extract_epi8(prev16, 6); dstPtr += channels;
            *dstPtr = _mm_extract_epi8(prev16, 7); dstPtr += channels;
            *dstPtr = _mm_extract_epi8(prev16, 8); dstPtr += channels;
            *dstPtr = _mm_extract_epi8(prev16, 9); dstPtr += channels;
            *dstPtr = _mm_extract_epi8(prev16, 10); dstPtr += channels;
            *dstPtr = _mm_extract_epi8(prev16, 11); dstPtr += channels;
            *dstPtr = _mm_extract_epi8(prev16, 12); dstPtr += channels;
            *dstPtr = _mm_extract_epi8(prev16, 13); dstPtr += channels;
            *dstPtr = _mm_extract_epi8(prev16, 14); dstPtr += channels;
            *dstPtr = _mm_extract_epi8(prev16, 15); dstPtr += channels;
            src += 16;
        }
        prev = _mm_extract_epi8(prev16, 15); // sse4.1
#       endif // if CPU_ARCH_X64

#       if CPU_ARCH_ARM64
        // NEON simd loop, 16 bytes at a time
        uint8x16_t prev16 = vdupq_n_u8(prev);
        uint8x16_t hibyte = vdupq_n_u8(15);
        //alignas(16) uint8_t scatter[16];
        for (; ip < planeElems / 16; ++ip)
        {
            // load 16 bytes of filtered data
            uint8x16_t v = vld1q_u8(src);
            // un-delta via prefix sum
            prev16 = vaddq_u8(prefix_sum_u8(v), vqtbl1q_u8(prev16, hibyte));
            // scattered write into destination
            //vst1q_u8(scatter, prev16);
            //for (int lane = 0; lane < 16; ++lane)
            //{
            //    *dstPtr = scatter[lane];
            //    dstPtr += channels;
            //}
            *dstPtr = vgetq_lane_u8(prev16, 0); dstPtr += channels;
            *dstPtr = vgetq_lane_u8(prev16, 1); dstPtr += channels;
            *dstPtr = vgetq_lane_u8(prev16, 2); dstPtr += channels;
            *dstPtr = vgetq_lane_u8(prev16, 3); dstPtr += channels;
            *dstPtr = vgetq_lane_u8(prev16, 4); dstPtr += channels;
            *dstPtr = vgetq_lane_u8(prev16, 5); dstPtr += channels;
            *dstPtr = vgetq_lane_u8(prev16, 6); dstPtr += channels;
            *dstPtr = vgetq_lane_u8(prev16, 7); dstPtr += channels;
            *dstPtr = vgetq_lane_u8(prev16, 8); dstPtr += channels;
            *dstPtr = vgetq_lane_u8(prev16, 9); dstPtr += channels;
            *dstPtr = vgetq_lane_u8(prev16, 10); dstPtr += channels;
            *dstPtr = vgetq_lane_u8(prev16, 11); dstPtr += channels;
            *dstPtr = vgetq_lane_u8(prev16, 12); dstPtr += channels;
            *dstPtr = vgetq_lane_u8(prev16, 13); dstPtr += channels;
            *dstPtr = vgetq_lane_u8(prev16, 14); dstPtr += channels;
            *dstPtr = vgetq_lane_u8(prev16, 15); dstPtr += channels;
            src += 16;
        }
        prev = vgetq_lane_u8(prev16, 15);
#       endif // if CPU_ARCH_ARM64

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

// memcpy: 3.6ms
// part 6 B: 20.1ms ratio 3.945x
// split 2M, part 6 B: 13.2ms ratio 3.939x
static void TestFilter(const uint8_t* src, uint8_t* dst, int channels, size_t dataElems)
{
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

static void Transpose(const uint8_t* a, uint8_t* b, int rows, int cols)
{
    // TODO: SIMD
    for (int j = 0; j < rows; ++j)
    {
        for (int i = 0; i < cols; ++i)
        {
            b[j * rows + i] = a[i * cols + j];
        }
    }
}

// memcpy: 2.9ms
// part 6 B: 21.5ms
// part 6 D: 18.8ms
// split 2M, part 6 B: 9.1ms ratio 3.939x
static void TestUnFilter(const uint8_t* src, uint8_t* dst, int channels, size_t dataElems)
{
    // two pass, seq dst write: 31.6ms
    // two pass, seq src read: 15.8ms
    // one pass, seq src read (Part 6 B), Split 2M: 8.8ms 

#if 0
    // sequential write into dst; scattered read from all streams (no 2M split): 33.6ms
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

#if 0
    // seq write into dst, special SSE case for 16 channels: vs 15.7ms clang 14.3ms
    if (channels == 16)
    {
        uint8_t* dstPtr = dst;
        __m128i prev = _mm_setzero_si128();
        for (size_t ip = 0; ip < dataElems; ip++)
        {
            // read from each of 16 channels
            alignas(16) uint8_t chdata[16] = {};
            const uint8_t* srcPtr = src + ip;
            for (int ich = 0; ich < 16; ++ich)
            {
                chdata[ich] = *srcPtr;
                srcPtr += dataElems;
            }
            // accumulate sum and write into destination
            prev = _mm_add_epi8(prev, _mm_load_si128((const __m128i*)chdata));
            _mm_storeu_si128((__m128i*)dstPtr, prev);
            dstPtr += 16;
        }
    }
    else
    {
        // temp: generic fallback
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
    }
#endif

#if 1
    // seq write into dst, special SSE case for 16 channels, w/ 16-byte SSE fetches in each: vs 8.6ms clang 8.5ms
    if (channels == 16)
    {
        uint8_t* dstPtr = dst;
        size_t ip = 0;
        __m128i prev = _mm_setzero_si128();
        // SIMD loop; reading from each channel 16 bytes at a time
        {
            for (; ip < dataElems-15; ip += 16)
            {
                // read 16b from each of 16 channels
                __m128i chdata[16];
                const uint8_t* srcPtr = src + ip;
                for (int ich = 0; ich < channels; ++ich)
                {
                    chdata[ich] = _mm_loadu_si128((const __m128i*)srcPtr);
                    srcPtr += dataElems;
                }
                // transpose
                __m128i xposed[16];
                Transpose((const uint8_t*)chdata, (uint8_t*)xposed, 16, 16);
                // accumulate sum and write into destination
                for (int i = 0; i < 16; ++i)
                {
                    prev = _mm_add_epi8(prev, xposed[i]);
                    _mm_storeu_si128((__m128i*)dstPtr, prev);
                    dstPtr += 16;
                }
            }
        }
        // scalar loop for any non-multiple-of-16 remainder
        {
            for (; ip < dataElems; ip++)
            {
                // read from each of 16 channels
                alignas(16) uint8_t chdata[16];
                const uint8_t* srcPtr = src + ip;
                for (int ich = 0; ich < 16; ++ich)
                {
                    chdata[ich] = *srcPtr;
                    srcPtr += dataElems;
                }
                // accumulate sum and write into destination
                prev = _mm_add_epi8(prev, _mm_load_si128((const __m128i*)chdata));
                _mm_storeu_si128((__m128i*)dstPtr, prev);
                dstPtr += 16;
            }
        }
    }
    else
    {
        // temp: generic fallback
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

/*
const size_t kSplitChunkSize = 128 * 1024 * 1024;
static void TestFilterWithSplit(const uint8_t* src, uint8_t* dst, int channels, size_t dataElems)
{
    const size_t elemsPerChunk = kSplitChunkSize / channels;
    const size_t chunkSize = elemsPerChunk * channels;
    for (size_t de = 0; de < dataElems; de += elemsPerChunk)
    {
        TestFilterImpl(src, dst, channels, de + elemsPerChunk > dataElems ? dataElems - de : elemsPerChunk);
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
        TestUnFilterImpl(src, dst, channels, de + elemsPerChunk > dataElems ? dataElems - de : elemsPerChunk);
        src += chunkSize;
        dst += chunkSize;
    }
}
*/


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
            TestFilter((const uint8_t*)data, tmp, channels * sizeof(float), planeElems);
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
            TestUnFilter(tmp, dstData, channels * sizeof(float), planeElems);
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
