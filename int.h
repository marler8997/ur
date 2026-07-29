#ifndef _INT_H
#define _INT_H

#ifdef _MSC_VER
    typedef unsigned __int8 u8;
    typedef unsigned __int16 u16;
    typedef unsigned __int32 u32;
    typedef unsigned __int64 u64;

    typedef signed __int32 i32;
    typedef signed __int64 i64;

#else
    typedef unsigned char u8;
    typedef unsigned short u16;
    typedef unsigned int u32;
    typedef unsigned long long u64;

    typedef int i32;
    typedef long long i64;
#endif

#endif // _INT_H
