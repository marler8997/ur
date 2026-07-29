#include "abortmacros.h"
#include "arena.h"
#include "filesink.h"
#include "fs.h"
#include "log.h"
#include "mem.h"
#include "processexit.h"
#include "sink.h"
#include "unicode.h"
#include "windows-ntio.h"
#include "windows-ntstatus.h"
#include "windows-peb.h"

#if defined(_WIN32) && defined(_MSC_VER)
    #pragma comment(lib, "ntdll")
#endif

#define STATUS_PENDING 0x00000103
#define STATUS_END_OF_FILE 0xC0000011   /* read past end of a regular file */
#define STATUS_PIPE_BROKEN 0xC000014B   /* pipe's write end closed -> our EOF */
#define STATUS_OBJECT_PATH_INVALID 0xC0000039   /* path resolution failed */
#define STATUS_NAME_TOO_LONG 0xC0000106         /* path longer than we can hold */

NTSTATUS NtWriteFile(
    HANDLE FileHandle,
    HANDLE Event,
    void* ApcRoutine,
    void* ApcContext,
    IO_STATUS_BLOCK* IoStatusBlock,
    const void* Buffer,
    u32 Length,
    long long* ByteOffset,
    u32* Key
);
NTSTATUS NtReadFile(
    HANDLE FileHandle,
    HANDLE Event,
    void* ApcRoutine,
    void* ApcContext,
    IO_STATUS_BLOCK* IoStatusBlock,
    void* Buffer,
    u32 Length,
    long long* ByteOffset,
    u32* Key
);
NTSTATUS NtWaitForSingleObject(HANDLE Handle, unsigned char Alertable, long long* Timeout);
void RtlExitUserProcess(int);

#ifdef _MSC_VER
    unsigned __int64 __readgsqword(unsigned long Offset);
    #pragma intrinsic(__readgsqword)
#endif

// Reserve a region (one VAD, no committed pages), then commit on demand by extending the
// committed prefix -- so commits inside the reservation never copy. When a grow outgrows the
// reservation, reserve a fresh, geometrically larger region, copy the live bytes over, and
// release the old one: the base moves (the "movable" contract). The reservation is a growth
// floor that doubles, not a ceiling, so the real limit is the OS. Pages.handle holds the
// current reserved cap.

NTSTATUS NtClose(HANDLE Handle);

#define STATUS_NO_MEMORY 0xC0000017

static NTSTATUS nt_write_file(HANDLE handle, const void* ptr, u32 len, u32* out_written)
{
    IO_STATUS_BLOCK iosb;
    iosb.Information = 0;
    NTSTATUS status = NtWriteFile(
        handle,
        0,     // No event
        0,     // No APC
        0,     // No APC context
        &iosb,
        ptr,
        len,
        0,     // No offset (auto-advance)
        0      // No key
    );
    if (status == STATUS_PENDING) {
        *out_written = 0;
    } else {
        *out_written = iosb.Information;
    }
    return status;
}

NTSTATUS NtCreateFile(
    HANDLE* FileHandle, u32 DesiredAccess, OBJECT_ATTRIBUTES* ObjectAttributes,
    IO_STATUS_BLOCK* IoStatusBlock, i64* AllocationSize, u32 FileAttributes,
    u32 ShareAccess, u32 CreateDisposition, u32 CreateOptions,
    void* EaBuffer, u32 EaLength
);

#define GENERIC_READ                 0x80000000
#define GENERIC_WRITE                0x40000000
#define SYNCHRONIZE                  0x00100000
#define OBJ_CASE_INSENSITIVE         0x00000040
#define FILE_ATTRIBUTE_NORMAL        0x00000080
#define FILE_SHARE_READ              0x00000001
#define FILE_OPEN                    1
#define FILE_OPEN_IF                 3
#define FILE_OVERWRITE_IF            5
#define FILE_SYNCHRONOUS_IO_NONALERT 0x00000020
#define FILE_NON_DIRECTORY_FILE      0x00000040
#define FILE_DIRECTORY_FILE          0x00000001

// A path's NT classification: whether it is drive-absolute ("C:\...", which needs the "\??\" prefix
// and a NULL root) vs relative (resolved against the process CWD handle). cc never produces UNC/
// rooted/drive-relative paths, so those fail loud. c0/c1/c2 are the first three code units (-1 if
// absent), which are ASCII either way so the test is identical for UTF-8 and WTF-16 inputs.
static Bool nt_drive_absolute(int c0, int c1, int c2)
{
    Bool slash0 = c0 == '\\' || c0 == '/';
    Bool colon1 = c1 == ':';
    Bool drive_abs = colon1 && (c2 == '\\' || c2 == '/');
    if (slash0 || (colon1 && !drive_abs)) {
        LOG_STRING("resolve_nt_path: unsupported path form (UNC/rooted/drive-relative)");
        process_exit(72);
    }
    return drive_abs;
}

