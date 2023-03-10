#include "filters.h"
#include "simd.h"
#include <assert.h>
#include <string.h>

const size_t kMaxChannels = 64;
static_assert(kMaxChannels >= 16, "max channels can't be lower than simd width");


// no-op filter: just a memcpy
void Filter_Null(const uint8_t* src, uint8_t* dst, int channels, size_t dataElems)
{
    memcpy(dst, src, channels * dataElems);
}

void UnFilter_Null(const uint8_t* src, uint8_t* dst, int channels, size_t dataElems)
{
    memcpy(dst, src, channels * dataElems);
}


// Part 6 "A"
template<typename T> static void EncodeDelta(T* data, size_t dataElems)
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
template<typename T> static void DecodeDelta(T* data, size_t dataElems)
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
template<typename T> static void Split(const T* src, T* dst, int channels, size_t dataElems)
{
    for (int ich = 0; ich < channels; ++ich)
    {
        const T* ptr = src + ich;
        for (size_t ip = 0; ip < dataElems; ++ip)
        {
            *dst = *ptr;
            ptr += channels;
            dst += 1;
        }
    }
}
template<typename T> static void UnSplit(const T* src, T* dst, int channels, size_t dataElems)
{
    for (int ich = 0; ich < channels; ++ich)
    {
        T* ptr = dst + ich;
        for (size_t ip = 0; ip < dataElems; ++ip)
        {
            *ptr = *src;
            src += 1;
            ptr += channels;
        }
    }
}
void Filter_A(const uint8_t* src, uint8_t* dst, int channels, size_t dataElems)
{
    Split<uint8_t>(src, dst, channels, dataElems);
    EncodeDelta<uint8_t>(dst, channels * dataElems);
}
void UnFilter_A(const uint8_t* src, uint8_t* dst, int channels, size_t dataElems)
{
    DecodeDelta<uint8_t>((uint8_t*)src, channels * dataElems);
    UnSplit<uint8_t>(src, dst, channels, dataElems);
}

