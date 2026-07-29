#include "abortmacros.h"
#include "arena.h"
#include "filesink.h"
#include "fs.h"
#include "linux-syscall.h"
#include "mem.h"
#include "processexit.h"
#include "sink.h"

#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

#define O_RDONLY 00
#define O_WRONLY 01
#define SYS_lseek 8
#define SEEK_SET 0
#define SEEK_END 2
#define O_CREAT  0100
#define O_TRUNC  01000

#define EEXIST       17
#define ENOMEM       12
#define ENAMETOOLONG 36

void _exit(int code)
{
    syscall1(SYS_exit, code);
    HEDLEY_UNREACHABLE();
}

// ---- helpers ------------------------------------------------------------------

static Error write_all(int fd, const void* ptr, size_t len)
{
    const u8* p = ptr;
    size_t off = 0;
    while (off < len) {
        long n = syscall3(SYS_write, fd, (long)(p + off), (long)(len - off));
        if (n < 0) return (int)(-n);
        off += (size_t)n;
    }
    return 0;
}

// ---- Pages (see pages.h): reserve + commit-on-demand; relocate (copy) only when a grow
// outgrows the reservation, which doubles geometrically (a floor, not a cap) ----

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
    return drain_fd(w, fw->file.fd, iov, iov_count, iov_written);
}
const SinkVtable stdout_vtable = { drain_stdout };
const SinkVtable stderr_vtable = { drain_stderr };
const SinkVtable file_vtable = { drain_file };

void log_put(const char* string)
{
    if (write_all(STDERR_FILENO, string, libc_strlen(string)) != 0) process_exit(0xff);
}

// ---- files --------------------------------------------------------------------

Error file_read(File file, u8* buf, size_t size, size_t* out_size)
{
    long n = syscall3(SYS_read, file.fd, (long)buf, (long)size);
    if (n < 0) {
        *out_size = 0;
        return (int)(-n);
    }
    *out_size = (size_t)n;
    return 0;
}

void assert_native_path(const FilenameChar* path, size_t path_len)
{
    ASSERT(path[path_len] == 0);   // only the caller's promise (a _z/_native path) makes this read valid
}

// Paths are native UTF-8 on Linux, so the only work a sized path may need is a NUL terminator. The
// _z/_native variants are already NUL-terminated (passed straight through); _span NUL-terminates via
// a single arena allocation (a span doesn't own path[path_len], so it must never peek there).
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
    long r = syscall3(SYS_mkdir, (long)path, 0755, 0);   // mkdir takes 2 args; the third is ignored
    if (r < 0 && (int)(-r) != EEXIST) return (int)(-r);  // already existing is success
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
    int mode = (flags & FILE_FLAG_EXECUTABLE) ? 0755 : 0644;
    int fd = syscall3(SYS_open, (long)path, (long)(O_WRONLY | O_CREAT | O_TRUNC), (long)mode);
    if (fd < 0) return (int)(-fd);
    out->fd = fd;
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
    int fd = syscall3(SYS_open, (long)path, (long)O_RDONLY, 0);
    if (fd < 0) return (int)(-fd);
    out->fd = fd;
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
    long end = syscall3(SYS_lseek, file.fd, 0, (long)SEEK_END);
    if (end < 0) return (int)(-end);
    long back = syscall3(SYS_lseek, file.fd, 0, (long)SEEK_SET);
    if (back < 0) return (int)(-back);
    *out_size = (size_t)end;
    return 0;
}

#define SYS_getcwd 79
Error file_cwd(u8* out, size_t cap, size_t* out_len)
{
    long r = syscall3(SYS_getcwd, (long)out, (long)cap, 0);   // writes a NUL-terminated path; returns its size
    if (r < 0) return (int)(-r);
    size_t n = (size_t)r;
    if (n > 0) n -= 1;                                        // strip the trailing NUL the kernel counts
    while (n > 0 && out[n - 1] == '/') n -= 1;                // drop any trailing separator
    *out_len = n;
    return 0;
}

Error file_write(File file, const u8* data, size_t len, size_t* out_written)
{
    long n = syscall3(SYS_write, file.fd, (long)data, (long)len);
    if (n < 0) return (int)(-n);
    *out_written = (size_t)n;
    return 0;
}

void file_close(File file)
{
    ASSERT(0 == syscall1(SYS_close, file.fd));
}