// The RootDirectory a non-absolute NT path is relative to: the process current-directory handle.
static HANDLE nt_cwd_handle(void)
{
    HANDLE cwd = peb()->ProcessParameters->CurrentDirectory.Handle;
    if (!cwd) { LOG_STRING("resolve_nt_path: no current-directory handle"); process_exit(72); }
    return cwd;
}

// Copy src[0..n] into dst in one pass: normalize '/'->'\', collapse repeated separators, and resolve
// "."/".." segments (".." drops the preceding segment). A ".." with nothing to drop "escapes the base":
// it is NOT written, just counted, and the count is returned (the canonical tail -- the part below the
// escaping ".." -- is what lands in dst, NUL-terminated, with its length via *out_len). dst may alias src
// for in-place use (the write index never outruns the read index); dst must hold n+1 u16. NT does not
// resolve "."/".." for RootDirectory-relative opens, and cc no longer calls RtlGetFullPathName_U.
static size_t collapse_into(u16* dst, const u16* src, size_t n, size_t* out_len)
{
    Bool lead = n > 0 && (src[0] == '\\' || src[0] == '/');   // keep a leading separator (absolute tail)
    if (lead) dst[0] = '\\';
    size_t base = lead ? 1 : 0;                               // segments live in [base, w)
    size_t w = base;
    size_t cnt = 0;
    size_t escapes = 0;
    size_t i = 0;
    while (i < n) {
        while (i < n && (src[i] == '\\' || src[i] == '/')) i++;
        size_t seg = i;
        while (i < n && src[i] != '\\' && src[i] != '/') i++;
        size_t seglen = i - seg;
        if (seglen == 0) break;
        if (seglen == 1 && src[seg] == '.') continue;          // "." -> drop
        if (seglen == 2 && src[seg] == '.' && src[seg + 1] == '.') {
            if (cnt == 0) { escapes++; continue; }             // escapes the base -> counted, not written
            size_t p = w;                                      // find the last written segment's start
            while (p > base && dst[p - 1] != '\\') p--;
            w = (p > base) ? p - 1 : base;                     // remove it (and its leading '\')
            cnt--;
            continue;
        }
        if (cnt > 0) dst[w++] = '\\';
        for (size_t k = seg; k < seg + seglen; k++) dst[w++] = src[k];
        cnt++;
    }
    dst[w] = 0;
    *out_len = w;
    return escapes;
}

// A relative path whose ".." climbed `up` levels above the CWD: splice it onto the CWD's absolute path
// (from the PEB DosPath, no syscall) with `up` trailing components removed, as "\??\C:\...\tail". The
// result is absolute, so it opens with RootDirectory = null.
static void resolve_escaped(const u16* tail, size_t tail_len, size_t up, u16** out_name, size_t* out_len)
{
    UNICODE_STRING* dp = &peb()->ProcessParameters->CurrentDirectory.DosPath;
    const u16* cwd = dp->ptr;
    size_t cwd_len = dp->size / 2;
    while (cwd_len > 0 && (cwd[cwd_len - 1] == '\\' || cwd[cwd_len - 1] == '/')) cwd_len--;   // trailing sep
    for (size_t c = 0; c < up; c++) {
        size_t sep = cwd_len;                              // sep = start of the last component
        while (sep > 0 && cwd[sep - 1] != '\\' && cwd[sep - 1] != '/') sep--;
        if (sep <= 2) { LOG_STRING("resolve_nt_path: '..' climbs above the drive root"); process_exit(72); }
        cwd_len = sep - 1;                                 // drop the component and its leading separator
    }

    u16* name = ARENA_ALLOC(&global_arena, (4 + cwd_len + 1 + tail_len + 1) * sizeof(u16));
    size_t o = 0;
    name[o++] = '\\'; name[o++] = '?'; name[o++] = '?'; name[o++] = '\\';
    for (size_t k = 0; k < cwd_len; k++) name[o++] = cwd[k];
    if (tail_len > 0) {
        name[o++] = '\\';
        for (size_t k = 0; k < tail_len; k++) name[o++] = tail[k];
    }
    name[o] = 0;
    *out_name = name;
    *out_len = o;
}

