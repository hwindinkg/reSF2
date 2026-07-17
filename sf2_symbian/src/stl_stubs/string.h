#pragma once
// string.h shim for WINSCW
// NOTE: mem*/ functions are provided by e32cmn.h — do NOT declare them here
unsigned int strlen(const char* s);
int strcmp(const char* a, const char* b);
int strncmp(const char* a, const char* b, unsigned int n);
char* strcpy(char* d, const char* s);
char* strncpy(char* d, const char* s, unsigned int n);
char* strcat(char* d, const char* s);
const char* strstr(const char* h, const char* n);
char* strchr(const char* s, int c);
