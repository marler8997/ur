#ifndef _MEM_H
#define _MEM_H

#include "hedley.h"
#include "size_t.h"

#if HEDLEY_HAS_BUILTIN(__builtin_offsetof)
    #define libc_offsetof __builtin_offsetof
#else
    #define libc_offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)
#endif

#if defined(UR_HAVE_LIBC)
    #define libc_strlen strlen
    size_t strlen(const char*);
#else
    size_t libc_strlen(const char*);
#endif

#if HEDLEY_HAS_BUILTIN(__builtin_memset)
    #define libc_memset __builtin_memset
#elif defined(UR_HAVE_LIBC)
    #define libc_memset memset
    void* memset(void*, int, size_t);
#else
    void* libc_memset(void*, int, size_t);
#endif

#if HEDLEY_HAS_BUILTIN(__builtin_memcpy)
    #define libc_memcpy __builtin_memcpy
#elif defined(UR_HAVE_LIBC)
    #define libc_memcpy memcpy
    void* memcpy(void*, const void*, size_t);
#else
    void* libc_memcpy(void*, const void*, size_t);
#endif

#if HEDLEY_HAS_BUILTIN(__builtin_memcmp)
    #define libc_memcmp __builtin_memcmp
#elif defined(UR_HAVE_LIBC)
    #define libc_memcmp memcmp
    int memcmp(const void*, const void*, size_t);
#else
    int libc_memcmp(const void*, const void*, size_t);
#endif

// str* (like strlen) have no compiler-rt provider, so the freestanding build
// uses our own libc_str* from mem.c rather than a __builtin_ that would lower
// to an undefined call.
#if defined(UR_HAVE_LIBC)
    #define libc_strcmp strcmp
    int strcmp(const char*, const char*);
#else
    int libc_strcmp(const char*, const char*);
#endif

#if defined(UR_HAVE_LIBC)
    #define libc_strncmp strncmp
    int strncmp(const char*, const char*, size_t);
#else
    int libc_strncmp(const char*, const char*, size_t);
#endif

#if defined(UR_HAVE_LIBC)
    #define libc_strchr strchr
    char* strchr(const char*, int);
#else
    char* libc_strchr(const char*, int);
#endif

#endif // _MEM_H
