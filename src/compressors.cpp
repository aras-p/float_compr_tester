#include "compressors.h"
#include <stdio.h>

#include <fpzip.h>
#include <zfp.h>
#include "../libs/bitshuffle/src/bitshuffle_core.h"
#include "../libs/spdp/spdp_11.h"

//#include <assert.h> // ndzip needs it
//#include <ndzip/ndzip.hh>
//#include <streamvbyte.h>
//#include <streamvbytedelta.h>

#include <string>

static std::vector<int> GetGenericLevelRange(CompressionFormat format)
{
	switch (format)
	{
	case kCompressionZstd:
		//return { -5, -3, -1, 1, 3, 5, 7, 9, 12, 15, 18, 22 };
		return { -5, -3, -1, 1, 3, 5, 7, 9, 12, 15 }; // comp time under 3s
	case kCompressionLZ4:
		//return { -5, -1, 0, 1, 6, 9, 12 };
		return { -5, -1, 0, 1, 6, 9 }; // comp time under 3s
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
		return { -4, -3, -2, -1, 1, 2, 3, 4, 5 }; // comp time under 3s
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
			Split((uint8_t*)data, tmp, channels * sizeof(float), planeElems);
			if ((filter & kFilterDeltaDiff) != 0) EncodeDeltaDif((uint8_t*)tmp, dataSize);
			if ((filter & kFilterDeltaXor) != 0) EncodeDeltaXor((uint8_t*)tmp, dataSize);
		}
		if ((filter & kFilterBitShuffle) != 0)
		{
			bshuf_bitshuffle(data, tmp, planeElems, channels * sizeof(float), 0);
			if ((filter & kFilterDeltaDiff) != 0) EncodeDeltaDif((uint8_t*)tmp, dataSize);
			if ((filter & kFilterDeltaXor) != 0) EncodeDeltaXor((uint8_t*)tmp, dataSize);
		}
	}
	return tmp;
}

static void DecompressionFilter(uint32_t filter, uint8_t* tmp, float* data, int width, int height, int channels)
{
	if (filter != kFilterNone)
	{
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
	bool faded = true; // (m_Filter & kFilterSplit8) == 0;
	if (m_Format == kCompressionZstd) return faded ? 0x90d596 : 0x0c9618; // green
	if (m_Format == kCompressionLZ4) return faded ? 0xd9d18c : 0xb19f00; // yellow
	if (m_Format == kCompressionZlib) return faded ? 0x8cd9cf : 0x00bfa7; // cyan
	if (m_Format == kCompressionLibdeflate) return 0x00786a; // cyan
	if (m_Format == kCompressionBrotli) return faded ? 0xd19a94 : 0xde5546; // orange
	// purple
	if (m_Format == kCompressionOoodleSelkie)	return 0xffb0ff;
	if (m_Format == kCompressionOoodleMermaid)	return 0xdc74ff;
	if (m_Format == kCompressionOoodleKraken)	return faded ? 0xc4b6c9 : 0x8a4b9d; // dark purple regular: 0x8a4b9d lighter: 0xc4b6c9
	return 0;
}

static const char* GetGenericShape(uint filter)
{
	if (filter == 0) return "'circle', lineDashStyle: [4, 2]";
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
	if (m_Format == kCompressionZstd) return 0x00b2ff;
	if (m_Format == kCompressionLZ4) return 0x49ddff;
	if (m_Format == kCompressionOoodleKraken) return 0x0094ef;
	return 0x006fb1;
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
const char* SpdpCompressor::GetShapeString() const { return "{type:'star', sides:6}, pointSize: 10"; }


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


/*
uint8_t* NdzipCompressor::Compress(int level, const float* data, int width, int height, int channels, size_t& outSize)
{
	// ndzip seems to have trouble if we try to do channels*width*height 3 dimensions
	// (does not handle dimension size < 16?), so do a 2D case instead
	ndzip::extent ext(2);
	ext[0] = width * channels;
	ext[1] = height;
	auto compressor = ndzip::make_compressor<float>(2, 1);
	size_t bound = ndzip::compressed_length_bound<float>(ext) * 4;
	uint8_t* cmp = new uint8_t[bound];
	size_t cmpSize = compressor->compress(data, ext, (uint32_t*)cmp) * 4;
	outSize = cmpSize;
	return cmp;
}

void NdzipCompressor::Decompress(const uint8_t* cmp, size_t cmpSize, float* data, int width, int height, int channels)
{
	ndzip::extent ext(2);
	ext[0] = width * channels;
	ext[1] = height;
	auto decompressor = ndzip::make_decompressor<float>(2, 1);
	decompressor->decompress((const uint32_t*)cmp, data, ext);
}

void NdzipCompressor::PrintName(size_t bufSize, char* buf) const
{
	snprintf(buf, bufSize, "ndzip");
}

uint8_t* StreamVByteCompressor::Compress(int level, const float* data, int width, int height, int channels, size_t& outSize)
{
	uint32_t dataElems = width * height * channels;
	size_t bound = streamvbyte_max_compressedbytes(dataElems);
	uint8_t* cmp = new uint8_t[bound];
	size_t cmpSize = 0;
	if (m_Delta)
		cmpSize = streamvbyte_delta_encode((const uint32_t*)data, dataElems, cmp, 0);
	else
		cmpSize = streamvbyte_encode((const uint32_t*)data, dataElems, cmp);

	return CompressGeneric(m_Format, level, cmp, cmpSize, outSize);
}

void StreamVByteCompressor::Decompress(const uint8_t* cmp, size_t cmpSize, float* data, int width, int height, int channels)
{
	size_t decompSize;
	uint8_t* decomp = DecompressGeneric(m_Format, cmp, cmpSize, decompSize);

	uint32_t dataElems = width * height * channels;
	if (m_Delta)
		streamvbyte_delta_decode(decomp, (uint32_t*)data, dataElems, 0);
	else
		streamvbyte_decode(decomp, (uint32_t*)data, dataElems);
	if (decomp != cmp) delete[] decomp;
}

std::vector<int> StreamVByteCompressor::GetLevels() const
{
	return GetGenericLevelRange(m_Format);
}

void StreamVByteCompressor::PrintName(size_t bufSize, char* buf) const
{
	if (m_Format == kCompressionCount)
		snprintf(buf, bufSize, "streamvbyte%s", m_Delta ? "_d" : "");
	else
		snprintf(buf, bufSize, "streamvbyte-%s%s", kCompressionFormatNames[m_Format], m_Delta ? "_d" : "");	
}
*/
