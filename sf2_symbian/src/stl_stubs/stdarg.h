#pragma once
typedef char* va_list;
#define va_start(ap, param) ((void)(ap = (va_list)((char*)&(param) + sizeof(param))))
#define va_arg(ap, type) (*(type*)((ap += sizeof(type)) - sizeof(type)))
#define va_end(ap) ((void)(ap = 0))
