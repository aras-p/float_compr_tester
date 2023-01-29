#include <stdio.h>
#include <vector>
#include <algorithm>
#include "compressors.h"

#define SOKOL_TIME_IMPL
#include "../libs/sokol_time.h"

struct TestFile
{
	const char* path = nullptr;
	int width = 0;
	int height = 0;
	int channels = 0;
	std::vector<float> fileData;
};

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

static void DumpInputVisualizations(int width, int height, const float* data)
{
	double vmin[4] = { 1.0e10, 1.0e10, 1.0e10, 1.0e10 };
	double vmax[4] = { -1.0e10, -1.0e10, -1.0e10, -1.0e10 };
	double vsum[4] = { 0, 0, 0, 0 };
	const float* ptr = data;
	for (int i = 0; i < width * height; ++i)
	{
		for (int j = 0; j < 4; ++j)
		{
			double v = *ptr++;
			vmin[j] = std::min(vmin[j], v);
			vmax[j] = std::max(vmax[j], v);
			vsum[j] += v;
		}
	}
	for (int j = 0; j < 4; ++j)
	{
		printf("  channel #%i: %f .. %f (avg %f)\n", j, vmin[j], vmax[j], vsum[j] / (width * height));
	}

	// dump water images
	if (width == 2048)
	{
		// channel #0: -1251.028076 .. 199.367172 (avg 130.359882)
		// channel #1: -4.876561 .. 6.020788 (avg 0.001652)
		// channel #2: -9.831259 .. 0.339167 (avg 0.015836)
		// channel #3: -14.000000 .. 1.000000 (avg 0.211194)
		std::vector<uint32_t> waterHeight(width * height);
		std::vector<uint32_t> waterVel(width * height);
		std::vector<uint32_t> waterPol(width * height);
		std::vector<uint16_t> height16(width * height);
		for (int i = 0; i < width * height; ++i)
		{
			float vx = data[0];
			float vy = data[1];
			float vz = data[2];
			float vw = data[3];
			uint32_t bx = MapFloat8(vx, -1252, 200);
			uint32_t by = MapFloat8(vy, -10, 10);
			uint32_t bz = MapFloat8(vz, -10, 10);
			uint32_t bw = MapFloat8(vw, -15, 2);
			waterHeight[i] = bx | (bx << 8) | (bx << 16) | 0xFF000000;
			waterVel[i] = by | (bz << 8) | 0x00800000 | 0xFF000000;
			waterPol[i] = bw | (bw << 8) | (bw << 16) | 0xFF000000;
			height16[i] = MapFloat16(vx, 200, -1252);
			data += 4;
		}
		WriteTga("outXHeight.tga", width, height, waterHeight.data());
		WriteTga("outYZVel.tga", width, height, waterVel.data());
		WriteTga("outWPol.tga", width, height, waterPol.data());

		FILE* fraw = fopen("outHeightRaw.raw", "wb");
		fwrite(height16.data(), 2, width * height, fraw);
		fclose(fraw);
	}

	// dump snow images
	if (width == 1024)
	{
		// channel #0: 0 .. 3.115 (avg 0.000735) - snow
		// channel #1: 0 - snow in water
		// channel #2: -38973 .. -2004 - ground water
		// channel #3: 0 - unused
		std::vector<uint32_t> snow(width * height);
		std::vector<uint32_t> ground(width * height);
		std::vector<uint16_t> height16(width * height);
		for (int i = 0; i < width * height; ++i)
		{
			float vx = data[0];
			vx *= 30;
			vx = std::min(1.0f, vx);
			float vz = data[2];
			uint32_t bx = MapFloat8(vx, 0, 1);
			uint32_t bz = MapFloat8(vz, -38974, -2004);
			snow[i] = bx | (bx << 8) | (bx << 16) | 0xFF000000;
			ground[i] = bz | (bz << 8) | (bz << 16) | 0xFF000000;
			height16[i] = MapFloat16(vz, -38974, -2004);
			data += 4;
		}
		WriteTga("outSnow.tga", width, height, snow.data());
		WriteTga("outGround.tga", width, height, ground.data());

		FILE* fraw = fopen("outHeightRaw.raw", "wb");
		fwrite(height16.data(), 2, width * height, fraw);
		fclose(fraw);
	}
}