// Finish a relative path: keep it relative to the CWD handle, or (if its ".." escaped the CWD) splice it
// onto the CWD's absolute path and open at the null root.
static void finish_relative(u16* tail, size_t tail_len, size_t escapes, u16** out_name, size_t* out_len, HANDLE* out_root)
{
    if (escapes == 0) {
        *out_root = nt_cwd_handle();
        *out_name = tail;
        *out_len = tail_len;
    } else {
        resolve_escaped(tail, tail_len, escapes, out_name, out_len);
        *out_root = 0;
    }
}

// Resolve a native (WTF-16) Win32 path to a NUL-terminated NT ObjectName (a single arena allocation)
// plus the RootDirectory it is relative to. Drive-absolute paths become "\??\C:\..."; relative ones
// pass through against the CWD handle. '/'->'\' and "."/".." collapse happen in the single copy pass.
static void resolve_nt_path_w(const u16* w, size_t wn, u16** out_name, size_t* out_len, HANDLE* out_root)
{
    Bool drive_abs = nt_drive_absolute(wn >= 1 ? w[0] : -1, wn >= 2 ? w[1] : -1, wn >= 3 ? w[2] : -1);
    if (drive_abs) {
        u16* name = ARENA_ALLOC(&global_arena, (4 + wn + 1) * sizeof(u16));
        name[0] = '\\'; name[1] = '?'; name[2] = '?'; name[3] = '\\';
        name[4] = w[0]; name[5] = w[1];                                          // drive "C:"
        size_t tail_len;
        size_t escapes = collapse_into(name + 6, w + 2, wn - 2, &tail_len);
        if (escapes) { LOG_STRING("resolve_nt_path: '..' climbs above the drive root"); process_exit(72); }
        *out_root = 0; *out_name = name; *out_len = 6 + tail_len;
    } else {
        u16* tail = ARENA_ALLOC(&global_arena, (wn + 1) * sizeof(u16));
        size_t tail_len;
        size_t escapes = collapse_into(tail, w, wn, &tail_len);
        finish_relative(tail, tail_len, escapes, out_name, out_len, out_root);
    }
}

// As resolve_nt_path_w, but for a UTF-8 path: the irreducible utf8->wtf16 decode is the only extra pass;
// the collapse then runs in place over the decoded buffer.
static void resolve_nt_path_a(const char* path, size_t path_len, u16** out_name, size_t* out_len, HANDLE* out_root)
{
    Bool drive_abs = nt_drive_absolute(path_len >= 1 ? (u8)path[0] : -1,
                                       path_len >= 2 ? (u8)path[1] : -1,
                                       path_len >= 3 ? (u8)path[2] : -1);
    size_t wn = wtf16_from_utf8((const u8*)path, path_len, 0);
    if (drive_abs) {
        u16* name = ARENA_ALLOC(&global_arena, (4 + wn + 1) * sizeof(u16));
        wtf16_from_utf8((const u8*)path, path_len, name + 4);                    // decode "C:..." into name+4
        name[0] = '\\'; name[1] = '?'; name[2] = '?'; name[3] = '\\';
        size_t tail_len;
        size_t escapes = collapse_into(name + 6, name + 6, wn - 2, &tail_len);   // in place after "C:"
        if (escapes) { LOG_STRING("resolve_nt_path: '..' climbs above the drive root"); process_exit(72); }
        *out_root = 0; *out_name = name; *out_len = 6 + tail_len;
    } else {
        u16* tail = ARENA_ALLOC(&global_arena, (wn + 1) * sizeof(u16));
        wtf16_from_utf8((const u8*)path, path_len, tail);
        size_t tail_len;
        size_t escapes = collapse_into(tail, tail, wn, &tail_len);              // in place
        finish_relative(tail, tail_len, escapes, out_name, out_len, out_root);
    }
}

// NtCreateFile a resolved NT ObjectName; `access`/`disposition`/`options` select the operation.
static Error nt_create(u16* nt_name, size_t nt_len, HANDLE root, u32 access, u32 disposition, u32 options, HANDLE* out)
{
    UNICODE_STRING name;
    name.size = (u16)(nt_len * 2);
    name.capacity = name.size;
    name.ptr = nt_name;

    OBJECT_ATTRIBUTES oa;
    oa.Length = sizeof(oa);
    oa.RootDirectory = root;
    oa.ObjectName = &name;
    oa.Attributes = OBJ_CASE_INSENSITIVE;
    oa.SecurityDescriptor = 0;
    oa.SecurityQualityOfService = 0;

    HANDLE h;
    IO_STATUS_BLOCK iosb;
    NTSTATUS s = NtCreateFile(
        &h, access | SYNCHRONIZE, &oa, &iosb, 0,
        FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ, disposition,
        FILE_SYNCHRONOUS_IO_NONALERT | options, 0, 0
    );
    if (ntstatus_failed(s)) return nterror(s);
    *out = h;
    return 0;
}

