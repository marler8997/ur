#ifndef _PAGESIZE_H
#define _PAGESIZE_H

#include "size_t.h"

#define PAGE_SIZE_MIN 4096

size_t page_size(void); // cheap, queries onces then caches the value

#if defined(__linux__)
    #define PAGE_SIZE_LINUX 4096
#endif

#endif // _PAGESIZE_H
