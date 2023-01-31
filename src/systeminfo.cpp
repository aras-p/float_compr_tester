#include "systeminfo.h"

#ifdef _WIN32
#include <intrin.h>
#endif

static std::string TrimRight(std::string s)
{
	while (!s.empty())
	{
		if (s.back() <= ' ')
			s.pop_back();
		else
			break;
	}
	return s;
}

std::string SysInfoGetCpuName()
{
#	if defined(_WIN32)
	int cpuInfo[4] = { -1 };
	char brandString[49] = {};

	__cpuid(cpuInfo, 0x80000002);
	memcpy(brandString, cpuInfo, 16);
	__cpuid(cpuInfo, 0x80000003);
	memcpy(brandString + 16, cpuInfo, 16);
	__cpuid(cpuInfo, 0x80000004);
	memcpy(brandString + 32, cpuInfo, 16);
	return TrimRight(brandString);
#	else
#	error Unknown platform
#	endif
}


std::string SysInfoGetCompilerName()
{
#if defined __clang__
	// Clang
	return std::string("Clang ") + TrimRight(__clang_version__);
#elif defined _MSC_VER
	// MSVC
#	if _MSC_VER >= 1937
	return "MSVC 2022 17.7+";
#	elif _MSC_VER == 1936
	return "MSVC 2022 17.6";
#	elif _MSC_VER == 1935
	return "MSVC 2022 17.5";
#	elif _MSC_VER == 1934
	return "MSVC 2022 17.4";
#	elif _MSC_VER == 1933
	return "MSVC 2022 17.3";
#	elif _MSC_VER == 1932
	return "MSVC 2022 17.2";
#	elif _MSC_VER == 1931
	return "MSVC 2022 17.1";
#	elif _MSC_VER == 1930
	return "MSVC 2022 17.0";
#	elif _MSC_VER == 1929
	return "MSVC 2019 16.10+";
#	elif _MSC_VER == 1928
	return "MSVC 2019 16.8/9";
#	elif _MSC_VER == 1927
	return "MSVC 2019 16.7";
#	elif _MSC_VER == 1926
	return "MSVC 2019 16.6";
#	elif _MSC_VER == 1925
	return "MSVC 2019 16.5";
#	elif _MSC_VER == 1924
	return "MSVC 2019 16.4";
#	elif _MSC_VER == 1923
	return "MSVC 2019 16.3";
#	elif _MSC_VER == 1922
	return "MSVC 2019 16.2";
#	elif _MSC_VER == 1921
	return "MSVC 2019 16.1";
#	elif _MSC_VER == 1920
	return "MSVC 2019 16.0";
#	elif _MSC_VER >= 1910
	return "MSVC 2017";
#	elif _MSC_VER >= 1900
	return "MSVC 2015";
#	else
	return "MSVC Unknown";
#	endif
#else
#	error Unknown compiler
#endif
}