// Part 6 "B"
void Filter_B(const uint8_t* src, uint8_t* dst, int channels, size_t dataElems)
{
    uint8_t prev = 0;
    for (int ich = 0; ich < channels; ++ich)
    {
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
void UnFilter_B(const uint8_t* src, uint8_t* dst, int channels, size_t dataElems)
{
    uint8_t prev = 0;
    for (int ich = 0; ich < channels; ++ich)
    {
        uint8_t* dstPtr = dst + ich;
        for (size_t ip = 0; ip < dataElems; ++ip)
        {
            uint8_t v = *src + prev;
            prev = v;
            *dstPtr = v;
            src += 1;
            dstPtr += channels;
        }
    }
}

// Part 6 "D"
void Filter_D(const uint8_t* src, uint8_t* dst, int channels, size_t dataElems)
{
    uint8_t prev = 0;
    for (int ich = 0; ich < channels; ++ich)
    {
        const uint8_t* srcPtr = src + ich;
        size_t ip = 0;

        // SIMD loop, 16 bytes at a time
        Bytes16 prev16 = SimdSet1(prev);
        for (; ip < dataElems / 16; ++ip)
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
        for (ip = ip * 16; ip < dataElems; ++ip)
        {
            uint8_t v = *srcPtr;
            *dst = v - prev;
            prev = v;

            srcPtr += channels;
            dst += 1;
        }
    }
}

void UnFilter_D(const uint8_t* src, uint8_t* dst, int channels, size_t dataElems)
{
    // "d" case: combined delta+unsplit; SIMD prefix sum delta, unrolled scattered writes into destination
    uint8_t prev = 0;
    for (int ich = 0; ich < channels; ++ich)
    {
        uint8_t* dstPtr = dst + ich;
        size_t ip = 0;

        // SIMD loop, 16 bytes at a time
        Bytes16 prev16 = SimdSet1(prev);
        Bytes16 hibyte = SimdSet1(15);
        for (; ip < dataElems / 16; ++ip)
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
        for (ip = ip * 16; ip < dataElems; ++ip)
        {
            uint8_t v = *src + prev;
            prev = v;
            *dstPtr = v;
            src += 1;
            dstPtr += channels;
        }
    }
}

// F: just like B, except delta individual streams and not everything
void Filter_F(const uint8_t* src, uint8_t* dst, int channels, size_t dataElems)
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
void UnFilter_F(const uint8_t* src, uint8_t* dst, int channels, size_t dataElems)
{
    for (int ich = 0; ich < channels; ++ich)
    {
        uint8_t prev = 0;
        uint8_t* dstPtr = dst + ich;
        for (size_t ip = 0; ip < dataElems; ++ip)
        {
            uint8_t v = *src + prev;
            prev = v;
            *dstPtr = v;
            src += 1;
            dstPtr += channels;
        }
    }
}

// Scalar, fetch from N streams, write sequential
void UnFilter_G(const uint8_t* src, uint8_t* dst, int channels, size_t dataElems)
{
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


// Transpose NxM byte matrix, with faster code paths for rows=16, cols=multiple-of-16 case.
// Largely based on https://fgiesen.wordpress.com/2013/07/09/simd-transposes-1/ and
// https://fgiesen.wordpress.com/2013/08/29/simd-transposes-2/
static void EvenOddInterleave16(const Bytes16* a, Bytes16* b, int astride = 1)
{
    int bidx = 0;
    for (int i = 0; i < 8; ++i)
    {
        b[bidx] = SimdInterleaveL(a[i * astride], a[(i + 8) * astride]); bidx++;
        b[bidx] = SimdInterleaveR(a[i * astride], a[(i + 8) * astride]); bidx++;
    }
}
static void Transpose16x16(const Bytes16* a, Bytes16* b, int astride = 1)
{
    Bytes16 tmp1[16], tmp2[16];
    EvenOddInterleave16(a, tmp1, astride);
    EvenOddInterleave16(tmp1, tmp2);
    EvenOddInterleave16(tmp2, tmp1);
    EvenOddInterleave16(tmp1, b);
}
static void Transpose(const uint8_t* a, uint8_t* b, int cols, int rows)
{
    if (rows == 16 && ((cols % 16) == 0))
    {
        int blocks = cols / rows;
        for (int i = 0; i < blocks; ++i)
        {
            Transpose16x16(((const Bytes16*)a) + i, ((Bytes16*)b) + i * 16, blocks);
        }
    }
    else
    {
        for (int j = 0; j < rows; ++j)
        {
            for (int i = 0; i < cols; ++i)
            {
                b[i * rows + j] = a[j * cols + i];
            }
        }
    }
}

// Fetch 16 N-sized items, transpose, SIMD delta, write N separate 16-sized items
void Filter_H(const uint8_t* src, uint8_t* dst, int channels, size_t dataElems)
{
    uint8_t* dstPtr = dst;
    int64_t ip = 0;
    
    const uint8_t* srcPtr = src;
    // simd loop
    Bytes16 prev[kMaxChannels] = {};
    for (; ip < int64_t(dataElems) - 15; ip += 16)
    {
        // fetch 16 data items
        uint8_t curr[kMaxChannels * 16];
        memcpy(curr, srcPtr, channels * 16);
        srcPtr += channels * 16;
        // transpose so we have 16 bytes for each channel
        Bytes16 currT[kMaxChannels];
        Transpose(curr, (uint8_t*)currT, channels, 16);
        // delta within each channel, store
        for (int ich = 0; ich < channels; ++ich)
        {
            Bytes16 v = currT[ich];
            Bytes16 delta = SimdSub(v, SimdConcat<15>(v, prev[ich]));
            SimdStore(dstPtr + dataElems * ich, delta);
            prev[ich] = v;
        }
        dstPtr += 16;
    }
    // any remaining leftover
    if (ip < int64_t(dataElems))
    {
        uint8_t prev1[kMaxChannels];
        for (int ich = 0; ich < channels; ++ich)
            prev1[ich] = SimdGetLane<15>(prev[ich]);
        for (; ip < int64_t(dataElems); ip++)
        {
            for (int ich = 0; ich < channels; ++ich)
            {
                uint8_t v = *srcPtr;
                srcPtr++;
                dstPtr[dataElems * ich] = v - prev1[ich];
                prev1[ich] = v;
            }
            dstPtr++;
        }
    }
}

// Fetch 16b from N streams, prefix sum SIMD undelta, transpose, sequential write 16xN chunk.
void UnFilter_H(const uint8_t* src, uint8_t* dst, int channels, size_t dataElems)
{
    uint8_t* dstPtr = dst;
    int64_t ip = 0;

    // simd loop: fetch 16 bytes from each stream
    Bytes16 curr[kMaxChannels] = {};
    const Bytes16 hibyte = SimdSet1(15);
    for (; ip < int64_t(dataElems) - 15; ip += 16)
    {
        // fetch 16 bytes from each channel, prefix-sum un-delta
        const uint8_t* srcPtr = src + ip;
        for (int ich = 0; ich < channels; ++ich)
        {
            Bytes16 v = SimdLoad(srcPtr);
            // un-delta via prefix sum
            curr[ich] = SimdAdd(SimdPrefixSum(v), SimdShuffle(curr[ich], hibyte));
            srcPtr += dataElems;
        }

        // now transpose 16xChannels matrix
        uint8_t currT[kMaxChannels * 16];
        Transpose((const uint8_t*)curr, currT, 16, channels);

        // and store into destination
        memcpy(dstPtr, currT, 16 * channels);
        dstPtr += 16 * channels;
    }

    // any remaining leftover
    if (ip < int64_t(dataElems))
    {
        uint8_t curr1[kMaxChannels];
        for (int ich = 0; ich < channels; ++ich)
            curr1[ich] = SimdGetLane<15>(curr[ich]);
        for (; ip < int64_t(dataElems); ip++)
        {
            const uint8_t* srcPtr = src + ip;
            for (int ich = 0; ich < channels; ++ich)
            {
                uint8_t v = *srcPtr + curr1[ich];
                curr1[ich] = v;
                *dstPtr = v;
                srcPtr += dataElems;
                dstPtr += 1;
            }
        }
    }
}

// same as UnFilter_H but with special case for 16 channels
void UnFilter_I(const uint8_t* src, uint8_t* dst, int channels, size_t dataElems)
{
    // non-16 channels: use "H"
    if (channels != 16)
    {
        UnFilter_H(src, dst, channels, dataElems);
        return;
    }

    // 16 channels case:
    uint8_t* dstPtr = dst;
    int64_t ip = 0;

    // simd loop: fetch 16 bytes from each stream
    Bytes16 prev = SimdZero();
    for (; ip < int64_t(dataElems) - 15; ip += 16)
    {
        // fetch 16 bytes from each channel
        Bytes16 curr[16];
        const uint8_t* srcPtr = src + ip;
        for (int ich = 0; ich < 16; ++ich)
        {
            Bytes16 v = SimdLoad(srcPtr);
            curr[ich] = v;
            srcPtr += dataElems;
        }

        // transpose 16xChannels matrix
        Bytes16 currT[16];
        Transpose((const uint8_t*)curr, (uint8_t*)currT, 16, channels);

        // un-delta and store
        for (int ib = 0; ib < 16; ++ib)
        {
            prev = SimdAdd(prev, currT[ib]);
            SimdStore(dstPtr, prev);
            dstPtr += 16;
        }
    }

    // any remaining leftover
    if (ip < int64_t(dataElems))
    {
        alignas(16) uint8_t curr1[16];
        for (; ip < int64_t(dataElems); ip++)
        {
            const uint8_t* srcPtr = src + ip;
            for (int ich = 0; ich < channels; ++ich)
            {
                curr1[ich] = *srcPtr;
                srcPtr += dataElems;
            }
            prev = SimdAdd(prev, SimdLoadA(curr1));
            SimdStore(dstPtr, prev);
            dstPtr += 16;
        }
    }
}


// Fetch M bytes from N streams, transpose, SIMD undelta, store
// 16:  288 28.1
// 64:  335 29.0
// 128: 351 29.1
// 256: 258 27.2
// 384: 257 27.7
// 512: 259 28.2
// 768: 275 29.0
// 1k:  285 29.8
// 2k:  288 29.9
void UnFilter_J(const uint8_t* src, uint8_t* dst, int channels, size_t dataElems)
{
    const int kChunkBytes = 256;
    const int kChunkSimdSize = kChunkBytes / 16;
    static_assert((kChunkBytes % 16) == 0, "chunk bytes needs to be multiple of simd width");
    uint8_t* dstPtr = dst;
    int64_t ip = 0;
    alignas(16) uint8_t prev[kMaxChannels] = {};
    for (; ip < int64_t(dataElems) - (kChunkBytes - 1); ip += kChunkBytes)
    {
        // read chunk of bytes from each channel
        Bytes16 curr[kMaxChannels][kChunkSimdSize];
        const uint8_t* srcPtr = src + ip;
        for (int ich = 0; ich < channels; ++ich)
        {
            memcpy(curr[ich], srcPtr, kChunkBytes);
            srcPtr += dataElems;
        }

        // transpose
        uint8_t currT[kMaxChannels * kChunkBytes];
        Transpose((const uint8_t*)curr, currT, kChunkBytes, channels);

        // un-delta
        for (int ib = 0; ib < kChunkBytes; ++ib)
        {
            const uint8_t* curPtr = currT + ib * channels;
            for (int ich = 0; ich < channels; ich += 16)
            {
                Bytes16 v = SimdAdd(SimdLoadA(&prev[ich]), SimdLoad(curPtr));
                SimdStoreA(&prev[ich], v);
                curPtr += 16;
            }
            // store
            memcpy(dstPtr, prev, channels);
            dstPtr += channels;
        }
    }

    // scalar loop for any remainder
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

// WinVS                        Mac
//                  +16         +16
// 16:              185 16.3    203 18.0
// 32:              189 16.2    191 17.6
// 64:  209 28.0    183 17.1    191 16.6
// 128:             191 15.0    189 17.1
// 256: 207 27.8    176 15.4    192 16.8
// 384: 205 28.8    174 15.2    193 16.1
// 512: 207 28.7    180 15.0    193 17.0
// 768:             175 15.3    191 17.3
// 1k:              190 15.7    189 17.1
// 2k:              188 15.9    192 15.9
// 4k:              186 15.4    196 14.6
void UnFilter_K(const uint8_t* src, uint8_t* dst, int channels, size_t dataElems)
{
    if ((channels % 4) != 0) // should never happen; our data is floats so channels will always be multiple of 4
    {
        assert(false);
        return;
    }

    const int kChunkBytes = 384;
    constexpr bool k16Ch = true;
    const int kChunkSimdSize = kChunkBytes / 16;
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

        if (channels == 16 && k16Ch)
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
                        // store
                        memcpy(dstPtr, curPtrStart, channels);
                        dstPtr += channels;
                        curPtrStart += kMaxChannels;
                    }
                }
            }
        }
    }

    // any remainder
    if (channels == 16 && k16Ch)
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
}



