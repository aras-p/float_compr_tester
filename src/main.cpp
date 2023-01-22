#include <stdio.h>
#include <vector>
#include <algorithm>
#include "compressors.h"

const int kWidth = 2048;
const int kHeight = 2048;
const int kChannels = 4;
const size_t kTotalFloats = kWidth * kHeight * kChannels;
static std::vector<float> g_FileData(kTotalFloats);

static std::vector<Compressor*> g_Compressors;


static void WriteTga(const char* path, int width, int height, const uint32_t* data)
{
	FILE* tga = fopen(path, "wb");
	uint8_t header[] = {
		0, // ID length
		0, // no color map
		2, // uncompressed, true color
		0, 0, 0, 0,
		0,
		0, 0, 0, 0, // x and y origin
		uint8_t(width & 0x00FF),
		uint8_t((width & 0xFF00) >> 8),
		uint8_t(height & 0x00FF),
		uint8_t((height & 0xFF00) >> 8),
		32, // bpp
		0
	};
	fwrite(header, 1, sizeof(header), tga);
	fwrite(data, 4, width*height, tga);
	fclose(tga);
}

static uint8_t MapFloat8(float v, float vmin, float vmax)
{
	return (uint8_t)((v - vmin) / (vmax - vmin) * 255.0f);
}
static uint16_t MapFloat16(float v, float vmin, float vmax)
{
	return (uint16_t)((v - vmin) / (vmax - vmin) * 65535.0f);
}

static void DumpInputVisualizations()
{
	// channel #0: -1251.028076 .. 199.367172 (avg 130.359882)
	// channel #1: -4.876561 .. 6.020788 (avg 0.001652)
	// channel #2: -9.831259 .. 0.339167 (avg 0.015836)
	// channel #3: -14.000000 .. 1.000000 (avg 0.211194)
	/*
	const float* data = g_FileData.data();
	double vmin[4] = { 1.0e10, 1.0e10, 1.0e10, 1.0e10 };
	double vmax[4] = { -1.0e10, -1.0e10, -1.0e10, -1.0e10 };
	double vsum[4] = { 0, 0, 0, 0 };
	for (int i = 0; i < kWidth * kHeight; ++i)
	{
		for (int j = 0; j < 4; ++j)
		{
			double v = *data++;
			vmin[j] = std::min(vmin[j], v);
			vmax[j] = std::max(vmax[j], v);
			vsum[j] += v;
		}
	}
	for (int j = 0; j < 4; ++j)
	{
		printf("  channel #%i: %f .. %f (avg %f)\n", j, vmin[j], vmax[j], vsum[j] / (kWidth * kHeight));
	}
	*/
	std::vector<uint32_t> height(kWidth * kHeight);
	std::vector<uint32_t> vel(kWidth * kHeight);
	std::vector<uint32_t> pol(kWidth * kHeight);
	std::vector<uint16_t> height16(kWidth * kHeight);
	const float* data = g_FileData.data();
	for (int i = 0; i < kWidth * kHeight; ++i)
	{
		float vx = data[0];
		float vy = data[1];
		float vz = data[2];
		float vw = data[3];
		uint32_t bx = MapFloat8(vx, -1252, 200);
		uint32_t by = MapFloat8(vy, -10, 10);
		uint32_t bz = MapFloat8(vz, -10, 10);
		uint32_t bw = MapFloat8(vw, -15, 2);
		height[i] = bx | (bx << 8) | (bx << 16) | 0xFF000000;
		vel[i] = by | (bz << 8) | 0x00800000 | 0xFF000000;
		pol[i] = bw | (bw << 8) | (bw << 16) | 0xFF000000;
		height16[i] = MapFloat16(vx, -1252, 200);
		data += 4;
	}
	WriteTga("outXHeight.tga", kWidth, kHeight, height.data());
	WriteTga("outYZVel.tga", kWidth, kHeight, vel.data());
	WriteTga("outWPol.tga", kWidth, kHeight, pol.data());

	FILE* fraw = fopen("outHeightRaw.raw", "wb");
	fwrite(height16.data(), 2, kWidth * kHeight, fraw);
	fclose(fraw);
}

static void TestCompressors()
{
	g_Compressors.emplace_back(new GenericCompressor(kCompressionZstd, 3));
	g_Compressors.emplace_back(new GenericCompressor(kCompressionZstd, 10));
	g_Compressors.emplace_back(new GenericCompressor(kCompressionLZ4, 0));

	std::vector<float> decompressed(kWidth * kHeight * kChannels);

	char cmpName[1000];
	printf("%-10s %6.3f MB\n", "Raw", kTotalFloats * 4 / (1024.0 * 1024.0));
	for (auto* cmp : g_Compressors)
	{
		size_t compressedSize = 0;
		uint8_t* compressed = cmp->Compress(g_FileData.data(), kWidth, kHeight, kChannels, compressedSize);
		memset(decompressed.data(), 0, 4 * decompressed.size());
		cmp->Decompress(compressed, compressedSize, decompressed.data(), kWidth, kHeight, kChannels);
		cmp->PrintName(1000, cmpName);
		printf("%-10s ", cmpName);
		if (memcmp(g_FileData.data(), decompressed.data(), 4 * decompressed.size()) != 0)
		{
			printf("ERROR, did not decompress back to input\n");
			continue;
		}
		printf("%6.3f MB\n", compressedSize / (1024.0 * 1024.0));
		delete[] compressed;
	}

	for (auto* cmp : g_Compressors)
		delete cmp;
	g_Compressors.clear();
}

int main()
{
	FILE* inFile = fopen("../../../data/2048_sq_float4.bin", "rb");
	if (inFile == nullptr)
	{
		printf("ERROR: failed to open data file\n");
		return 1;
	}
	size_t readFloats = fread(g_FileData.data(), sizeof(float), kTotalFloats, inFile);
	fclose(inFile);
	if (readFloats != kTotalFloats)
	{
		printf("ERROR: failed to read data file, expected %zi floats got %zi floats\n", kTotalFloats, readFloats);
		return 1;
	}

	//DumpInputVisualizations();
	TestCompressors();

	return 0;
}