static Error nt_open_w(const u16* w, size_t wn, u32 access, u32 disposition, u32 options, HANDLE* out)
{
    ArenaPosition pos = arena_position(&global_arena);
    u16* name; size_t name_len; HANDLE root;
    resolve_nt_path_w(w, wn, &name, &name_len, &root);
    Error e = nt_create(name, name_len, root, access, disposition, options, out);
    arena_reset(&global_arena, pos);
    return e;
}

static Error nt_open_a(const char* path, size_t path_len, u32 access, u32 disposition, u32 options, HANDLE* out)
{
    ArenaPosition pos = arena_position(&global_arena);
    u16* name; size_t name_len; HANDLE root;
    resolve_nt_path_a(path, path_len, &name, &name_len, &root);
    Error e = nt_create(name, name_len, root, access, disposition, options, out);
    arena_reset(&global_arena, pos);
    return e;
}

void assert_native_path(const FilenameChar* path, size_t path_len)
{
    // no requirements
    (void)path;
    (void)path_len;
}

Error dir_create_z(const char* path, size_t path_len)
{
    ASSERT(path[path_len] == 0);
    return dir_create_span(path, path_len);
}
Error dir_create_span(const char* path, size_t path_len)
{
    HANDLE h;
    // FILE_OPEN_IF: open if the directory exists, create it otherwise -- idempotent like mkdir -p's leaves.
    Error e = nt_open_a(path, path_len, GENERIC_WRITE, FILE_OPEN_IF, FILE_DIRECTORY_FILE, &h);
    if (e) return e;
    MUST(error_from_nt(NtClose(h)));
    return 0;
}

Error file_create_z(const char* path, size_t path_len, u32 flags, File* out)
{
    ASSERT(path[path_len] == 0);
    return file_create_span(path, path_len, flags, out);
}
Error file_create_span(const char* path, size_t path_len, u32 flags, File* out)
{
    (void)flags;
    return nt_open_a(
        path, path_len,
        GENERIC_WRITE, FILE_OVERWRITE_IF, FILE_NON_DIRECTORY_FILE,
        &out->handle
    );
}
Error file_create_native(const FilenameChar* path, size_t path_len, u32 flags, File* out)
{
    (void)flags;
    return nt_open_w(
        path, path_len,
        GENERIC_WRITE, FILE_OVERWRITE_IF, FILE_NON_DIRECTORY_FILE,
        &out->handle
    );
}


Error file_open_z(const char* path, size_t path_len, File* out)
{
    ASSERT(path[path_len] == 0);
    return file_open_span(path, path_len, out);
}
Error file_open_span(const char* path, size_t path_len, File* out)
{
    return nt_open_a(
        path, path_len,
        GENERIC_READ, FILE_OPEN, FILE_NON_DIRECTORY_FILE,
        &out->handle
    );
}
Error file_open_native(const FilenameChar* path, size_t path_len, File* out)
{
    return nt_open_w(
        path, path_len,
        GENERIC_READ, FILE_OPEN, FILE_NON_DIRECTORY_FILE,
        &out->handle
    );
}

typedef struct {
    i64 AllocationSize;
    i64 EndOfFile;
    u32 NumberOfLinks;
    u8 DeletePending;
    u8 Directory;
} FILE_STANDARD_INFORMATION;

NTSTATUS NtQueryInformationFile(HANDLE FileHandle, IO_STATUS_BLOCK* IoStatusBlock,
                                void* FileInformation, u32 Length, int FileInformationClass);

Error file_size(File file, size_t* out_size)
{
    FILE_STANDARD_INFORMATION info;
    IO_STATUS_BLOCK iosb;
    NTSTATUS s = NtQueryInformationFile(file.handle, &iosb, &info, sizeof(info), 5);   // FileStandardInformation
    if (ntstatus_failed(s)) return nterror(s);
    *out_size = (size_t)info.EndOfFile;
    return 0;
}