void Filter_Shuffle(const uint8_t* src, uint8_t* dst, int channels, size_t dataElems)
{
    Split<uint8_t>(src, dst, channels, dataElems);
}

void UnFilter_Shuffle(const uint8_t* src, uint8_t* dst, int channels, size_t dataElems)
{
#if 0
    // A without delta
    // memcpy:      49.6    7.4
    // A-UnSplit    509     77
    UnSplit<uint8_t>(src, dst, channels, dataElems);
#endif

#if 0
    // G without delta
    // G w/o delta  654     95
    uint8_t* dstPtr = dst;
    for (size_t ip = 0; ip < dataElems; ++ip)
    {
        const uint8_t* srcPtr = src + ip;
        for (int ich = 0; ich < channels; ++ich)
        {
            uint8_t v = *srcPtr;
            *dstPtr = v;
            srcPtr += dataElems;
            dstPtr += 1;
        }
    }
#endif

#if 0
    // H without delta
    // H w/o delta  210 19.0
    uint8_t* dstPtr = dst;
    int64_t ip = 0;

    Bytes16 curr[kMaxChannels] = {};
    for (; ip < int64_t(dataElems) - 15; ip += 16)
    {
        // fetch 16 bytes from each channel
        const uint8_t* srcPtr = src + ip;
        for (int ich = 0; ich < channels; ++ich)
        {
            Bytes16 v = SimdLoad(srcPtr);
            curr[ich] = v;
            srcPtr += dataElems;
        }

        // now transpose 16xChannels matrix
        uint8_t currT[kMaxChannels * 16];
        Transpose((const uint8_t*)curr, currT, 16, channels);

        // and store into destination
        memcpy(dstPtr, currT, 16 * channels);
        dstPtr += 16 * channels;
    }

    // any remaining leftover
    for (; ip < int64_t(dataElems); ip++)
    {
        const uint8_t* srcPtr = src + ip;
        for (int ich = 0; ich < channels; ++ich)
        {
            uint8_t v = *srcPtr;
            *dstPtr = v;
            srcPtr += dataElems;
            dstPtr += 1;
        }
    }
#endif

#if 0
    // ^ H w/o delta  210 19.0
    // 
    // Similar to H, but fetch more than 16 bytes from each channel at once
    // 16: 212 17.7
    // 32: 243 20.7
    // 64: 288 22.1
    // 128: 284 23.0
    // 256: 188 18.0
    // 384: 184 17.9
    // 512: 187 18.6
    // 768: 207 19.3
    // 1024: 215 20.4
    // 2048: 224 21.4
    // 4096: 234 24.6
    const int kChunkBytes = 256;
    uint8_t* dstPtr = dst;
    int64_t ip = 0;

    uint8_t curr[kMaxChannels][kChunkBytes] = {};
    for (; ip < int64_t(dataElems) - (kChunkBytes-1); ip += kChunkBytes)
    {
        // fetch N bytes from each channel
        const uint8_t* srcPtr = src + ip;
        for (int ich = 0; ich < channels; ++ich)
        {
            memcpy(curr[ich], srcPtr, kChunkBytes);
            srcPtr += dataElems;
        }

        // now transpose NxChannels matrix
        uint8_t currT[kMaxChannels * kChunkBytes];
        Transpose(curr[0], currT, kChunkBytes, channels);

        // and store into destination
        memcpy(dstPtr, currT, kChunkBytes * channels);
        dstPtr += kChunkBytes * channels;
    }

    // any remaining leftover
    for (; ip < int64_t(dataElems); ip++)
    {
        const uint8_t* srcPtr = src + ip;
        for (int ich = 0; ich < channels; ++ich)
        {
            uint8_t v = *srcPtr;
            *dstPtr = v;
            srcPtr += dataElems;
            dstPtr += 1;
        }
    }
#endif

#if 1
    // ^256: 188 18.0
    // 
    // Fetch from groups of 4 channels at once, interleave, store to stack. Then final interleave and store to dest from there.
    // WinVS:
    //                      +16
    // 16:      132 18.3    122 14.5
    // 32:      135 17.7    132 14.0
    // 64:      134 19.2    134 15.0
    // 128:     158 19.8    137 13.7
    // 256:     144 20.4    122 14.4
    // 384:     127 18.0    118 13.4
    // 512:     149 21.4    123 14.1
    // 768:     130 19.5    117 13.5
    // 1024:    166 23.6    133 14.0
    // 2048:    162 23.6    131 14.5
    // 4096:    161 23.1    129 13.9

    const int kChunkBytes = 384;
    constexpr bool k16Ch = true;
    const int kChunkSimdSize = kChunkBytes / 16;
    static_assert((kChunkBytes % 16) == 0, "chunk bytes needs to be multiple of simd width");
    uint8_t* dstPtr = dst;
    int64_t ip = 0;
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
                Bytes16 d0 = SimdLoad(((const Bytes16*)(srcPtr + dataElems * 0)) + item);
                Bytes16 d1 = SimdLoad(((const Bytes16*)(srcPtr + dataElems * 1)) + item);
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


        if (channels == 16 && k16Ch)
        {
            // channels == 16 case is much simpler
            // read groups of data from stack, interleave, store
            for (int item = 0; item < kChunkSimdSize; ++item)
            {
                for (int chgrp = 0; chgrp < 4; ++chgrp)
                {
                    Bytes16 a0 = chdata[chgrp +  0][item];
                    Bytes16 a1 = chdata[chgrp +  4][item];
                    Bytes16 a2 = chdata[chgrp +  8][item];
                    Bytes16 a3 = chdata[chgrp + 12][item];
                    // now we want a 4x4 as-uint matrix transpose
                    Bytes16 b0 = SimdInterleave4L(a0, a2); Bytes16 b1 = SimdInterleave4R(a0, a2);
                    Bytes16 b2 = SimdInterleave4L(a1, a3); Bytes16 b3 = SimdInterleave4R(a1, a3);
                    Bytes16 c0 = SimdInterleave4L(b0, b2); Bytes16 c1 = SimdInterleave4R(b0, b2);
                    Bytes16 c2 = SimdInterleave4L(b1, b3); Bytes16 c3 = SimdInterleave4R(b1, b3);
                    // store c0..c3
                    SimdStore(dstPtr, c0); dstPtr += 16;
                    SimdStore(dstPtr, c1); dstPtr += 16;
                    SimdStore(dstPtr, c2); dstPtr += 16;
                    SimdStore(dstPtr, c3); dstPtr += 16;
                }
            }
        }
        else
        {
            // general case: interleave data
            uint8_t cur[kMaxChannels * kChunkBytes];
            uint32_t* cur32 = (uint32_t*)cur;
            for (int ib = 0; ib < kChunkBytes; ++ib)
            {
                for (int ich = 0; ich < channels; ich += 4)
                {
                    *cur32 = *(const uint32_t*)(((const uint8_t*)chdata) + ich * kChunkBytes + ib * 4);
                    cur32++;
                }
            }
            // store
            // the row address we want from "cur" is interleaved in a funky way due to 4-channels data fetch above.
            for (int item = 0; item < kChunkSimdSize; ++item)
            {
                for (int chgrp = 0; chgrp < 4; ++chgrp)
                {
                    uint8_t* curPtr = cur + (chgrp * kChunkSimdSize + item) * 4 * channels;
                    memcpy(dstPtr, curPtr, channels * 4);
                    dstPtr += channels * 4;
                }
            }
        }
    }

    // scalar loop for any non-multiple-of-16 remainder
    if (channels == 16 && k16Ch)
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
            // write into destination
            SimdStore(dstPtr, SimdLoadA(chdata));
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
                uint8_t v = *srcPtr;
                *dstPtr = v;
                srcPtr += dataElems;
                dstPtr += 1;
            }
        }
    }
#endif
}

#if 0

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

#endif
