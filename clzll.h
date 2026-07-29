#ifndef _CLZLL_H
#define _CLZLL_H

#include "hedley.h"

#if HEDLEY_HAS_BUILTIN(__builtin_clzll)
    #define clzll __builtin_clzll
#else
    unsigned long clzll(unsigned long long mask);
#endif

#endif // _CLZLL_H
