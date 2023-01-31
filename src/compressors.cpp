#include "compressors.h"
#include <stdio.h>

#include <fpzip.h>
#include <zfp.h>
#include "../libs/bitshuffle/src/bitshuffle_core.h"

//#include <assert.h> // ndzip needs it
//#include <ndzip/ndzip.hh>
//#include <streamvbyte.h>
//#include <streamvbytedelta.h>

static std::vector<int> GetGenericLevelRange(CompressionFormat format)
{
	switch (format)
	{
	case kCompressionZstd:
		//return { -5, -3, -1, 1, 3, 5, 7, 9, 12, 15, 18, 22 };
		return { -5, -3, -1, 1, 3, 5, 7, 9 }; // comp time under 2s
		//return { -5, 3, 5 };
	case kCompressionLZ4:
		//return { -5, -1, 0, 1, 6, 9, 12 };
		return { -5, -1, 0, 1, 6, 9 }; // comp time under 2s
		//return { -5, 0, 1 };
	case kCompressionZlib:
		return { 1, 3, 5, 6, 7, 9 };
		//return { 1, 3, 5, 6 }; // comp time under 2s
		//return { 1, 6, 9 };
	case kCompressionBrotli:
		return { 0, 1, 2, 4, 5, 6, 9, 10, 11 };
		//return { 0, 2, 4 }; // comp time under 2s
	case kCompressionLibdeflate:
		return { 1, 3, 5, 6, 9, 10, 12 };
	case kCompressionOoodleSelkie:
	case kCompressionOoodleMermaid:
	case kCompressionOoodleKraken:
		return { -4, -3, -2, -1, 1, 2, 3, 4, 5, 6, 7, 8 };
	default:
		return { 0 };
	}
}


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