static void TestCompressors(size_t testFileCount, TestFile* testFiles)
{
	const int kRuns = 1;

	g_Compressors.emplace_back(new GenericCompressor(kCompressionZstd));
	//g_Compressors.emplace_back(new GenericCompressor(kCompressionLZ4));
	//g_Compressors.emplace_back(new GenericCompressor(kCompressionZlib));
	g_Compressors.emplace_back(new GenericCompressor(kCompressionBrotli));
	/*
	g_Compressors.emplace_back(new GenericCompressor(kCompressionZstd, kFilterSplitFloats));
	g_Compressors.emplace_back(new GenericCompressor(kCompressionLZ4,  kFilterSplitFloats));
	g_Compressors.emplace_back(new GenericCompressor(kCompressionZstd, kFilterSplitFloats | kFilterDeltaDiff));
	g_Compressors.emplace_back(new GenericCompressor(kCompressionZstd, kFilterSplitFloats | kFilterDeltaXor));
	g_Compressors.emplace_back(new GenericCompressor(kCompressionZstd, kFilterSplitBytes));
	g_Compressors.emplace_back(new GenericCompressor(kCompressionLZ4,  kFilterSplitBytes));
	g_Compressors.emplace_back(new GenericCompressor(kCompressionZstd, kFilterSplitBytes | kFilterDeltaDiff));
	g_Compressors.emplace_back(new GenericCompressor(kCompressionLZ4,  kFilterSplitBytes | kFilterDeltaDiff));
	g_Compressors.emplace_back(new GenericCompressor(kCompressionZstd, kFilterSplitBytes | kFilterDeltaXor));
	g_Compressors.emplace_back(new GenericCompressor(kCompressionLZ4,  kFilterSplitBytes | kFilterDeltaXor));

	g_Compressors.emplace_back(new MeshOptCompressor(kCompressionCount));
	g_Compressors.emplace_back(new MeshOptCompressor(kCompressionZstd));
	g_Compressors.emplace_back(new MeshOptCompressor(kCompressionCount, kFilterSplitFloats));
	g_Compressors.emplace_back(new MeshOptCompressor(kCompressionZstd,  kFilterSplitFloats));
	g_Compressors.emplace_back(new MeshOptCompressor(kCompressionCount, kFilterSplitFloats | kFilterDeltaDiff));
	g_Compressors.emplace_back(new MeshOptCompressor(kCompressionZstd,  kFilterSplitFloats | kFilterDeltaDiff));

	g_Compressors.emplace_back(new FpzipCompressor());
	g_Compressors.emplace_back(new ZfpCompressor());
	g_Compressors.emplace_back(new NdzipCompressor());
	g_Compressors.emplace_back(new StreamVByteCompressor(kCompressionCount, false));
	g_Compressors.emplace_back(new StreamVByteCompressor(kCompressionZstd, false));
	g_Compressors.emplace_back(new StreamVByteCompressor(kCompressionCount, true));
	g_Compressors.emplace_back(new StreamVByteCompressor(kCompressionZstd, true));
	*/

	/*
	g_Compressors.emplace_back(new GenericCompressor(kCompressionZstd, 3));											// 23.044 0.187 0.064
	g_Compressors.emplace_back(new GenericCompressor(kCompressionZstd, 10));										// 21.800 1.240 0.060
	g_Compressors.emplace_back(new GenericCompressor(kCompressionLZ4, 0));											// 32.669 0.062 0.016
	g_Compressors.emplace_back(new GenericCompressor(kCompressionZstd, 3, kFilterSplitFloats));						// 22.267 0.148 0.072
	g_Compressors.emplace_back(new GenericCompressor(kCompressionZstd, 10, kFilterSplitFloats));					// 21.670 0.474 0.069
	g_Compressors.emplace_back(new GenericCompressor(kCompressionLZ4, 0, kFilterSplitFloats));						// 27.306 0.052 0.034
	g_Compressors.emplace_back(new GenericCompressor(kCompressionZstd, 3, kFilterSplitFloats | kFilterDeltaDiff));	// 16.295 0.181 0.080
	g_Compressors.emplace_back(new GenericCompressor(kCompressionZstd, 10, kFilterSplitFloats | kFilterDeltaDiff));	// 15.403 0.805 0.086
	g_Compressors.emplace_back(new GenericCompressor(kCompressionZstd, 3, kFilterSplitFloats | kFilterDeltaXor));	// 17.472 0.189 0.085
	g_Compressors.emplace_back(new GenericCompressor(kCompressionZstd, 10, kFilterSplitFloats | kFilterDeltaXor));	// 16.743 0.798 0.084
	g_Compressors.emplace_back(new GenericCompressor(kCompressionZstd, 3, kFilterSplitBytes));						// 15.142 0.161 0.096
	g_Compressors.emplace_back(new GenericCompressor(kCompressionZstd, 10, kFilterSplitBytes));						// 14.338 0.606 0.092
	g_Compressors.emplace_back(new GenericCompressor(kCompressionLZ4, 0, kFilterSplitBytes));						// 17.791 0.093 0.078
	g_Compressors.emplace_back(new GenericCompressor(kCompressionZstd, 3, kFilterSplitBytes | kFilterDeltaDiff));	// 13.415 0.188 0.127
	g_Compressors.emplace_back(new GenericCompressor(kCompressionZstd, 10, kFilterSplitBytes | kFilterDeltaDiff));	// 12.864 0.731 0.119
	g_Compressors.emplace_back(new GenericCompressor(kCompressionLZ4, 0, kFilterSplitBytes | kFilterDeltaDiff));	// 17.000 0.119 0.098
	g_Compressors.emplace_back(new GenericCompressor(kCompressionZstd, 3, kFilterSplitBytes | kFilterDeltaXor));	// 14.081 0.191 0.121
	g_Compressors.emplace_back(new GenericCompressor(kCompressionZstd, 10, kFilterSplitBytes | kFilterDeltaXor));	// 13.522 0.695 0.117
	g_Compressors.emplace_back(new GenericCompressor(kCompressionLZ4, 0, kFilterSplitBytes | kFilterDeltaXor));		// 17.637 0.117 0.101

	g_Compressors.emplace_back(new MeshOptCompressor(kCompressionCount, 0));										// 17.535 0.113 0.017
	g_Compressors.emplace_back(new MeshOptCompressor(kCompressionZstd, 3));											// 14.324 0.221 0.034
	g_Compressors.emplace_back(new MeshOptCompressor(kCompressionZstd, 10));										// 13.786 0.459 0.035
	g_Compressors.emplace_back(new MeshOptCompressor(kCompressionCount, 0, kFilterSplitFloats));					// 17.535 0.142 0.041
	g_Compressors.emplace_back(new MeshOptCompressor(kCompressionZstd, 3, kFilterSplitFloats));						// 13.896 0.248 0.057
	g_Compressors.emplace_back(new MeshOptCompressor(kCompressionZstd, 10, kFilterSplitFloats));					// 13.485 0.488 0.058
	g_Compressors.emplace_back(new MeshOptCompressor(kCompressionCount, 0, kFilterSplitFloats | kFilterDeltaDiff));	// 17.802 0.193 0.064
	g_Compressors.emplace_back(new MeshOptCompressor(kCompressionZstd, 3, kFilterSplitFloats | kFilterDeltaDiff));	// 13.959 0.258 0.067
	g_Compressors.emplace_back(new MeshOptCompressor(kCompressionZstd, 10, kFilterSplitFloats | kFilterDeltaDiff));	// 13.632 0.496 0.073

	g_Compressors.emplace_back(new FpzipCompressor());																// 46.544 0.511 0.559
	g_Compressors.emplace_back(new ZfpCompressor());																// 59.872 0.256 0.152
	g_Compressors.emplace_back(new NdzipCompressor());																// 52.415 0.206 0.232
	g_Compressors.emplace_back(new StreamVByteCompressor(kCompressionCount, 0, false));								// 51.525 0.011 0.007
	g_Compressors.emplace_back(new StreamVByteCompressor(kCompressionZstd, 3, false));								// 24.358 0.163 0.079
	g_Compressors.emplace_back(new StreamVByteCompressor(kCompressionCount, 0, true));								// 62.582 0.014 0.008
	g_Compressors.emplace_back(new StreamVByteCompressor(kCompressionZstd, 3, true));								// 32.662 0.129 0.086
	*/

	size_t maxFloats = 0, totalFloats = 0;
	for (int tfi = 0; tfi < testFileCount; ++tfi)
	{
		int width = testFiles[tfi].width;
		int height = testFiles[tfi].height;
		int channels = testFiles[tfi].channels;
		size_t floats = width * height * channels;
		maxFloats = std::max(maxFloats, floats);
		totalFloats += floats;
	}

	std::vector<float> decompressed(maxFloats);

	struct Result
	{
		size_t size = 0;
		double cmpTime = 0;
		double decTime = 0;
	};
	typedef std::vector<Result> LevelResults;
	std::vector<LevelResults> results;
	for (auto* cmp : g_Compressors)
	{
		int levelMin, levelMax;
		cmp->GetLevelRange(levelMin, levelMax);
		LevelResults res(levelMax - levelMin + 1);
		results.emplace_back(res);
	}

	char cmpName[1000];
	for (int ir = 0; ir < kRuns; ++ir)
	{
		printf("Run %i/%i, %zi compressors on %zi files:\n", ir+1, kRuns, g_Compressors.size(), testFileCount);
		for (size_t ic = 0; ic < g_Compressors.size(); ++ic)
		{
			Compressor* cmp = g_Compressors[ic];
			cmp->PrintName(sizeof(cmpName), cmpName);
			int levelMin, levelMax;
			cmp->GetLevelRange(levelMin, levelMax);
			printf("%s: %i levels: ", cmpName, levelMax-levelMin+1);
			for (int level = levelMin; level <= levelMax; ++level)
			{
				if (cmp->ShouldSkipLevel(level))
					continue;
				printf(".");
				for (int tfi = 0; tfi < testFileCount; ++tfi)
				{
					const TestFile& tf = testFiles[tfi];

					// compress
					size_t compressedSize = 0;
					uint64_t t0 = stm_now();
					uint8_t* compressed = cmp->Compress(level, tf.fileData.data(), tf.width, tf.height, tf.channels, compressedSize);
					double tComp = stm_sec(stm_since(t0));

					// decompress
					memset(decompressed.data(), 0, 4 * tf.fileData.size());
					t0 = stm_now();
					cmp->Decompress(compressed, compressedSize, decompressed.data(), tf.width, tf.height, tf.channels);
					double tDecomp = stm_sec(stm_since(t0));

					// stats
					auto& res = results[ic][level - levelMin];
					res.size += compressedSize;
					res.cmpTime += tComp;
					res.decTime += tDecomp;

					// check validity
					if (memcmp(tf.fileData.data(), decompressed.data(), 4 * tf.fileData.size()) != 0)
					{
						cmp->PrintName(sizeof(cmpName), cmpName);
						printf("  ERROR, %s level %i did not decompress back to input\n", cmpName, level);
						exit(1);
					}
					delete[] compressed;
				}
			}
			printf("\n");
		}
		printf("\n");
	}

	double oneGB = 1024.0 * 1024.0 * 1024.0;
	double rawSize = (double)(totalFloats * 4);
	// print results to screen
	/*
	printf("Compressor             SizeMB CTimeS  DTimeS Ratio CGB/s DGB/s\n");
	printf("%-22s %7.3f\n", "Raw", rawSize / oneGB);
	for (size_t ic = 0; ic < g_Compressors.size(); ++ic)
	{
		Compressor* cmp = g_Compressors[ic];
		cmp->PrintName(sizeof(cmpName), cmpName);
		double csize = (double)(sizes[ic] / kRuns);
		double ctime = cmpTimes[ic] / kRuns;
		double dtime = decompTimes[ic] / kRuns;
		double ratio = rawSize / csize;
		double cspeed = rawSize / ctime;
		double dspeed = rawSize / dtime;
		printf("%-22s %7.3f %6.3f %6.3f %6.3f %5.0f %5.0f\n", cmpName, csize / oneGB, ctime, dtime, ratio, cspeed / oneGB, dspeed / oneGB);
	}
	*/

	// print to HTML report page
	FILE* fout = fopen("../../report.html", "wb");
	fprintf(fout, "<script type='text/javascript' src='https://www.gstatic.com/charts/loader.js'></script>\n");
	fprintf(fout, "<center style='font-family: Arial;'>\n");
	fprintf(fout, "<div style='border: 1px solid #ccc; width: 1290px;'>\n");
	fprintf(fout, "<div id='chart_cmp' style='width: 640px; height: 480px; display:inline-block;'></div>\n");
	fprintf(fout, "<div id='chart_dec' style='width: 640px; height: 480px; display:inline-block;'></div>\n");
	fprintf(fout, "</div>\n");
	fprintf(fout, "<script type='text/javascript'>\n");
	fprintf(fout, "google.charts.load('current', {'packages':['corechart']});\n");
	fprintf(fout, "google.charts.setOnLoadCallback(drawChart);\n");
	fprintf(fout, "function drawChart() {\n");
	fprintf(fout, "var dataCmp = new google.visualization.DataTable();\n");
	fprintf(fout, "var dataDec = new google.visualization.DataTable();\n");
	fprintf(fout, "dataCmp.addColumn('number', 'Throughput');\n");
	fprintf(fout, "dataDec.addColumn('number', 'Throughput');\n");
	for (auto* cmp : g_Compressors)
	{
		cmp->PrintName(sizeof(cmpName), cmpName);
		fprintf(fout, "dataCmp.addColumn('number', '%s'); dataCmp.addColumn({type:'string', role:'tooltip'}); dataCmp.addColumn({type:'string', role:'style'});\n", cmpName);
		fprintf(fout, "dataDec.addColumn('number', '%s'); dataDec.addColumn({type:'string', role:'tooltip'}); dataDec.addColumn({type:'string', role:'style'});\n", cmpName);
	}
	fprintf(fout, "dataCmp.addRows([\n");
	for (size_t ic = 0; ic < g_Compressors.size(); ++ic)
	{
		Compressor* cmp = g_Compressors[ic];
		cmp->PrintName(sizeof(cmpName), cmpName);
		int levelMin, levelMax;
		cmp->GetLevelRange(levelMin, levelMax);
		for (size_t ir = 0; ir < results[ic].size(); ++ir)
		{
			if (cmp->ShouldSkipLevel(ir + levelMin))
				continue;
			const Result& res = results[ic][ir];
			double csize = (double)(res.size / kRuns);
			double ctime = res.cmpTime / kRuns;
			//double dtime = res.decTime / kRuns;
			double ratio = rawSize / csize;
			double cspeed = rawSize / ctime;
			//double dspeed = rawSize / dtime;
			fprintf(fout, "  [%.3f", cspeed / oneGB);
			for (size_t j = 0; j < ic; ++j) fprintf(fout, ",null,null,null");
			fprintf(fout, ", %.3f,'%s", ratio, cmpName);
			if (levelMin != levelMax)
				fprintf(fout, " %i", (int)(levelMin + ir));
			fprintf(fout, "\\n%.3fx at %.3f GB/s','' ", ratio, cspeed / oneGB);
			for (size_t j = ic + 1; j < g_Compressors.size(); ++j) fprintf(fout, ",null,null,null");
			fprintf(fout, "]%s\n", (ic == g_Compressors.size() - 1) && (ir == results[ic].size() - 1) ? "" : ",");
		}
	}
	fprintf(fout, "]);\n");
	fprintf(fout, "dataDec.addRows([\n");
	for (size_t ic = 0; ic < g_Compressors.size(); ++ic)
	{
		Compressor* cmp = g_Compressors[ic];
		cmp->PrintName(sizeof(cmpName), cmpName);
		int levelMin, levelMax;
		cmp->GetLevelRange(levelMin, levelMax);
		for (size_t ir = 0; ir < results[ic].size(); ++ir)
		{
			if (cmp->ShouldSkipLevel(ir + levelMin))
				continue;
			const Result& res = results[ic][ir];
			double csize = (double)(res.size / kRuns);
			//double ctime = res.cmpTime / kRuns;
			double dtime = res.decTime / kRuns;
			double ratio = rawSize / csize;
			//double cspeed = rawSize / ctime;
			double dspeed = rawSize / dtime;
			fprintf(fout, "  [%.3f", dspeed / oneGB);
			for (size_t j = 0; j < ic; ++j) fprintf(fout, ",null,null,null");
			fprintf(fout, ", %.3f,'%s", ratio, cmpName);
			if (levelMin != levelMax)
				fprintf(fout, " %i", (int)(levelMin + ir));
			fprintf(fout, "\\n%.3fx at %.3f GB/s','' ", ratio, dspeed / oneGB);
			for (size_t j = ic + 1; j < g_Compressors.size(); ++j) fprintf(fout, ",null,null,null");
			fprintf(fout, "]%s\n", (ic == g_Compressors.size() - 1) && (ir == results[ic].size() - 1) ? "" : ",");
		}
	}
	fprintf(fout, "]);\n");
	fprintf(fout, "var titleDec = 'Decompression Ratio vs Throughput';\n");
	fprintf(fout, "var options = {\n");
	fprintf(fout, "title: 'Compression Ratio vs Throughput',\n");
	fprintf(fout, "pointSize: 6,\n");
	fprintf(fout, "series: {\n");
	for (size_t ic = 0; ic < g_Compressors.size(); ++ic)
	{
		const Compressor* cmp = g_Compressors[ic];
		fprintf(fout, "  %zi: {pointShape: %s},\n", ic, cmp->GetShapeString());
	}
	fprintf(fout, "  %zi: {},\n", g_Compressors.size());
	fprintf(fout, "},\n");
	fprintf(fout, "colors: [");
	for (size_t ic = 0; ic < g_Compressors.size(); ++ic)
	{
		const Compressor* cmp = g_Compressors[ic];
		uint32_t col = cmp->GetColor();
		fprintf(fout, "'%02x%02x%02x'%s", (col >> 16)&0xFF, (col >> 8)&0xFF, col&0xFF, ic== g_Compressors.size()-1?"":",");
	}
	fprintf(fout, "],\n");
	fprintf(fout, "hAxis: {title: 'Compression GB/s', logScale: true, viewWindow: {min:0.0, max:5.0}},\n");
	fprintf(fout, "vAxis: {title: 'Ratio', viewWindow: {min:1}},\n");
	fprintf(fout, "chartArea: {left:60, right:10, top:50, bottom:50},\n");
	fprintf(fout, "legend: {position: 'top'},\n");
	fprintf(fout, "lineWidth: 1\n");
	fprintf(fout, "};\n");
	fprintf(fout, "var chartCmp = new google.visualization.ScatterChart(document.getElementById('chart_cmp'));\n");
	fprintf(fout, "chartCmp.draw(dataCmp, options);\n");
	fprintf(fout, "options.title = titleDec;\n");
	fprintf(fout, "options.hAxis.title = 'Decompression GB/s';\n");
	fprintf(fout, "options.hAxis.viewWindow.max = 10.0;\n");
	fprintf(fout, "var chartDec = new google.visualization.ScatterChart(document.getElementById('chart_dec'));\n");
	fprintf(fout, "chartDec.draw(dataDec, options);\n");
	fprintf(fout, "}\n");
	fprintf(fout, "</script>\n");
	fclose(fout);

	// cleanup
	for (size_t ic = 0; ic < g_Compressors.size(); ++ic)
	{
		Compressor* cmp = g_Compressors[ic];
		delete cmp;
	}
	g_Compressors.clear();
}

