#ifndef _FS_H
#define _FS_H

#include "int.h"
#include "size_t.h"
#include "error.h"

typedef struct struct_File {
    #ifdef __linux__
        int fd;
    #else
        void* handle;
    #endif
} File;

#ifdef _WIN32
    typedef u16 FilenameChar;
#else
    typedef char FilenameChar;
#endif

#define FILE_FLAG_EXECUTABLE 0x1

// suffix: _z (utf8 null-terminated)
// suffix: _span (utf8, not null-terminated)
// suffix: _native (windows: wtf16-string and not null-terminatd, otherwise utf8 null-terminate)
// if you already have a null-terminated string, use the _z, if not,
// you might as well use _n because doing the conversion yourself may
// be unnecessary because the platform has to allocate/modify the path anyway.

void assert_native_path(const FilenameChar* path, size_t path_len);

Error dir_create_z(const char* path, size_t path_len);
Error dir_create_span(const char* path, size_t path_len);
/* Error dir_create_native(const FilenameChar* path, size_t path_len); */

Error file_create_z(const char* path, size_t path_len, u32 flags, File* out);
Error file_create_span(const char* path, size_t path_len, u32 flags, File* out);
Error file_create_native(const FilenameChar* path, size_t path_len, u32 flags, File* out);

Error file_open_z(const char* path, size_t path_len, File* out);
Error file_open_span(const char* path, size_t path_len, File* out);
Error file_open_native(const FilenameChar* path, size_t path_len, File* out);

// The process's current working directory as UTF-8 into `out` (not NUL-terminated), with any trailing
// separator removed; *out_len receives the length. Fails if it does not fit in `cap`.
Error file_cwd(u8* out, size_t cap, size_t* out_len);

Error file_size(File, size_t* out_size);
Error file_read(File, u8*, size_t size, size_t* out_size);
Error file_write(File, const u8* data, size_t len, size_t* out_written);
void file_close(File);

#endif // _FS_H
