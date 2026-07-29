#ifndef _WINDOWSNTSTATUS_H
#define _WINDOWSNTSTATUS_H

#include "abortmacros.h"   // ASSERT
#include "bool.h"
#include "error.h"         // Error
#include "int.h"

typedef u32 NTSTATUS;
static inline Bool ntstatus_failed(NTSTATUS status)
{
    return 0 != (status & 0x80000000);
}
static inline Error nterror(NTSTATUS status)
{
    ASSERT(ntstatus_failed(status));
    return status;
}
static inline Error error_from_nt(NTSTATUS status)
{
    if (ntstatus_failed(status)) return nterror(status);
    return 0;
}


#endif // _WINDOWSNTSTATUS_H
