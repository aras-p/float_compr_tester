#include "resultcache.h"

#include <string>
#include <stdio.h>
#include "systeminfo.h"


#define INI_IMPLEMENTATION
#include "../libs/ini.h"

static const char* kIniFilePath = "../../../data/results/cache.ini";
static ini_t* s_Cache = nullptr;
static bool s_CacheModified = false;
static std::string s_CacheSectionName;
static int s_CacheSectionIndex;

void ResCacheInit()
{
	s_CacheModified = false;
	FILE* fp = fopen(kIniFilePath, "rt");
	if (fp)
	{
		fseek(fp, 0, SEEK_END);
		size_t fsize = ftell(fp);
		fseek(fp, 0, SEEK_SET);
		char* fdata = new char[fsize + 1];
		memset(fdata, 0, fsize + 1);
		fread(fdata, 1, fsize, fp);
		fclose(fp);

		s_Cache = ini_load(fdata, nullptr);
		delete[] fdata;
	}
	if (!s_Cache)
	{
		s_Cache = ini_create(nullptr);
	}

	s_CacheSectionName = SysInfoGetCpuName() + " " + SysInfoGetCompilerName();
	s_CacheSectionIndex = ini_find_section(s_Cache, s_CacheSectionName.c_str(), (int)s_CacheSectionName.size());
	if (s_CacheSectionIndex == INI_NOT_FOUND)
		s_CacheSectionIndex = ini_section_add(s_Cache, s_CacheSectionName.c_str(), (int)s_CacheSectionName.size());
}

void ResCacheClose()
{
	if (!s_Cache)
		return;
	if (s_CacheModified)
	{
		int fsize = ini_save(s_Cache, nullptr, 0);
		char* fdata = new char[fsize];
		fsize = ini_save(s_Cache, fdata, fsize);
		FILE* fp = fopen(kIniFilePath, "wb");
		if (fp != nullptr)
		{
			fwrite(fdata, 1, fsize - 1, fp);
			fclose(fp);
		}
		delete[] fdata;
	}
	ini_destroy(s_Cache);
	s_Cache = nullptr;
	s_CacheModified = false;
}

bool ResCacheGet(const char* name, int level, size_t* outSize, double* outCmpTime, double* outDecTime)
{
#ifdef _DEBUG
	return false;
#endif
	if (!s_Cache) return false;

	char propName[1024];
	snprintf(propName, sizeof(propName), "%s_%i", name, level);
	int propIndex = ini_find_property(s_Cache, s_CacheSectionIndex, propName, 0);
	if (propIndex == INI_NOT_FOUND)
		return false;
	const char* propValue = ini_property_value(s_Cache, s_CacheSectionIndex, propIndex);
	size_t size;
	double cmpTime, decTime;
	int parsed = sscanf(propValue, "%zi %lf %lf", &size, &cmpTime, &decTime);
	if (parsed != 3)
		return false;
	*outSize = size;
	*outCmpTime = cmpTime;
	*outDecTime = decTime;
	return true;
}

void ResCacheSet(const char* name, int level, size_t size, double cmpTime, double decTime)
{
#ifdef _DEBUG
	return;
#endif

	s_CacheModified = true;
	char propValue[1024];
	snprintf(propValue, sizeof(propValue), "%zi %.4lf %.4lf", size, cmpTime, decTime);

	char propName[1024];
	snprintf(propName, sizeof(propName), "%s_%i", name, level);
	int propIndex = ini_find_property(s_Cache, s_CacheSectionIndex, propName, 0);
	if (propIndex == INI_NOT_FOUND)
		ini_property_add(s_Cache, s_CacheSectionIndex, propName, 0, propValue, 0);
	else
		ini_property_value_set(s_Cache, s_CacheSectionIndex, propIndex, propValue, 0);
}
