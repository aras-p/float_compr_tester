#pragma once

#include <stddef.h>

void ResCacheInit();
void ResCacheClose();

bool ResCacheGet(const char* name, int level, size_t* outSize, double* outCmpTime, double* outDecTime);
void ResCacheSet(const char* name, int level, size_t size, double cmpTime, double decTime);
