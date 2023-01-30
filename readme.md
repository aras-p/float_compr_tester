# Testing compression approaches for floating point data

:warning: The code is a mess right now, don't look at it!

A series of experiments in compressing semi-structured floating point data. Blog post series about it:

* [Part 0: Intro](https://aras-p.info/blog/2023/01/29/Float-Compression-0-Intro/)
* [Part 1: Generic Compression Libraries](https://aras-p.info/blog/2023/01/29/Float-Compression-1-Generic/)


Code here uses 3rd party libraries:
* [zstd](https://github.com/facebook/zstd), v1.5.2, BSD or GPLv2 license.
* [lz4](https://github.com/lz4/lz4), v1.9.3, BSD license.
* [zlib](https://github.com/madler/zlib), 1.2.13, zlib license.
* [brotli](https://github.com/google/brotli), v1.0.9, MIT license.
* [sokol_time](https://github.com/floooh/sokol), zlib license.
