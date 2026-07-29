#ifndef _WINDOWS_NTIO_H
#define _WINDOWS_NTIO_H

// Shared ntdll I/O types used by more than one translation unit (filesink + subprocess). Each is an
// anonymous-struct typedef, so it must have ONE definition -- redefining it in two files is an error in
// the unity build. Function declarations (NtReadFile, ...) may stay duplicated; identical decls are fine.

#include "int.h"
#include "windows-ntstatus.h"   // NTSTATUS
#include "windows-peb.h"        // HANDLE, UNICODE_STRING

typedef struct {
    union { NTSTATUS Status; void* Pointer; } u;
    size_t Information;
} IO_STATUS_BLOCK;

typedef struct {
    u32 Length;
    HANDLE RootDirectory;
    UNICODE_STRING* ObjectName;
    u32 Attributes;
    void* SecurityDescriptor;
    void* SecurityQualityOfService;
} OBJECT_ATTRIBUTES;

#endif // _WINDOWS_NTIO_H
