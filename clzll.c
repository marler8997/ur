#include "clzll.h"

#if !HEDLEY_HAS_BUILTIN(__builtin_clzll)
    #ifdef _MSC_VER
        #include <intrin.h>
        #pragma intrinsic(_BitScanReverse64)

        unsigned long clzll(unsigned long long mask) {
            unsigned long index;
            if (_BitScanReverse64(&index, mask)) {
                return 63UL - index;
            } else {
                return 64UL; // Undefined for 0, but 64 is standard behavior
            }
        }
    #else
        unsigned long clzll(unsigned long long mask) {
            if (mask == 0) return 64UL;
            unsigned long long idx;
            __asm__ ("bsrq %1, %0" : "=r"(idx) : "r"(mask));
            return 63UL - (unsigned long)idx;
        }
    #endif
#endif
