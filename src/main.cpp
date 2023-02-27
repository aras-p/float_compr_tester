#include <stdio.h>
#include <vector>
#include <algorithm>
#include "compressors.h"
#include "compression_helpers.h"
#include "filters.h"
#include "systeminfo.h"
#include "resultcache.h"
#include <set>
#include <math.h>
#include <memory>

#define SOKOL_TIME_IMPL
#include "../libs/sokol_time.h"

constexpr bool kWriteResultsCache = true;
const int kRuns = 3;

struct FilterDesc
{
	const char* name;
	void (*filterFunc)(const uint8_t* src, uint8_t* dst, int channels, size_t dataElems);
	void (*unfilterFunc)(const uint8_t* src, uint8_t* dst, int channels, size_t dataElems);
};
static FilterDesc g_Filters[] =
{
	{ "0-memcpy", Filter_Null, UnFilter_Null },
	//{ "1-Shuffle", Filter_Shuffle, UnFilter_Shuffle },
	{ "A-simple", Filter_A, UnFilter_A },
	{ "B-part6", Filter_B, UnFilter_B },
	{ "D-part6", Filter_D, UnFilter_D },
	{ "F-sep", Filter_F, UnFilter_F },
	{ "G-sep", Filter_F, UnFilter_G },
	{ "H-16xCh", Filter_H, UnFilter_H },
	{ "I-16x16", Filter_H, UnFilter_I },
	{ "J-256xCh", Filter_H, UnFilter_J },
	{ "K-384xCh-4x", Filter_H, UnFilter_K },
};
constexpr int kFilterCount = sizeof(g_Filters) / sizeof(g_Filters[0]);

static FilterDesc g_FilterSplit8AndDeltaDiff = {"-s8-dif", Filter_A, UnFilter_A }; // part 3 / part 6 beginning
static FilterDesc g_FilterSplit8Delta = { "-s8dif", Filter_D, UnFilter_D }; // part 6 end
static FilterDesc g_FilterSplit8DeltaOpt = { "-s8dopt", Filter_H, UnFilter_K };

static std::unique_ptr<GenericCompressor> g_CompZstd = std::make_unique<GenericCompressor>(kCompressionZstd);
static std::unique_ptr<GenericCompressor> g_CompLZ4 = std::make_unique<GenericCompressor>(kCompressionLZ4);
#if BUILD_WITH_OODLE
static std::unique_ptr<GenericCompressor> g_CompKraken = std::make_unique<GenericCompressor>(kCompressionOoodleKraken);
#endif

struct TestFile
{
	const char* path = nullptr;
	int width = 0;
	int height = 0;
	int channels = 0;
	std::vector<float> fileData;
};

struct CompressorConfig
{
	Compressor* cmp;
	FilterDesc* filter;

	std::string GetName() const
	{
		char buf[100];
		cmp->PrintName(sizeof(buf), buf);
		std::string res = buf;
		if (filter != nullptr)
			res += filter->name;
		return res;
	}
	const char* GetShapeString() const
	{
		if (filter == &g_FilterSplit8DeltaOpt) return "'circle', pointSize: 12, lineWidth: 3";
		if (filter == &g_FilterSplit8Delta) return "'circle'";
		if (filter == &g_FilterSplit8AndDeltaDiff) return "{type:'square', rotation: 45}, lineDashStyle: [4, 4]";
		if (filter == nullptr) return "'circle', lineDashStyle: [4, 2]";
		return "'circle'";
	}
	uint32_t GetColor() const
	{
		// https://www.w3schools.com/colors/colors_picker.asp
		bool faded = filter != &g_FilterSplit8DeltaOpt;
		if (cmp == g_CompZstd.get()) return faded ? 0x90d596 : 0x0c9618; // green
		if (cmp == g_CompLZ4.get()) return faded ? 0xd9d18c : 0xb19f00; // yellow
		//if (cmp == g_CompZlib.get()) return faded ? 0x8cd9cf : 0x00bfa7; // cyan
		//if (cmp == g_CompLibDeflate.get()) return 0x00786a; // cyan
		//if (cmp == g_CompBrotli) return faded ? 0xd19a94 : 0xde5546; // orange
		// purple
		//if (cmp == g_CompSelkie.get())   return 0xffb0ff;
		//if (cmp == g_CompMermaid.get())  return 0xdc74ff;
		if (cmp == g_CompKraken.get())   return faded ? 0xc4b6c9 : 0x8a4b9d; // dark purple regular: 0x8a4b9d lighter: 0xc4b6c9
		return 0;

	}
};

static std::vector<CompressorConfig> g_Compressors;