static uint8_t* CompressionFilter(uint32_t filter, const float* data, int width, int height, int channels)
{
	uint8_t* tmp = (uint8_t*)data;
	if (filter != kFilterNone)
	{
		int planeElems = width * height;
		int dataSize = planeElems * channels * sizeof(float);
		tmp = new uint8_t[dataSize];
		if ((filter & kFilterSplit32) != 0)
		{
			int dataElems = planeElems * channels;
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
		int planeElems = width * height;
		if ((filter & kFilterSplit32) != 0)
		{
			int dataElems = planeElems * channels;
			if ((filter & kFilterDeltaDiff) != 0) DecodeDeltaDif((uint32_t*)tmp, dataElems);
			if ((filter & kFilterDeltaXor) != 0) DecodeDeltaXor((uint32_t*)tmp, dataElems);
			UnSplit((const uint32_t*)tmp, (uint32_t*)data, channels, planeElems);
		}
		if ((filter & kFilterSplit8) != 0)
		{
			int dataSize = planeElems * channels * sizeof(float);
			if ((filter & kFilterDeltaDiff) != 0) DecodeDeltaDif(tmp, dataSize);
			if ((filter & kFilterDeltaXor) != 0) DecodeDeltaXor(tmp, dataSize);
			UnSplit(tmp, (uint8_t*)data, channels * sizeof(float), planeElems);
		}
		if ((filter & kFilterBitShuffle) != 0)
		{
			int dataSize = planeElems * channels * sizeof(float);
			if ((filter & kFilterDeltaDiff) != 0) DecodeDeltaDif(tmp, dataSize);
			if ((filter & kFilterDeltaXor) != 0) DecodeDeltaXor(tmp, dataSize);
			bshuf_bitunshuffle(tmp, data, planeElems, channels * sizeof(float), 0);
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
	if (m_Filter != kFilterNone) tmp = new uint8_t[dataSize];

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

void GenericCompressor::PrintName(size_t bufSize, char* buf) const
{
	const char* split = "";
	if ((m_Filter & kFilterSplit32) != 0) split = "-s32";
	if ((m_Filter & kFilterSplit8) != 0) split = "-s8";
	if ((m_Filter & kFilterBitShuffle) != 0) split = "-s1";
	const char* delta = "";
	if ((m_Filter & kFilterDeltaDiff) != 0) delta = "-dif";
	if ((m_Filter & kFilterDeltaXor) != 0) delta = "-xor";
	snprintf(buf, bufSize, "%s%s%s", kCompressionFormatNames[m_Format], split, delta);
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
	bool faded = (m_Filter & kFilterSplit8) == 0;
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
	if ((filter & kFilterSplit8) && (filter & kFilterDeltaDiff)) return "{type:'square', rotation: 45}, pointSize: 8, lineWidth: 2";
	if ((filter & kFilterSplit8) && (filter & kFilterDeltaXor))  return "{type:'star', sides:4, dent: 0.5}, pointSize: 8, lineWidth: 2";
	if ((filter & kFilterSplit8)) return "'square', pointSize: 8, lineDashStyle: [4, 4], lineWidth: 1";
	if ((filter & kFilterSplit32) && (filter & kFilterDeltaDiff)) return "{type:'triangle', rotation: 30}, pointSize: 10, lineDashStyle: [4, 4], lineWidth: 1";
	if ((filter & kFilterSplit32) && (filter & kFilterDeltaXor))  return "{type:'triangle', rotation: -30}, pointSize: 10, lineDashStyle: [4, 4], lineWidth: 1";
	if ((filter & kFilterSplit32)) return "'triangle', pointSize: 10, lineDashStyle: [4, 4], lineWidth: 1";
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
	uint8_t* cmp = new uint8_t[bound];
	outSize = compress_data(data, dataSize, cmp, bound, format, level);
	delete[] data;
	return cmp;
}

/*
static uint8_t* DecompressGeneric(CompressionFormat format, const uint8_t* cmp, size_t cmpSize, size_t& outSize)
{
	if (format == kCompressionCount)
	{
		outSize = cmpSize;
		return (uint8_t*)cmp;
	}
	size_t bound = decompress_calc_bound(cmp, cmpSize, format);
	uint8_t* decomp = new uint8_t[bound];
	outSize = decompress_data(cmp, cmpSize, decomp, bound, format);
	return decomp;
}
*/

/*
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
	if (m_Filter != kFilterNone) tmp = new uint8_t[dataSize];
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
	const char* split = "";
	if ((m_Filter & kFilterSplit32) != 0) split = "-s32";
	if ((m_Filter & kFilterSplit8) != 0) split = "-s8";
	const char* delta = "";
	if ((m_Filter & kFilterDeltaDiff) != 0) delta = "-dif";
	if ((m_Filter & kFilterDeltaXor) != 0) delta = "-xor";
	if (m_Format == kCompressionCount)
		snprintf(buf, bufSize, "meshopt%s%s", split, delta);
	else
		snprintf(buf, bufSize, "meshopt-%s%s%s", kCompressionFormatNames[m_Format], split, delta);
}

uint32_t MeshOptCompressor::GetColor() const
{
	// blue
	if (m_Format == kCompressionZstd) return 0x00b2ff;
	if (m_Format == kCompressionLZ4) return 0x49ddff;
	return 0x006fb1;
}

const char* MeshOptCompressor::GetShapeString() const
{
	return GetGenericShape(m_Filter);
}
*/

uint8_t* FpzipCompressor::Compress(int level, const float* data, int width, int height, int channels, size_t& outSize)
{
	size_t dataSize = width * height * channels * sizeof(float);
	size_t bound = dataSize; //@TODO: what's the max compression bound?
	uint8_t* cmp = new uint8_t[bound];
	FPZ* fpz = fpzip_write_to_buffer(cmp, bound);
	fpz->type = FPZIP_TYPE_FLOAT;
	fpz->prec = 0;
	fpz->nx = width;
	fpz->ny = height;
	fpz->nz = 1;
	fpz->nf = channels;
	size_t cmpSize = fpzip_write(fpz, data);
	fpzip_write_close(fpz);
	outSize = cmpSize;
	return cmp;
}

void FpzipCompressor::Decompress(const uint8_t* cmp, size_t cmpSize, float* data, int width, int height, int channels)
{
	FPZ* fpz = fpzip_read_from_buffer(cmp);
	fpz->type = FPZIP_TYPE_FLOAT;
	fpz->prec = 0;
	fpz->nx = width;
	fpz->ny = height;
	fpz->nz = 1;
	fpz->nf = channels;
	fpzip_read(fpz, data);
	fpzip_read_close(fpz);
}

void FpzipCompressor::PrintName(size_t bufSize, char* buf) const
{
	snprintf(buf, bufSize, "fpzip");
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
	snprintf(buf, bufSize, "zfp");
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
