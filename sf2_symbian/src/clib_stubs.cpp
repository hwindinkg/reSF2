// C runtime stubs for Symbian GCCE (no libc)
// Provides: math, string (non-mem*), stdlib, stdio
// NOTE: memcpy/memset/memmove/memcmp are in e32cmn.h — do NOT redeclare

#include <e32std.h>
#include <e32math.h>
#include <e32def.h>

extern "C" {

// ---- string.h (non-mem*) ----
size_t strlen(const char* s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

int strcmp(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return *(unsigned char*)a - *(unsigned char*)b;
}

int strncmp(const char* a, const char* b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i]) return (unsigned char)a[i] - (unsigned char)b[i];
        if (!a[i]) return 0;
    }
    return 0;
}

char* strcpy(char* d, const char* s) {
    char* r = d;
    while ((*d++ = *s++));
    return r;
}

char* strncpy(char* d, const char* s, size_t n) {
    size_t i;
    for (i = 0; i < n && s[i]; i++) d[i] = s[i];
    for (; i < n; i++) d[i] = 0;
    return d;
}

char* strcat(char* d, const char* s) {
    char* r = d;
    d += strlen(d);
    while ((*d++ = *s++));
    return r;
}

const char* strstr(const char* h, const char* n) {
    if (!*n) return h;
    size_t nl = strlen(n);
    while (*h) {
        if (strncmp(h, n, nl) == 0) return h;
        h++;
    }
    return NULL;
}

char* strchr(const char* s, int c) {
    while (*s) {
        if (*s == (char)c) return (char*)s;
        s++;
    }
    return NULL;
}

int atoi(const char* s) {
    int r = 0, sign = 1;
    while (*s == ' ') s++;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') { r = r * 10 + (*s - '0'); s++; }
    return sign * r;
}

// ---- stdlib.h ----
void* malloc(unsigned int size) {
    if (size == 0) return NULL;
    return User::Alloc(size);
}

void free(void* ptr) {
    if (ptr) User::Free(ptr);
}

void* realloc(void* ptr, unsigned int size) {
    if (ptr == NULL) return malloc(size);
    if (size == 0) { free(ptr); return NULL; }
    return User::ReAlloc(ptr, size);
}

void* calloc(unsigned int count, unsigned int size) {
    unsigned int total = count * size;
    void* p = malloc(total);
    if (p) Mem::FillZ(p, total);
    return p;
}

int abs(int x) { return x < 0 ? -x : x; }

// ---- math.h ----
double sin(double x) {
    TReal r;
    Math::Sin(r, x);
    return r;
}

double cos(double x) {
    TReal r;
    Math::Cos(r, x);
    return r;
}

double sqrt(double x) {
    TReal r;
    Math::Sqrt(r, x);
    return r;
}

double pow(double x, double y) {
    TReal r;
    Math::Pow(r, x, y);
    return r;
}

double floor(double x) {
    long i = (long)x;
    if (x >= 0.0 || (double)i == x) return (double)i;
    return (double)(i - 1);
}

double ceil(double x) {
    long i = (long)x;
    if (x <= 0.0 || (double)i == x) return (double)i;
    return (double)(i + 1);
}

double fmod(double x, double y) {
    if (y == 0.0) return 0.0;
    double q = x / y;
    double t = (q < 0.0) ? -(double)(long)(-q) : (double)(long)q;
    return x - t * y;
}

double fabs(double x) { return x < 0.0 ? -x : x; }

double atan2(double y, double x) {
    if (x > 0.0) { TReal r; Math::ATan(r, y / x); return r; }
    else if (x < 0.0) {
        TReal r;
        Math::ATan(r, y / x);
        return y >= 0.0 ? r + 3.14159265358979 : r - 3.14159265358979;
    } else {
        return y > 0 ? 3.14159265358979 / 2 : (y < 0 ? -3.14159265358979 / 2 : 0.0);
    }
}

double atan(double x) {
    TReal r;
    Math::ATan(r, x);
    return r;
}

double acos(double x) {
    TReal r;
    Math::ACos(r, x);
    return r;
}

double asin(double x) {
    TReal r;
    Math::ASin(r, x);
    return r;
}

double tan(double x) {
    TReal r;
    Math::Tan(r, x);
    return r;
}

double exp(double x) {
    TReal r;
    Math::Exp(r, x);
    return r;
}

double log(double x) {
    TReal r;
    Math::Log(r, x);
    return r;
}

double log10(double x) {
    TReal r;
    Math::Log10(r, x);
    return r;
}

// ---- stdio.h (minimal) ----
int sprintf(char* buf, const char* fmt, ...) {
    // Minimal stub — will be replaced with actual implementation
    if (buf) buf[0] = 0;
    return 0;
}

int snprintf(char* buf, size_t n, const char* fmt, ...) {
    if (buf && n > 0) buf[0] = 0;
    return 0;
}

}  // extern "C"