static void TestCompressors(size_t testFileCount, TestFile* testFiles)
{
#	if BUILD_WITH_OODLE
	void oodle_init();
	oodle_init();
#	endif


	g_Compressors.push_back({g_CompZstd.get(), &g_FilterSplit8DeltaOpt});
	g_Compressors.push_back({g_CompLZ4.get(), &g_FilterSplit8DeltaOpt});
#	if BUILD_WITH_OODLE
	g_Compressors.push_back({g_CompKraken.get(), &g_FilterSplit8DeltaOpt});
#	endif

	g_Compressors.push_back({g_CompZstd.get(), &g_FilterSplit8Delta});
	g_Compressors.push_back({g_CompLZ4.get(), &g_FilterSplit8Delta});
#	if BUILD_WITH_OODLE
	g_Compressors.push_back({g_CompKraken.get(), &g_FilterSplit8Delta});
#	endif

	g_Compressors.push_back({g_CompZstd.get(), &g_FilterSplit8AndDeltaDiff});
	g_Compressors.push_back({g_CompLZ4.get(), &g_FilterSplit8AndDeltaDiff});
#	if BUILD_WITH_OODLE
	g_Compressors.push_back({g_CompKraken.get(), &g_FilterSplit8AndDeltaDiff});
#	endif

	g_Compressors.push_back({g_CompZstd.get(), nullptr});
	g_Compressors.push_back({g_CompLZ4.get(), nullptr});
#	if BUILD_WITH_OODLE
	g_Compressors.push_back({g_CompKraken.get(), nullptr});
#	endif

	// For: https://aras-p.info/blog/2023/02/18/Float-Compression-6-Filtering-Optimization/
	/*
	g_Compressors.emplace_back(new GenericCompressor(kCompressionZstd, kFilterSplit8Delta));
	g_Compressors.emplace_back(new GenericCompressor(kCompressionLZ4, kFilterSplit8Delta));
#	if BUILD_WITH_OODLE
	g_Compressors.emplace_back(new GenericCompressor(kCompressionOoodleKraken, kFilterSplit8Delta));
#	endif
	g_Compressors.emplace_back(new GenericCompressor(kCompressionZstd, kFilterSplit8 | kFilterDeltaDiff));
	g_Compressors.emplace_back(new GenericCompressor(kCompressionLZ4, kFilterSplit8 | kFilterDeltaDiff));
#	if BUILD_WITH_OODLE
	g_Compressors.emplace_back(new GenericCompressor(kCompressionOoodleKraken, kFilterSplit8 | kFilterDeltaDiff));
#	endif
	g_Compressors.emplace_back(new GenericCompressor(kCompressionZstd));
	g_Compressors.emplace_back(new GenericCompressor(kCompressionLZ4));
#	if BUILD_WITH_OODLE
	g_Compressors.emplace_back(new GenericCompressor(kCompressionOoodleKraken));
#	endif
	*/

	// For: https://aras-p.info/blog/2023/02/03/Float-Compression-5-Science/
	/*
	g_Compressors.emplace_back(new ZfpCompressor());
	g_Compressors.emplace_back(new FpzipCompressor());
    g_Compressors.emplace_back(new SpdpCompressor());
#   if BUILD_WITH_NDZIP
	g_Compressors.emplace_back(new NdzipCompressor());
#   endif
	g_Compressors.emplace_back(new StreamVByteCompressor(kCompressionCount, false, false));
    //g_Compressors.emplace_back(new StreamVByteCompressor(kCompressionZstd, false, false)); // not good/interesting
    //g_Compressors.emplace_back(new StreamVByteCompressor(kCompressionCount, false, true)); // not good/interesting
    //g_Compressors.emplace_back(new StreamVByteCompressor(kCompressionZstd, false, true)); // not good/interesting
	//g_Compressors.emplace_back(new StreamVByteCompressor(kCompressionCount, true, false)); // not good/interesting
	//g_Compressors.emplace_back(new StreamVByteCompressor(kCompressionZstd, true, false)); // not good/interesting
	//g_Compressors.emplace_back(new StreamVByteCompressor(kCompressionCount, true, true)); // not good/interesting
	g_Compressors.emplace_back(new StreamVByteCompressor(kCompressionZstd, true, true));
	//g_Compressors.emplace_back(new StreamVByteCompressor(kCompressionLZ4, true, true)); // not too interesting
	// previous post
	//g_Compressors.emplace_back(new GenericCompressor(kCompressionOoodleKraken, kFilterSplit8 | kFilterDeltaDiff));
	g_Compressors.emplace_back(new GenericCompressor(kCompressionZstd, kFilterSplit8 | kFilterDeltaDiff));
	g_Compressors.emplace_back(new MeshOptCompressor(kCompressionZstd));
	g_Compressors.emplace_back(new GenericCompressor(kCompressionZstd));
	g_Compressors.emplace_back(new GenericCompressor(kCompressionLZ4));
	//g_Compressors.emplace_back(new GenericCompressor(kCompressionOoodleKraken));
	*/

	// For: https://aras-p.info/blog/2023/02/02/Float-Compression-4-Mesh-Optimizer/
	/*
	g_Compressors.emplace_back(new MeshOptCompressor(kCompressionZstd));
	g_Compressors.emplace_back(new MeshOptCompressor(kCompressionLZ4));
    #if BUILD_WITH_OODLE
	g_Compressors.emplace_back(new MeshOptCompressor(kCompressionOoodleKraken));
    #endif
	g_Compressors.emplace_back(new MeshOptCompressor(kCompressionCount));
	// none of filters help really
	//g_Compressors.emplace_back(new MeshOptCompressor(kCompressionZstd, kFilterSplit32 | kFilterDeltaDiff));
	//g_Compressors.emplace_back(new MeshOptCompressor(kCompressionZstd, kFilterSplit8 | kFilterDeltaDiff));
	//g_Compressors.emplace_back(new MeshOptCompressor(kCompressionZstd, kFilterSplit32));
	//g_Compressors.emplace_back(new MeshOptCompressor(kCompressionZstd, kFilterSplit8));
	g_Compressors.emplace_back(new GenericCompressor(kCompressionZstd, kFilterSplit8 | kFilterDeltaDiff));
	g_Compressors.emplace_back(new GenericCompressor(kCompressionLZ4, kFilterSplit8 | kFilterDeltaDiff));
    #if BUILD_WITH_OODLE
	g_Compressors.emplace_back(new GenericCompressor(kCompressionOoodleKraken, kFilterSplit8 | kFilterDeltaDiff));
    #endif
	g_Compressors.emplace_back(new GenericCompressor(kCompressionZstd));
	g_Compressors.emplace_back(new GenericCompressor(kCompressionLZ4));
    #if BUILD_WITH_OODLE
	g_Compressors.emplace_back(new GenericCompressor(kCompressionOoodleKraken));
    #endif
	*/


	//g_Compressors.emplace_back(new GenericCompressor(kCompressionZlib));
	//g_Compressors.emplace_back(new GenericCompressor(kCompressionBrotli));
	//g_Compressors.emplace_back(new GenericCompressor(kCompressionLibdeflate));
    //#if BUILD_WITH_OODLE
	//g_Compressors.emplace_back(new GenericCompressor(kCompressionOoodleSelkie));
	//g_Compressors.emplace_back(new GenericCompressor(kCompressionOoodleMermaid));
	//g_Compressors.emplace_back(new GenericCompressor(kCompressionOoodleKraken));
    //#endif

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
		int level = 0;
		bool cached = false;
		size_t size = 0;
		double cmpTime = 0;
		double decTime = 0;
	};
	typedef std::vector<Result> LevelResults;
	std::vector<LevelResults> results;
	for (auto& cmp : g_Compressors)
	{
		auto levels = cmp.cmp->GetLevels();
		LevelResults res(levels.size());
		for (size_t i = 0; i < levels.size(); ++i)
			res[i].level = levels[i];
		results.emplace_back(res);
	}

	std::string cmpName;
	for (int ir = 0; ir < kRuns; ++ir)
	{
		printf("Run %i/%i, %zi compressors on %zi files:\n", ir+1, kRuns, g_Compressors.size(), testFileCount);
		for (size_t ic = 0; ic < g_Compressors.size(); ++ic)
		{
			Compressor* cmp = g_Compressors[ic].cmp;
			FilterDesc* filter = g_Compressors[ic].filter;
			cmpName = g_Compressors[ic].GetName();
			LevelResults& levelRes = results[ic];
			printf("%s: %zi levels:\n", cmpName.c_str(), levelRes.size());
			for (Result& res : levelRes)
			{
				printf(".");
				size_t cachedSize;
				double cachedCmpTime, cachedDecTime;
				if (ResCacheGet(cmpName.c_str(), res.level, &cachedSize, &cachedCmpTime, &cachedDecTime))
				{
					res.size += cachedSize;
					res.cmpTime += cachedCmpTime;
					res.decTime += cachedDecTime;
					res.cached = true;
					continue;
				}
				for (int tfi = 0; tfi < testFileCount; ++tfi)
				{
					const TestFile& tf = testFiles[tfi];

					const float* srcData = tf.fileData.data();
					SysInfoFlushCaches();

					// filter if needed
					uint64_t t0 = stm_now();
					uint8_t* filterBuffer = nullptr;
					if (filter)
					{
						filterBuffer = new uint8_t[4 * tf.fileData.size()];
						filter->filterFunc((const uint8_t*)srcData, filterBuffer, tf.channels * sizeof(float), tf.width * tf.height);
						srcData = (const float*)filterBuffer;
					}

					// compress
					size_t compressedSize = 0;
					uint8_t* compressed = cmp->Compress(res.level, srcData, tf.width, tf.height, tf.channels, compressedSize);
					double tComp = stm_sec(stm_since(t0));

					// decompress:
					memset(decompressed.data(), 0, 4 * tf.fileData.size());
					if (filterBuffer)
						memset(filterBuffer, 0, 4 * tf.fileData.size());
					SysInfoFlushCaches();
					t0 = stm_now();
					cmp->Decompress(compressed, compressedSize, filter == nullptr ? decompressed.data() : (float*)filterBuffer, tf.width, tf.height, tf.channels);

					// unfilter data if needed
					if (filter != nullptr)
					{
						filter->unfilterFunc(filterBuffer, (uint8_t*)decompressed.data(), tf.channels * sizeof(float), tf.width * tf.height);
						delete[] filterBuffer;
					}
					double tDecomp = stm_sec(stm_since(t0));

					// stats
					res.size += compressedSize;
					res.cmpTime += tComp;
					res.decTime += tDecomp;

					// check validity
					if (memcmp(tf.fileData.data(), decompressed.data(), 4 * tf.fileData.size()) != 0)
					{
						printf("  ERROR, %s level %i did not decompress back to input\n", cmpName.c_str(), res.level);
						for (size_t i = 0; i < 4 * tf.fileData.size(); ++i)
						{
							float va = tf.fileData[i];
							float vb = decompressed[i];
							uint32_t ia = ((const uint32_t*)tf.fileData.data())[i];
							uint32_t ib = ((const uint32_t*)decompressed.data())[i];
							if (va != vb)
							{
								printf("    diff at #%zi: exp %f got %f (%08x %08x)\n", i, va, vb, ia, ib);
								break;
							}
						}
						exit(1);
					}
					delete[] compressed;
				}
			}
			printf("\n");
		}
		printf("\n");
	}

	// normalize results, cache the ones we ran, produce compressor versions
	int counterRan = 0, counterCached = 0;
	std::set<std::string> cmpVersions;
	char cmpVersion[256];
	for (size_t ic = 0; ic < g_Compressors.size(); ++ic)
	{
		Compressor* cmp = g_Compressors[ic].cmp;
		cmpName = g_Compressors[ic].GetName();
		cmp->PrintVersion(sizeof(cmpVersion), cmpVersion);
		cmpVersions.insert(cmpVersion);
		LevelResults& levelRes = results[ic];
		for (Result& res : levelRes)
		{
			res.size /= kRuns;
			res.cmpTime /= kRuns;
			res.decTime /= kRuns;
			if (!res.cached)
			{
				if (kWriteResultsCache)
				{
					ResCacheSet(cmpName.c_str(), res.level, res.size, res.cmpTime, res.decTime);
				}
			}
			else
			{
				++counterCached;
			}
			++counterRan;
		}
	}
	printf("  Ran %i cases (%i were fetched from previous runs)\n", counterRan, counterCached);


	double oneMB = 1024.0 * 1024.0;
	double oneGB = oneMB * 1024.0;
	double rawSize = (double)(totalFloats * 4);
	// print results to screen
	/*
	printf("Compressor             SizeMB CTimeS  DTimeS Ratio CGB/s DGB/s\n");
	printf("%-22s %7.3f\n", "Raw", rawSize / oneGB);
	for (size_t ic = 0; ic < g_Compressors.size(); ++ic)
	{
		cmpName = g_Compressors[ic].GetName();
		double csize = (double)(sizes[ic]);
		double ctime = cmpTimes[ic];
		double dtime = decompTimes[ic];
		double ratio = rawSize / csize;
		double cspeed = rawSize / ctime;
		double dspeed = rawSize / dtime;
		printf("%-22s %7.3f %6.3f %6.3f %6.3f %5.0f %5.0f\n", cmpName.c_str(), csize / oneGB, ctime, dtime, ratio, cspeed / oneGB, dspeed / oneGB);
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
	fprintf(fout, "<p>CPU: %s Compiler: %s</p>\n", SysInfoGetCpuName().c_str(), SysInfoGetCompilerName().c_str());
	fprintf(fout, "<p>");
	for (const auto& v : cmpVersions) fprintf(fout, "%s ", v.c_str());
	fprintf(fout, "</p>");
	fprintf(fout, "</center>");
	fprintf(fout, "<script type='text/javascript'>\n");
	fprintf(fout, "google.charts.load('current', {'packages':['corechart']});\n");
	fprintf(fout, "google.charts.setOnLoadCallback(drawChart);\n");
	fprintf(fout, "function drawChart() {\n");
	fprintf(fout, "var dataCmp = new google.visualization.DataTable();\n");
	fprintf(fout, "var dataDec = new google.visualization.DataTable();\n");
	fprintf(fout, "dataCmp.addColumn('number', 'Throughput');\n");
	fprintf(fout, "dataDec.addColumn('number', 'Throughput');\n");
	for (auto& cmp : g_Compressors)
	{
		cmpName = cmp.GetName();
		fprintf(fout, "dataCmp.addColumn('number', '%s'); dataCmp.addColumn({type:'string', role:'tooltip'}); dataCmp.addColumn({type:'string', role:'style'});\n", cmpName.c_str());
		fprintf(fout, "dataDec.addColumn('number', '%s'); dataDec.addColumn({type:'string', role:'tooltip'}); dataDec.addColumn({type:'string', role:'style'});\n", cmpName.c_str());
	}
	fprintf(fout, "dataCmp.addRows([\n");
	for (size_t ic = 0; ic < g_Compressors.size(); ++ic)
	{
		cmpName = g_Compressors[ic].GetName();
		const LevelResults& levelRes = results[ic];
		for (const Result& res : levelRes)
		{
			double csize = (double)res.size;
			double ctime = res.cmpTime;
			//double dtime = res.decTime;
			double ratio = rawSize / csize;
			double cspeed = rawSize / ctime;
			//double dspeed = rawSize / dtime;
			fprintf(fout, "  [%.3f", cspeed / oneGB);
			for (size_t j = 0; j < ic; ++j) fprintf(fout, ",null,null,null");
			fprintf(fout, ", %.3f,'%s", ratio, cmpName.c_str());
			if (levelRes.size() > 1)
				fprintf(fout, " %i", res.level);
			//if (strcmp(cmpName, "zstd-tst") == 0 && res.level == 1) // TEST TEST TEST
			//	printf("%s_%i ratio: %.3f\n", cmpName, res.level, ratio);
			fprintf(fout, "\\n%.3fx at %.3f GB/s\\n%.1FMB %.3fs','' ", ratio, cspeed / oneGB, csize / oneMB, ctime);
			for (size_t j = ic + 1; j < g_Compressors.size(); ++j) fprintf(fout, ",null,null,null");
			fprintf(fout, "]%s\n", (ic == g_Compressors.size() - 1) && (&res == &levelRes.back()) ? "" : ",");
		}
	}
	fprintf(fout, "]);\n");
	fprintf(fout, "dataDec.addRows([\n");
	for (size_t ic = 0; ic < g_Compressors.size(); ++ic)
	{
		cmpName = g_Compressors[ic].GetName();
		const LevelResults& levelRes = results[ic];
		for (const Result& res : levelRes)
		{
			double csize = (double)res.size;
			//double ctime = res.cmpTime;
			double dtime = res.decTime;
			double ratio = rawSize / csize;
			//double cspeed = rawSize / ctime;
			double dspeed = rawSize / dtime;
			fprintf(fout, "  [%.3f", dspeed / oneGB);
			for (size_t j = 0; j < ic; ++j) fprintf(fout, ",null,null,null");
			fprintf(fout, ", %.3f,'%s", ratio, cmpName.c_str());
			if (levelRes.size() > 1)
				fprintf(fout, " %i", res.level);
			fprintf(fout, "\\n%.3fx at %.3f GB/s\\n%.1FMB %.3fs','' ", ratio, dspeed / oneGB, csize / oneMB, dtime);
			for (size_t j = ic + 1; j < g_Compressors.size(); ++j) fprintf(fout, ",null,null,null");
			fprintf(fout, "]%s\n", (ic == g_Compressors.size() - 1) && (&res == &levelRes.back()) ? "" : ",");
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
		fprintf(fout, "  %zi: {pointShape: %s},\n", ic, g_Compressors[ic].GetShapeString());
	}
	fprintf(fout, "  %zi: {},\n", g_Compressors.size());
	fprintf(fout, "},\n");
	fprintf(fout, "colors: [");
	for (size_t ic = 0; ic < g_Compressors.size(); ++ic)
	{
		uint32_t col = g_Compressors[ic].GetColor();
		fprintf(fout, "'%02x%02x%02x'%s", (col >> 16)&0xFF, (col >> 8)&0xFF, col&0xFF, ic== g_Compressors.size()-1?"":",");
	}
	fprintf(fout, "],\n");
	fprintf(fout, "hAxis: {title: 'Compression GB/s', logScale: true, viewWindow: {min:0.03, max:2.0}},\n");
	fprintf(fout, "vAxis: {title: 'Ratio', viewWindow: {min:1.0, max:4.5}},\n");
	fprintf(fout, "chartArea: {left:60, right:10, top:50, bottom:50},\n");
	fprintf(fout, "legend: {position: 'top'},\n");
	fprintf(fout, "lineWidth: 1\n");
	fprintf(fout, "};\n");
	fprintf(fout, "var chartCmp = new google.visualization.ScatterChart(document.getElementById('chart_cmp'));\n");
	fprintf(fout, "chartCmp.draw(dataCmp, options);\n");
	fprintf(fout, "options.title = titleDec;\n");
	fprintf(fout, "options.hAxis.title = 'Decompression GB/s';\n");
	fprintf(fout, "options.hAxis.viewWindow.min = 0.5;\n");
	fprintf(fout, "options.hAxis.viewWindow.max = 8.0;\n");
	fprintf(fout, "var chartDec = new google.visualization.ScatterChart(document.getElementById('chart_dec'));\n");
	fprintf(fout, "chartDec.draw(dataDec, options);\n");
	fprintf(fout, "}\n");
	fprintf(fout, "</script>\n");
	fclose(fout);

	// cleanup
	g_Compressors.clear();
}



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


static void TestFiltersOnSyntheticData()
{
	printf("Testing filters on synthetic data:\n");
	// generate data and allocate buffers
	const size_t kFloatCount = 8 * 1024 * 1024;
	float* srcData = new float[kFloatCount];
	for (size_t i = 0; i < kFloatCount; ++i)
		srcData[i] = -1000.0f + i * 0.01f - cosf(i * i * 0.001f);
	float* srcDataOrig = new float[kFloatCount];
	memcpy(srcDataOrig, srcData, kFloatCount * 4);
	float* encData = new float[kFloatCount];
	float* gotData = new float[kFloatCount];
	size_t cmpBound = compress_calc_bound(kFloatCount * 4, kCompressionZstd);
	uint8_t* cmpBuffer = new uint8_t[cmpBound];

	bool wasCached[kFilterCount] = {};
	size_t cmpSizeFilter[kFilterCount] = {};
	uint64_t timeFilter[kFilterCount] = {};
	uint64_t timeUnfilter[kFilterCount] = {};
	double timeMinFilter[kFilterCount] = {};
	double timeMinUnfilter[kFilterCount] = {};

	// check which ones we already have cached
	for (int fi = 0; fi < kFilterCount; ++fi)
	{
		size_t cachedSize;
		double cachedCmpTime, cachedDecTime;
		char namebuf[1024];
		snprintf(namebuf, sizeof(namebuf), "filter_syn_%s", g_Filters[fi].name);
		if (ResCacheGet(namebuf, 0, &cachedSize, &cachedCmpTime, &cachedDecTime))
		{
			wasCached[fi] = true;
			cmpSizeFilter[fi] = cachedSize;
			timeMinFilter[fi] = cachedCmpTime;
			timeMinUnfilter[fi] = cachedDecTime;
		}
	}

	size_t totalSize = 0;

	const int kStridesToTest[] = { 4, 8, 12, 12, 16, 16, 16, 16, 16, 20, 32, 32, 44, 48, 64 };
	const size_t kElemsToTest[] = { 1, 5, 16, 33, 131, 1024, 4229, 16*1024, 57041, 256*1024, 0 };

	uint64_t t0, t1;

	// several runs
	for (int run = 0; run < kRuns; ++run)
	{
		printf(" run %i: strides ", run);
		memset(timeFilter, 0, sizeof(timeFilter));
		memset(timeUnfilter, 0, sizeof(timeUnfilter));

		// varying strides
		for (int stride : kStridesToTest)
		{
			printf("%i ", stride); fflush(stdout);
			size_t maxElemsThisStride = kFloatCount * 4 / stride;

			// varying data sizes
			for (size_t elemCount : kElemsToTest)
			{
				if (elemCount > maxElemsThisStride) continue;
				if (elemCount == 0) elemCount = maxElemsThisStride;
				if (run == 0)
					totalSize += stride * elemCount;

				// test the filters
				for (int fi = 0; fi < kFilterCount; ++fi)
				{
					if (wasCached[fi])
						continue; // already have a cached result

					memset(encData, 0, stride * elemCount);
					memset(gotData, 0, stride * elemCount);

					// compression filter
					SysInfoFlushCaches();
					t0 = stm_now();
					g_Filters[fi].filterFunc((const uint8_t*)srcData, (uint8_t*)encData, stride, elemCount);
					t1 = stm_now();
					timeFilter[fi] += t1 - t0;

					// test what is zstd1 size with this filter
					if (elemCount == maxElemsThisStride && stride == 16 && cmpSizeFilter[fi] == 0)
					{
						cmpSizeFilter[fi] = compress_data(encData, stride * elemCount, cmpBuffer, cmpBound, kCompressionZstd, 1);
					}

					// decompression filter
					SysInfoFlushCaches();
					t0 = stm_now();
					g_Filters[fi].unfilterFunc((const uint8_t*)encData, (uint8_t*)gotData, stride, elemCount);
					t1 = stm_now();
					timeUnfilter[fi] += t1 - t0;

					if (memcmp(srcData, gotData, stride * elemCount) != 0)
					{
						printf("ERROR filter '%s' did not decode properly for stride=%i elemCount=%zi\n", g_Filters[fi].name, stride, elemCount);
						exit(1);
					}
					if (memcmp(srcData, srcDataOrig, stride * elemCount) != 0)
					{
						printf("ERROR filter '%s' modified source data! stride=%i elemCount=%zi\n", g_Filters[fi].name, stride, elemCount);
						exit(1);
					}
				}
			}
		}
		printf("\n");

		// take result of the fastest run
		for (int fi = 0; fi < kFilterCount; ++fi)
		{
			if (wasCached[fi])
				continue;
			double timeF = stm_ms(timeFilter[fi]);
			double timeUf = stm_ms(timeUnfilter[fi]);
			if (timeMinFilter[fi] == 0) timeMinFilter[fi] = timeF;
			if (timeMinUnfilter[fi] == 0) timeMinUnfilter[fi] = timeUf;
			timeMinFilter[fi] = std::min(timeMinFilter[fi], timeF);
			timeMinUnfilter[fi] = std::min(timeMinUnfilter[fi], timeUf);
		}
	}
	delete[] srcData;
	delete[] srcDataOrig;
	delete[] encData;
	delete[] gotData;
	delete[] cmpBuffer;

	// write new results into cache
	for (int fi = 0; fi < kFilterCount; ++fi)
	{
		if (kWriteResultsCache && !wasCached[fi])
		{
			char namebuf[1024];
			snprintf(namebuf, sizeof(namebuf), "filter_syn_%s", g_Filters[fi].name);
			ResCacheSet(namebuf, 0, cmpSizeFilter[fi], timeMinFilter[fi], timeMinUnfilter[fi]);
		}
	}

	printf("Filter test ok! Total size %.1fMB\n", totalSize/1024.0/1024.0);
	printf("%-15s %6s  %6s  %6s\n", "Filter", "Cmp", "Dec", "Ratio");
	for (int fi = 0; fi < kFilterCount; ++fi)
	{
		printf("%-15s %6.1f  %6.1f  %6.3f\n", g_Filters[fi].name, timeMinFilter[fi], timeMinUnfilter[fi], kFloatCount * 4.0 / cmpSizeFilter[fi]);
	}
}

static void TestFiltersOnFiles(size_t testFileCount, TestFile* testFiles)
{
	printf("Testing filters on data files: ");

	std::vector<size_t> startIndex(testFileCount);
	size_t totalFloats = 0;
	for (size_t i = 0; i < testFileCount; ++i)
	{
		startIndex[i] = totalFloats;
		totalFloats += testFiles[i].width * testFiles[i].height * testFiles[i].channels;
	}
	std::vector<float> filtered(totalFloats);
	std::vector<float> unfiltered(totalFloats);
	size_t cmpBound = compress_calc_bound(totalFloats * 4, kCompressionZstd);
	std::vector<uint8_t> cmpBuffer(cmpBound);

	bool wasCached[kFilterCount] = {};
	size_t cmpSizeFilter[kFilterCount] = {};
	double timeMinFilter[kFilterCount] = {};
	double timeMinUnfilter[kFilterCount] = {};

	// check which ones we already have cached
	for (int fi = 0; fi < kFilterCount; ++fi)
	{
		size_t cachedSize;
		double cachedCmpTime, cachedDecTime;
		char namebuf[1024];
		snprintf(namebuf, sizeof(namebuf), "filter_%s", g_Filters[fi].name);
		if (ResCacheGet(namebuf, 0, &cachedSize, &cachedCmpTime, &cachedDecTime))
		{
			wasCached[fi] = true;
			cmpSizeFilter[fi] = cachedSize;
			timeMinFilter[fi] = cachedCmpTime;
			timeMinUnfilter[fi] = cachedDecTime;
		}
	}

	uint64_t t0;

	for (int i = 0; i < kRuns; ++i)
	{
		printf("."); fflush(stdout);

		// test the filters
		for (int fi = 0; fi < kFilterCount; ++fi)
		{
			if (wasCached[fi])
				continue; // already have a cached result

			// go over the files
			uint64_t timeFilter = 0, timeUnfilter = 0;
			size_t cmpSizeSum = 0;
			for (size_t tfi = 0; tfi < testFileCount; ++tfi)
			{
				const TestFile& tf = testFiles[tfi];

				// compression filter
				SysInfoFlushCaches();
				t0 = stm_now();
				g_Filters[fi].filterFunc((const uint8_t*)tf.fileData.data(), (uint8_t*)&filtered[startIndex[tfi]], tf.channels * 4, tf.width * tf.height);
				timeFilter += stm_since(t0);

				// test what is zstd1 size with this filter
				if (cmpSizeFilter[fi] == 0)
					cmpSizeSum += compress_data(&filtered[startIndex[tfi]], tf.channels * 4 * tf.width * tf.height, cmpBuffer.data(), cmpBound, kCompressionZstd, 1);

				// decompression filter
				SysInfoFlushCaches();
				t0 = stm_now();
				g_Filters[fi].unfilterFunc((const uint8_t*)&filtered[startIndex[tfi]], (uint8_t*)&unfiltered[startIndex[tfi]], tf.channels * 4, tf.width * tf.height);
				timeUnfilter += stm_since(t0);

				if (memcmp(tf.fileData.data(), &unfiltered[startIndex[tfi]], tf.fileData.size() * 4) != 0)
				{
					printf("ERROR filter '%s' did not decode properly for %s %ix%i ch=%i\n", g_Filters[fi].name, tf.path, tf.width, tf.height, tf.channels);
					exit(1);
				}
			}

			if (cmpSizeFilter[fi] == 0)
				cmpSizeFilter[fi] = cmpSizeSum;

			// take result of the fastest run
			double timeF = stm_ms(timeFilter);
			double timeUf = stm_ms(timeUnfilter);
			if (timeMinFilter[fi] == 0) timeMinFilter[fi] = timeF;
			if (timeMinUnfilter[fi] == 0) timeMinUnfilter[fi] = timeUf;
			timeMinFilter[fi] = std::min(timeMinFilter[fi], timeF);
			timeMinUnfilter[fi] = std::min(timeMinUnfilter[fi], timeUf);
		}
	}
	printf("\n");

	// write new results into cache
	for (int fi = 0; fi < kFilterCount; ++fi)
	{
		if (kWriteResultsCache && !wasCached[fi])
		{
			char namebuf[1024];
			snprintf(namebuf, sizeof(namebuf), "filter_%s", g_Filters[fi].name);
			ResCacheSet(namebuf, 0, cmpSizeFilter[fi], timeMinFilter[fi], timeMinUnfilter[fi]);
		}
	}

	printf("Data filter times on %.1fMB\n", totalFloats * 4 / 1024.0 / 1024.0);
	printf("%-15s %6s  %6s  %6s\n", "Filter", "Cmp", "Dec", "Ratio");
	for (int fi = 0; fi < kFilterCount; ++fi)
	{
		printf("%-15s %6.1f  %6.1f  %6.3f\n", g_Filters[fi].name, timeMinFilter[fi], timeMinUnfilter[fi], totalFloats * 4.0 / cmpSizeFilter[fi]);
	}
}


int main()
{
	stm_setup();
	printf("CPU: '%s' Compiler: '%s'\n", SysInfoGetCpuName().c_str(), SysInfoGetCompilerName().c_str());

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
	ResCacheInit();

	//TestFiltersOnSyntheticData();
	//TestFiltersOnFiles(std::size(testFiles), testFiles);

	TestCompressors(std::size(testFiles), testFiles);

	ResCacheClose();
	return 0;
}

// Lossy:
// SZ https://github.com/szcompressor/SZ
// DCTZ https://github.com/swson/DCTZ
// digitroundingZ https://github.com/disheng222/digitroundingZ
// BitGroomingZ https://github.com/disheng222/BitGroomingZ

