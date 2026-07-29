#ifndef _SIZE_T_H
#define _SIZE_T_H

#ifdef __SIZE_TYPE__
    typedef __SIZE_TYPE__ size_t; // clang, gcc, cc, tcc
#elif defined(_MSC_VER)
    #ifdef _WIN64
        typedef unsigned __int64 size_t;
    #else
        typedef unsigned __int32 size_t;
    #endif
#else
    typedef unsigned long long size_t;  // last resort, 8 bytes on any 64-bit target
#endif

#endif // _SIZE_T_H
