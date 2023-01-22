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

# meshoptimizer
FetchContent_Declare(
	meshopt
	URL https://github.com/zeux/meshoptimizer/archive/refs/tags/v0.18.zip # latest at time of writing
)
FetchContent_MakeAvailable(meshopt)

project ("float_compr_tester")

add_executable (float_compr_tester
	src/main.cpp
	src/compression_helpers.cpp
	src/compressors.cpp
)
set_property(TARGET float_compr_tester PROPERTY CXX_STANDARD 20)

target_include_directories(float_compr_tester PRIVATE 
	${zstd_SOURCE_DIR}/lib
	${lz4_SOURCE_DIR}/lib
	${meshopt_SOURCE_DIR}
)

target_link_libraries(float_compr_tester PRIVATE
	libzstd_static
    lz4_static
	meshoptimizer
)

target_compile_definitions(float_compr_tester PRIVATE
	_CRT_SECURE_NO_DEPRECATE
)
