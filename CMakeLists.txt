cmake_minimum_required (VERSION 3.21)
if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW) # enable MSVC_RUNTIME_LIBRARY
endif()
if(POLICY CMP0077)
    cmake_policy(SET CMP0077 NEW) # enable override of options from parent CMakeLists.txt
endif()
if(POLICY CMP0135)
	cmake_policy(SET CMP0135 NEW) # avoid warning about DOWNLOAD_EXTRACT_TIMESTAMP in CMake 3.24+
endif()
if(POLICY CMP0092)
    cmake_policy(SET CMP0092 NEW) # enables /W4 override for MSVC
endif()

include(FetchContent)

# zstd
FetchContent_Declare(
	zstd
	URL https://github.com/facebook/zstd/archive/refs/tags/v1.5.2.zip # latest at time of writing
	SOURCE_SUBDIR build/cmake
)
set(ZSTD_LEGACY_SUPPORT OFF)
set(ZSTD_MULTITHREAD_SUPPORT OFF)
set(ZSTD_BUILD_TESTS OFF)
set(ZSTD_BUILD_PROGRAMS OFF)
set(ZSTD_BUILD_CONTRIB OFF)
set(ZSTD_BUILD_STATIC ON)
set(ZSTD_BUILD_SHARED OFF)
FetchContent_MakeAvailable(zstd)

# lz4
FetchContent_Declare(
	lz4
	URL https://github.com/lz4/lz4/archive/refs/tags/v1.9.3.zip # latest at time of writing
	SOURCE_SUBDIR build/cmake
)
set(LZ4_BUILD_LEGACY_LZ4C OFF)
set(LZ4_BUILD_CLI OFF)
FetchContent_MakeAvailable(lz4)

# brotli
FetchContent_Declare(
	brotli
	URL https://github.com/google/brotli/archive/refs/tags/v1.0.9.zip # latest (2020 Aug) at time of writing
)
FetchContent_MakeAvailable(brotli)

# libdeflate
FetchContent_Declare(
	libdeflate
	URL https://github.com/ebiggers/libdeflate/archive/refs/tags/v1.17.zip # latest (2023 Jan) at time of writing
)
set(LIBDEFLATE_BUILD_SHARED_LIB OFF)
set(LIBDEFLATE_ZLIB_SUPPORT OFF)
set(LIBDEFLATE_GZIP_SUPPORT OFF)
set(LIBDEFLATE_BUILD_GZIP OFF)
set(LIBDEFLATE_BUILD_SHARED_LIB OFF)
set(LIBDEFLATE_BUILD_SHARED_LIB OFF)
set(LIBDEFLATE_BUILD_SHARED_LIB OFF)
FetchContent_MakeAvailable(libdeflate)

# zlib
# Note: do not do FetchContent_MakeAvailable on it, since it builds "example"
# target which clashes with streamvbyte "example" target. Instead setup manually
FetchContent_Declare(
	zlib
	URL https://github.com/madler/zlib/archive/refs/tags/v1.2.13.zip # latest (2022 Oct) at time of writing
	CONFIGURE_COMMAND ""
	BUILD_COMMAND ""
)
FetchContent_GetProperties(zlib)
if(NOT zlib_POPULATED)
	FetchContent_Populate(zlib)
endif()
project(zlib LANGUAGES C)
add_library(zlib STATIC)
if(MSVC)
    target_compile_options(zlib PRIVATE /MP)
endif()
target_sources(zlib PRIVATE
	${zlib_SOURCE_DIR}/adler32.c
	${zlib_SOURCE_DIR}/compress.c
	${zlib_SOURCE_DIR}/crc32.c
	${zlib_SOURCE_DIR}/deflate.c
	${zlib_SOURCE_DIR}/infback.c
	${zlib_SOURCE_DIR}/inffast.c
	${zlib_SOURCE_DIR}/inflate.c
	${zlib_SOURCE_DIR}/inftrees.c
	${zlib_SOURCE_DIR}/trees.c
	${zlib_SOURCE_DIR}/uncompr.c
	${zlib_SOURCE_DIR}/zutil.c
	# Note: not including gz*.c sources, we don't need them
)
target_include_directories(zlib PRIVATE	${zlib_BINARY_DIR})

# meshoptimizer
FetchContent_Declare(
	meshopt
	URL https://github.com/zeux/meshoptimizer/archive/refs/tags/v0.18.zip # latest at time of writing
)
FetchContent_MakeAvailable(meshopt)

