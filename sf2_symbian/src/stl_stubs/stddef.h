#pragma once
#ifndef NULL
#define NULL 0
#endif
typedef unsigned int size_t;
typedef int ptrdiff_t;
#define offsetof(type, member) ((size_t)&((type*)0)->member)
