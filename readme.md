# Testing compression approaches for floating point data

:warning: The code is a mess right now, don't look at it!

A series of experiments in compressing semi-structured floating point data. Blog post series about it:

* [Part 0: Intro](https://aras-p.info/blog/2023/01/29/Float-Compression-0-Intro/)
* [Part 1: Generic Compression Libraries](https://aras-p.info/blog/2023/01/29/Float-Compression-1-Generic/) (zlib, lz4, zstd, brotli)
* [Part 2: Generic Compression Libraries](https://aras-p.info/blog/2023/01/31/Float-Compression-2-Oodleflate/) (libdeflate, oodle)
* [Part 3: Data Filtering](https://aras-p.info/blog/2023/02/01/Float-Compression-3-Filters/) (simple data filtering to improve compression ratio)
* [Part 4: Mesh Optimizer](https://aras-p.info/blog/2023/02/02/Float-Compression-4-Mesh-Optimizer/) (mis-using mesh compression library on our data set)
* [Part 5: Science!](https://aras-p.info/blog/2023/02/03/Float-Compression-5-Science/) (zfp, fpzip, SPDP, ndzip, streamvbyte)
* [Part 6: Optimize Filtering](https://aras-p.info/blog/2023/02/18/Float-Compression-6-Filtering-Optimization/) (optimizations for part 3 data filters)

Code here uses 3rd party libraries:
* [zstd](https://github.com/facebook/zstd), v1.5.2, BSD or GPLv2 license.
* [lz4](https://github.com/lz4/lz4), v1.9.3, BSD license.
* [zlib](https://github.com/madler/zlib), 1.2.13, zlib license.
* [brotli](https://github.com/google/brotli), v1.0.9, MIT license.
* [libdeflate](https://github.com/ebiggers/libdeflate), v1.17, MIT license.
* [bitshuffle](https://github.com/kiyo-masui/bitshuffle), 0.5.1, MIT license.
* [meshoptimizer](https://github.com/zeux/meshoptimizer), v0.18, MIT license.
* [spdp_11.c](https://userweb.cs.txstate.edu/~burtscher/research/SPDPcompressor/), BSD license.
* [zfp](https://github.com/LLNL/zfp), 1.0.0, BSD license.
* [fpzip](https://github.com/LLNL/fpzip), 1.3.0, BSD license.
* [ndzip](https://github.com/celerity/ndzip), 2022.06, MIT license.
* [streamvbyte](https://github.com/lemire/streamvbyte), 0.5.2, Apache-2.0 license.
* [sokol_time](https://github.com/floooh/sokol), zlib license.
* [ini.h](https://github.com/mattiasgustavsson/libs/blob/main/ini.h), 2022 Sep (aef7d92), MIT or Public Domain.

Oodle SDK is not public, so that one is not compiled in unless you put:
* Oodle wrapper functions implementation file into `src/oodle_wrapper.cpp`,
  with implementation of `oodle_init`, `oodle_compress_calc_bound`,
  `oodle_compress_data`, `oodle_decompress_data`, `oodle_compressor_get_version`.
* Oodle header files and core static library under `libs/oodle` folder.
