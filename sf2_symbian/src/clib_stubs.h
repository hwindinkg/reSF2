// clib_stubs.h — declarations for C runtime stubs
// These are extern "C" functions provided by clib_stubs.cpp

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// string.h
unsigned int strlen(const char* s);
int strcmp(const char* a, const char* b);
int strncmp(const char* a, const char* b, unsigned int n);
char* strcpy(char* d, const char* s);
char* strncpy(char* d, const char* s, unsigned int n);
char* strcat(char* d, const char* s);
const char* strstr(const char* h, const char* n);
char* strchr(const char* s, int c);

// stdlib.h
int atoi(const char* s);
void* malloc(unsigned int size);
void free(void* ptr);
void* realloc(void* ptr, unsigned int size);
void* calloc(unsigned int count, unsigned int size);
int abs(int x);

// math.h
double sin(double x);
double cos(double x);
double sqrt(double x);
double pow(double x, double y);
double floor(double x);
double ceil(double x);
double fmod(double x, double y);
double fabs(double x);
double atan2(double y, double x);
double atan(double x);
double acos(double x);
double asin(double x);
double tan(double x);
double exp(double x);
double log(double x);
double log10(double x);

// stdio.h
int sprintf(char* buf, const char* fmt, ...);
int snprintf(char* buf, unsigned int n, const char* fmt, ...);

// stdint.h typedefs (if not in stdint.h shim)
typedef signed char int8_t;
typedef unsigned char uint8_t;
typedef short int16_t;
typedef unsigned short uint16_t;
typedef long int32_t;
typedef unsigned long uint32_t;
typedef long long int64_t;
typedef unsigned long long uint64_t;

#ifdef __cplusplus
}
#endif
