#pragma once

#include <stdint.h>
#include <stddef.h>

void Filter_Null(const uint8_t* src, uint8_t* dst, int channels, size_t dataElems);
void UnFilter_Null(const uint8_t* src, uint8_t* dst, int channels, size_t dataElems);

void Filter_Shuffle(const uint8_t* src, uint8_t* dst, int channels, size_t dataElems);
void UnFilter_Shuffle(const uint8_t* src, uint8_t* dst, int channels, size_t dataElems);


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

// Scalar, fetch 1b from N streams, write sequential
void UnFilter_G(const uint8_t* src, uint8_t* dst, int channels, size_t dataElems);

// Fetch process 16xN bytes at once
void Filter_H(const uint8_t* src, uint8_t* dst, int channels, size_t dataElems);
void UnFilter_H(const uint8_t* src, uint8_t* dst, int channels, size_t dataElems);

// Like H, but special code path for channels==16 case
void UnFilter_I(const uint8_t* src, uint8_t* dst, int channels, size_t dataElems);

// Like H, but fetch more than 16 bytes from each channel
void UnFilter_J(const uint8_t* src, uint8_t* dst, int channels, size_t dataElems);