int main()
{
	stm_setup();

	TestFile testFiles[] = {
		{"../../../data/2048_sq_float4.bin", 2048, 2048, 4}, // water sim: X height, Y&Z velocity, W pollution
		{"../../../data/1024_sq_float4.bin", 1024, 1024, 4}, // snow sim: X amount, Y in water amount, Z ground height, W unused
		{"../../../data/232630_float4.bin", 232630, 1, 4}, // all sorts of float4 data (quaternions, colors, etc.)
		{"../../../data/953134_float3.bin", 953134, 1, 3}, // all sorts of float3 data (positions, scales, directions, ...)
	};
	for (auto& tf : testFiles)
	{
		FILE* inFile = fopen(tf.path, "rb");
		if (inFile == nullptr)
		{
			printf("ERROR: failed to open data file %s\n", tf.path);
			return 1;
		}
		tf.fileData.resize(tf.width * tf.height * tf.channels);
		size_t readFloats = fread(tf.fileData.data(), sizeof(float), tf.fileData.size(), inFile);
		fclose(inFile);
		if (readFloats != tf.fileData.size())
		{
			printf("ERROR: failed to read data file, expected %zi floats got %zi floats\n", tf.fileData.size(), readFloats);
			return 1;
		}

		//DumpInputVisualizations(tf.width, tf.height, tf.fileData.data());
	}

	TestCompressors(std::size(testFiles), testFiles);
	return 0;
}