Error file_cwd(u8* out, size_t cap, size_t* out_len)
{
    UNICODE_STRING* dp = &peb()->ProcessParameters->CurrentDirectory.DosPath;   // e.g. "C:\dir\" (WTF-16)
    const u16* cwd = dp->ptr;
    size_t n = dp->size / 2;
    while (n > 0 && (cwd[n - 1] == '\\' || cwd[n - 1] == '/')) n -= 1;          // drop the trailing separator
    if (n * 3 > cap) return nterror((NTSTATUS)0xC0000023u);                     // STATUS_BUFFER_TOO_SMALL
    *out_len = utf8_from_wtf16(cwd, n, out);
    return 0;
}

Error file_write(File file, const u8* data, size_t len, size_t* out_written)
{
    u32 chunk = len > 0xffffffff ? 0xffffffff : (u32)len;   // NtWriteFile Length is a u32
    u32 written;
    NTSTATUS s = nt_write_file(file.handle, data, chunk, &written);
    if (ntstatus_failed(s)) return nterror(s);
    *out_written = written;
    return 0;
}

void file_close(File file)
{
    NTSTATUS s = NtClose(file.handle);
    if (ntstatus_failed(s)) MUST(nterror(s));
}

static u32 strlen32(const char* s)
{
    u32 len = 0;
    while (s[len]) {
        len++;
        if (len == 0) break;
    }
    return len;
}

void log_put(const char* string)
{
    HANDLE stderr = 0;
    for (;;) {
        u32 len = strlen32(string);
        if (len == 0) break;
        u32 last_written;
        if (!stderr) stderr = peb()->ProcessParameters->StandardError;
        NTSTATUS status = nt_write_file(stderr, string, len, &last_written);
        if (status) {
            // TODO: log the status and error, then abort
            RtlExitUserProcess(status);
        }
        string += last_written;
    }
}

static Error drain_to_handle(Sink* w, HANDLE handle, const IovConst* iov, size_t iov_count, size_t *iov_written)
{
    if (w->end) {
        size_t written = 0;
        do {
            u32 write_len = (w->end > 0xffffffff) ? 0xffffffff : w->end;
            u32 last_written;
            NTSTATUS status = nt_write_file(handle, w->buffer, write_len, &last_written);
            if (status) return status;
            written += last_written;
        } while (written < w->end);
        w->end = 0;
    }

    *iov_written = 0;
    for (size_t i = 0; i < iov_count; i++) {
        const u8* p = (const u8*)iov[i].ptr;
        size_t remaining = iov[i].len;
        while (remaining) {
            u32 write_len = (remaining > 0xffffffff) ? 0xffffffff : (u32)remaining;
            u32 last_written;
            NTSTATUS status = nt_write_file(handle, p, write_len, &last_written);
            if (status) return status;
            p += last_written;
            remaining -= last_written;
        }
        *iov_written += iov[i].len;
    }
    return 0;
}
static Error drain_stdout(Sink* w, const IovConst* iov, size_t iov_count, size_t *iov_written)
{
    return drain_to_handle(w, peb()->ProcessParameters->StandardOutput, iov, iov_count, iov_written);
}
static Error drain_stderr(Sink* w, const IovConst* iov, size_t iov_count, size_t *iov_written)
{
    return drain_to_handle(w, peb()->ProcessParameters->StandardError, iov, iov_count, iov_written);
}
static Error drain_file(Sink* w, const IovConst* iov, size_t iov_count, size_t *iov_written)
{
    FileSink* fw = (FileSink*)(((char*)w) - libc_offsetof(FileSink, base));
    return drain_to_handle(w, fw->file.handle, iov, iov_count, iov_written);
}
const SinkVtable stdout_vtable = { drain_stdout };
const SinkVtable stderr_vtable = { drain_stderr };
const SinkVtable file_vtable = { drain_file };

Error file_read(File file, u8* buf, size_t size, size_t* out_size)
{
    IO_STATUS_BLOCK iosb;
    iosb.Information = 0;
    NTSTATUS status = NtReadFile(
        file.handle,
        0,     // No event
        0,     // No APC
        0,     // No APC context
        &iosb,
        buf,
        (size > 0xffffffff) ? 0xffffffff: (u32)size,
        0,     // No offset (auto-advance)
        0      // No key
    );
    if (status == STATUS_PENDING) {
        // async handle: block until the read completes, real status lands in the iosb
        NtWaitForSingleObject(file.handle, 0, 0);
        status = iosb.u.Status;
    }
    if (status == (NTSTATUS)STATUS_END_OF_FILE || status == (NTSTATUS)STATUS_PIPE_BROKEN) {
        *out_size = 0;   // EOF: nothing more to read, not an error
        return 0;
    }
    if (ntstatus_failed(status)) {
        *out_size = 0;
        return nterror(status);
    }
    *out_size = (size_t)iosb.Information;
    return 0;
}
