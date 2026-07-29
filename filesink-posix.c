#include "abortmacros.h"
#include "arena.h"
#include "filesink.h"
#include "fs.h"
#include "mem.h"
#include "pagesize.h"
#include "processexit.h"
#include "sink.h"

#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

static Error write_all(int fd, const void* ptr, size_t len)
{
    const u8* p = ptr;
    size_t off = 0;
    while (off < len) {
        long n = write(fd, p + off, len - off);
        if (n < 0) return errno;
        off += (size_t)n;
    }
    return 0;
}

size_t query_page_size(void)
{
    long ps = sysconf(_SC_PAGESIZE);
    if (ps < (long)PAGE_SIZE_MIN) {
        char stderr_buf[200];
        Sink err = sink_init(&stderr_vtable, stderr_buf, sizeof(stderr_buf));
        MUST(SINK_LITERAL(&err, "page size < min: "));
        MUST(sink_format_i64(&err, ps));
        MUST(SINK_LITERAL(&err, "\n"));
        MUST(sink_flush(&err));
        process_exit(0xff);
    }
    size_t ps_size_t = (size_t)ps;
    ASSERT((long)ps_size_t == ps);
    return ps_size_t;
}

// ---- stdout / stderr ----------------------------------------------------------

static Error drain_fd(Sink* w, int fd, const IovConst* iov, size_t iov_count, size_t* iov_written)
{
    if (w->end) {
        Error e = write_all(fd, w->buffer, w->end);
        if (e) return e;
        w->end = 0;
    }

    *iov_written = 0;
    for (size_t i = 0; i < iov_count; i++) {
        Error e = write_all(fd, iov[i].ptr, iov[i].len);
        if (e) return e;
        *iov_written += iov[i].len;
    }
    return 0;
}
static Error drain_stdout(Sink* w, const IovConst* iov, size_t iov_count, size_t* iov_written)
{
    return drain_fd(w, STDOUT_FILENO, iov, iov_count, iov_written);
}
static Error drain_stderr(Sink* w, const IovConst* iov, size_t iov_count, size_t* iov_written)
{
    return drain_fd(w, STDERR_FILENO, iov, iov_count, iov_written);
}
static Error drain_file(Sink* w, const IovConst* iov, size_t iov_count, size_t* iov_written)
{
    FileSink* fw = (FileSink*)(((char*)w) - libc_offsetof(FileSink, base));
    return drain_fd(w, fileno((FILE*)fw->file.handle), iov, iov_count, iov_written);
}
const SinkVtable stdout_vtable = { drain_stdout };
const SinkVtable stderr_vtable = { drain_stderr };
const SinkVtable file_vtable = { drain_file };

void log_put(const char* string)
{
    if (write_all(STDERR_FILENO, string, libc_strlen(string)) != 0) process_exit(0xff);
}

// ---- files --------------------------------------------------------------------
// File.handle holds a stdio FILE*.

Error file_read(File file, u8* buf, size_t size, size_t* out_size)
{
    FILE* f = file.handle;
    size_t n = fread(buf, 1, size, f);
    *out_size = n;
    if (n < size && ferror(f)) return errno;   // short read with no error == EOF
    return 0;
}

void assert_native_path(const FilenameChar* path, size_t path_len)
{
    ASSERT(path[path_len] == 0);   // only the caller's promise (a _z/_native path) makes this read valid
}

static char* arena_cstr(const char* path, size_t path_len)
{
    char* cpath = ARENA_ALLOC(&global_arena, path_len + 1);
    for (size_t i = 0; i < path_len; i++) cpath[i] = path[i];
    cpath[path_len] = 0;
    return cpath;
}

Error dir_create_z(const char* path, size_t path_len)
{
    assert_native_path(path, path_len);
    if (mkdir(path, 0755) != 0 && errno != EEXIST) return errno;   // already existing is success
    return 0;
}
Error dir_create_span(const char* path, size_t path_len)
{
    ArenaPosition pos = arena_position(&global_arena);
    Error e = dir_create_z(arena_cstr(path, path_len), path_len);
    arena_reset(&global_arena, pos);
    return e;
}

Error file_create_z(const char* path, size_t path_len, u32 flags, File* out)
{
    assert_native_path(path, path_len);
    FILE* f = fopen(path, "wb");
    if (!f) return errno;
    if (flags & FILE_FLAG_EXECUTABLE) {
        if (chmod(path, 0755) != 0) {
            Error e = errno;
            fclose(f);
            return e;
        }
    }
    out->handle = f;
    return 0;
}
Error file_create_span(const char* path, size_t path_len, u32 flags, File* out)
{
    ArenaPosition pos = arena_position(&global_arena);
    Error e = file_create_z(arena_cstr(path, path_len), path_len, flags, out);
    arena_reset(&global_arena, pos);
    return e;
}
Error file_create_native(const FilenameChar* path, size_t path_len, u32 flags, File* out)
{
    return file_create_z(path, path_len, flags, out);
}

Error file_open_z(const char* path, size_t path_len, File* out)
{
    assert_native_path(path, path_len);
    FILE* f = fopen(path, "rb");
    if (!f) return errno;
    out->handle = f;
    return 0;
}
Error file_open_span(const char* path, size_t path_len, File* out)
{
    ArenaPosition pos = arena_position(&global_arena);
    Error e = file_open_z(arena_cstr(path, path_len), path_len, out);
    arena_reset(&global_arena, pos);
    return e;
}
Error file_open_native(const FilenameChar* path, size_t path_len, File* out)
{
    return file_open_z(path, path_len, out);
}

Error file_size(File file, size_t* out_size)
{
    FILE* f = file.handle;
    if (fseek(f, 0, SEEK_END) != 0) return errno;
    long n = ftell(f);
    if (n < 0) return errno;
    rewind(f);
    *out_size = (size_t)n;
    return 0;
}

Error file_cwd(u8* out, size_t cap, size_t* out_len)
{
    if (!getcwd((char*)out, cap)) return errno;
    size_t n = 0;
    while (n < cap && out[n]) n += 1;
    while (n > 0 && out[n - 1] == '/') n -= 1;                // drop any trailing separator
    *out_len = n;
    return 0;
}

Error file_write(File file, const u8* data, size_t len, size_t* out_written)
{
    size_t n = fwrite(data, 1, len, file.handle);
    if (n < len) return errno;
    *out_written = n;
    return 0;
}

void file_close(File file)
{
    ASSERT(0 == fclose(file.handle));
}
