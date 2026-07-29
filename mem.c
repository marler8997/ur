#include "hedley.h"
#include "mem.h"

#ifndef UR_HAVE_LIBC

size_t libc_strlen(const char* s)
{
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

int libc_strcmp(const char* a, const char* b)
{
    while (*a && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

int libc_strncmp(const char* a, const char* b, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        unsigned char x = (unsigned char)a[i], y = (unsigned char)b[i];
        if (x != y) return (int)x - (int)y;
        if (x == 0) break;
    }
    return 0;
}

char* libc_strchr(const char* s, int c)
{
    for (;; s++) {
        if (*s == (char)c) return (char*)s;
        if (!*s) return 0;
    }
}

#if !HEDLEY_HAS_BUILTIN(__builtin_memcpy)
    void* libc_memcpy(void* dst, const void* src, size_t n)
    {
        unsigned char* d = dst;
        const unsigned char* s = src;
        for (size_t i = 0; i < n; i++) d[i] = s[i];
        return dst;
    }
#endif

#if !HEDLEY_HAS_BUILTIN(__builtin_memset)
    void* libc_memset(void* dst, int c, size_t n)
    {
        unsigned char* d = dst;
        for (size_t i = 0; i < n; i++) d[i] = (unsigned char)c;
        return dst;
    }
#endif

#if !HEDLEY_HAS_BUILTIN(__builtin_memcmp)
    int libc_memcmp(const void* a, const void* b, size_t n)
    {
        const unsigned char* x = a;
        const unsigned char* y = b;
        for (size_t i = 0; i < n; i++) if (x[i] != y[i]) return (int)x[i] - (int)y[i];
        return 0;
    }
#endif

#endif
