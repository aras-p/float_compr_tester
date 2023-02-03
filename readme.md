# Testing compression approaches for floating point data

:warning: The code is a mess right now, don't look at it!

A series of experiments in compressing semi-structured floating point data. Blog post series about it:

* [Part 0: Intro](https://aras-p.info/blog/2023/01/29/Float-Compression-0-Intro/)
* [Part 1: Generic Compression Libraries](https://aras-p.info/blog/2023/01/29/Float-Compression-1-Generic/) (zlib, lz4, zstd, brotli)
* [Part 2: Generic Compression Libraries](https://aras-p.info/blog/2023/01/29/Float-Compression-1-Generic/) (libdeflate, oodle)
* [Part 3: Data Filtering](https://aras-p.info/blog/2023/02/01/Float-Compression-3-Filters/) (simple data filtering to improve compression ratio)
* [Part 4: Mesh Optimizer](https://aras-p.info/blog/2023/02/02/Float-Compression-4-Mesh-Optimizer/) (mis-using mesh compression library on our data set)

Code here uses 3rd party libraries:
* [zstd](https://github.com/facebook/zstd), v1.5.2, BSD or GPLv2 license.
* [lz4](https://github.com/lz4/lz4), v1.9.3, BSD license.
* [zlib](https://github.com/madler/zlib), 1.2.13, zlib license.
* [brotli](https://github.com/google/brotli), v1.0.9, MIT license.
* [libdeflate](https://github.com/ebiggers/libdeflate), v1.17, MIT license.
* [bitshuffle](https://github.com/kiyo-masui/bitshuffle), 0.5.1, MIT license.
* [meshoptimizer](https://github.com/zeux/meshoptimizer), v0.18, MIT license.
* [spdp_11.c](https://userweb.cs.txstate.edu/~burtscher/research/SPDPcompressor/), BSD license.
* [sokol_time](https://github.com/floooh/sokol), zlib license.
* [ini.h](https://github.com/mattiasgustavsson/libs/blob/main/ini.h), 2022 Sep (aef7d92), MIT or Public Domain.

