#include "abortmacros.h"
#include "int.h"

void verify_int_sizes(void)
{
    ASSERT(sizeof(u8) == 1);
    ASSERT(sizeof(u16) == 2);
    ASSERT(sizeof(u32) == 4);
    ASSERT(sizeof(u64) == 8);
    ASSERT(sizeof(i32) == 4);
    ASSERT(sizeof(i64) == 8);
    // format.c currently relies on this
    ASSERT(sizeof(size_t) <= sizeof(unsigned long long));
}