# fpzip
FetchContent_Declare(
	fpzip
	URL https://github.com/LLNL/fpzip/archive/refs/tags/1.3.0.zip # latest at time of writing (2019 Dec)
)
set(BUILD_SHARED_LIBS OFF)
set(BUILD_TESTING OFF)
set(BUILD_UTILITIES OFF)
FetchContent_MakeAvailable(fpzip)

# zfp
FetchContent_Declare(
	zfp
	URL https://github.com/LLNL/zfp/archive/refs/tags/1.0.0.zip # latest at time of writing (2022 Aug)
)
set(BUILD_SHARED_LIBS OFF)
set(BUILD_TESTING OFF)
set(BUILD_UTILITIES OFF)
FetchContent_MakeAvailable(zfp)

# ndzip
if (NOT APPLE) # ndzip does not build on Mac
    set(BUILD_WITH_NDZIP ON)
    FetchContent_Declare(
        ndzip
        URL https://github.com/aras-p/ndzip/archive/refs/heads/build-just-lib.zip # my fork with Windows build fixes
    )
    set(NDZIP_BUILD_COMPRESS OFF)
    set(NDZIP_WITH_MT OFF)
    set(NDZIP_WITH_HIPSYCL OFF)
    set(NDZIP_WITH_CUDA OFF)
    set(NDZIP_WITH_3RDPARTY_BENCHMARKS OFF)
    FetchContent_MakeAvailable(ndzip)
endif ()

# streamvbyte
FetchContent_Declare(
	streamvbyte
	URL https://github.com/lemire/streamvbyte/archive/refs/tags/v0.5.2.zip # latest (2023 Feb) at time of writing
)
set(STREAMVBYTE_ENABLE_TESTS OFF)
set(STREAMVBYTE_ENABLE_EXAMPLES OFF)
FetchContent_MakeAvailable(streamvbyte)


project ("float_compr_tester")

add_executable (float_compr_tester
	src/main.cpp
	src/compression_helpers.cpp
	src/compression_helpers.h
	src/compressors.cpp
	src/compressors.h
	src/systeminfo.cpp
	src/systeminfo.h
	src/resultcache.cpp
	src/resultcache.h
	libs/bitshuffle/src/bitshuffle_core.c
	libs/bitshuffle/src/bitshuffle_core.h
	libs/bitshuffle/src/iochain.c
    libs/spdp/spdp_11.c
    libs/spdp/spdp_11.h
	CMakeLists.txt
	CMakePresets.json
)
set_property(TARGET float_compr_tester PROPERTY CXX_STANDARD 20)

target_include_directories(float_compr_tester PRIVATE 
	${zstd_SOURCE_DIR}/lib
	${lz4_SOURCE_DIR}/lib
	${meshopt_SOURCE_DIR}
	${zlib_SOURCE_DIR} ${zlib_BINARY_DIR}
	${brotli_SOURCE_DIR}/c/include
	${libdeflate_SOURCE_DIR}
    ${streamvbyte_SOURCE_DIR/include}
)

target_link_libraries(float_compr_tester PRIVATE
	libzstd_static
    lz4_static
	zlib
	brotlienc-static brotlidec-static
	libdeflate_static
	meshoptimizer
	fpzip
	zfp
	streamvbyte_static
)
if (BUILD_WITH_NDZIP)
    target_link_libraries(float_compr_tester PRIVATE ndzip)
    target_compile_definitions(float_compr_tester PRIVATE BUILD_WITH_NDZIP=1)
endif ()

target_compile_definitions(float_compr_tester PRIVATE
	_CRT_SECURE_NO_DEPRECATE
	_CRT_NONSTDC_NO_WARNINGS
	NOMINMAX
)

if (EXISTS "${CMAKE_SOURCE_DIR}/src/oodle_wrapper.cpp")
	message (STATUS "Including Oodle compressors")
	target_compile_definitions(float_compr_tester PRIVATE BUILD_WITH_OODLE=1)
	target_sources(float_compr_tester PRIVATE src/oodle_wrapper.cpp)
	if (WIN32)
		target_link_libraries(float_compr_tester PRIVATE ${CMAKE_SOURCE_DIR}/libs/oodle/oo2core_win64.lib)
	endif ()
	if (APPLE)
		target_link_libraries(float_compr_tester PRIVATE ${CMAKE_SOURCE_DIR}/libs/oodle/liboo2coremac64.a)
	endif ()
endif ()
