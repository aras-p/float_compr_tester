#pragma once

#include <stdint.h>
#include <stddef.h>

void Filter_Null(const uint8_t* src, uint8_t* dst, int channels, size_t dataElems);
void UnFilter_Null(const uint8_t* src, uint8_t* dst, int channels, size_t dataElems);

// Part 6
void Filter_A(const uint8_t* src, uint8_t* dst, int channels, size_t dataElems);
void UnFilter_A(const uint8_t* src, uint8_t* dst, int channels, size_t dataElems);

void Filter_B(const uint8_t* src, uint8_t* dst, int channels, size_t dataElems);
void UnFilter_B(const uint8_t* src, uint8_t* dst, int channels, size_t dataElems);

void Filter_D(const uint8_t* src, uint8_t* dst, int channels, size_t dataElems);
void UnFilter_D(const uint8_t* src, uint8_t* dst, int channels, size_t dataElems);

// Part 7
// Just like B, except delta individual streams and not everything
void Filter_F(const uint8_t* src, uint8_t* dst, int channels, size_t dataElems);
void UnFilter_F(const uint8_t* src, uint8_t* dst, int channels, size_t dataElems);

// Scalar, fetch from N streams, write sequential
void UnFilter_G(const uint8_t* src, uint8_t* dst, int channels, size_t dataElems);
