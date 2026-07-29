#ifndef _WINDOWSPEB_H
#define _WINDOWSPEB_H

#include "int.h"

typedef void* HANDLE;

typedef struct {
    u16 size;
    u16 capacity;
    u16* ptr;
} UNICODE_STRING;

typedef struct {
    UNICODE_STRING DosPath;
    HANDLE Handle;
} CURDIR;

typedef struct {
    u8 Reserved1[16];   // 0x00
    HANDLE ConsoleHandle;          // 0x10  (the parent's console -- inherit it so no new window opens)
    u32 ConsoleFlags;              // 0x18
    u32 Reserved2;                 // 0x1c  (padding)
    HANDLE StandardInput;          // 0x20
    HANDLE StandardOutput;         // 0x28
    HANDLE StandardError;          // 0x30
    CURDIR CurrentDirectory;       // 0x38  (DosPath.Buffer @0x40)
    UNICODE_STRING DllPath;        // 0x50
    UNICODE_STRING ImagePathName;  // 0x60
    UNICODE_STRING CommandLine;    // 0x70  (Length @0x70, Buffer @0x78)
    u16* Environment;              // 0x80  UTF-16 NAME=VALUE\0 ... \0\0
    // The window/console startup block. We only need WindowFlags (it holds STARTUPINFO.dwFlags) so the child
    // honours STARTF_USESTDHANDLES and keeps our redirected std handles even when given a console.
    u32 StartingX;                 // 0x88
    u32 StartingY;                 // 0x8c
    u32 CountX;                    // 0x90
    u32 CountY;                    // 0x94
    u32 CountCharsX;               // 0x98
    u32 CountCharsY;               // 0x9c
    u32 FillAttribute;             // 0xa0
    u32 WindowFlags;               // 0xa4  STARTUPINFO.dwFlags; STARTF_USESTDHANDLES (0x100) keeps std handles
    u32 ShowWindowFlags;           // 0xa8
} RTL_USER_PROCESS_PARAMETERS;

typedef struct {
    u8 Reserved1[2];
    u8 BeingDebugged;
    u8 Reserved2[1];
    void* Reserved3[2];
    void* Ldr;
    RTL_USER_PROCESS_PARAMETERS* ProcessParameters;
} PEB;

static PEB* peb(void)
{
    PEB* p;
#if defined(__x86_64__)
    __asm__ volatile ("movq %%gs:0x60, %0" : "=r"(p));
#elif defined(__aarch64__)
    __asm__ volatile ("ldr %0, [x18, #0x60]" : "=r"(p));
#elif defined(_MSC_VER) && defined(_M_X64)
    p = (PEB*)__readgsqword(0x60);     // MSVC x64: no inline asm; read gs:0x60 via intrinsic
#else
    #error "peb(): unsupported architecture"
#endif
    return p;
}

#endif // _WINDOWSPEB_H
