#include "pages.h"

#include "abortmacros.h"
#include "alloc_stats_enabled.h"
#include "mem.h"
#include "pagesize.h"

void alloc_stats_reserve(const char* site, size_t bytes);
void alloc_stats_commit(const char* site, size_t bytes);
void alloc_stats_release(const char* site);

static size_t round_up_page(size_t n)
{
    #if defined(__linux__)
        return (n + PAGE_SIZE_LINUX - 1) & ~((size_t)PAGE_SIZE_LINUX - 1);
    #else
        size_t ps = page_size();
        return (n + ps - 1) & ~(ps - 1);
    #endif
}

////////////////////////////////////////////////////////////////////////////////
#if defined(_WIN32)
////////////////////////////////////////////////////////////////////////////////

#include "windows-ntstatus.h"

#if defined(_MSC_VER)
    #pragma comment(lib, "ntdll")
#endif

typedef void* HANDLE;

NTSTATUS NtAllocateVirtualMemory(
    HANDLE ProcessHandle, void** BaseAddress, size_t ZeroBits,
    size_t* RegionSize, u32 AllocationType, u32 Protect
);
NTSTATUS NtFreeVirtualMemory(
    HANDLE ProcessHandle, void** BaseAddress, size_t* RegionSize, u32 FreeType
);

#define MEM_COMMIT       0x00001000
#define MEM_RESERVE      0x00002000
#define MEM_RELEASE      0x00008000
#define PAGE_READWRITE 0x04

#define RESERVE_FLOOR_BYTES (((size_t)1) << 16)   // 64 KiB (one Windows allocation granule): the
                                                  // smallest starting reservation; it doubles on
                                                  // demand, so it is a floor, not a cap.

Error pages_alloc_fixed(void** out, size_t size)
{
    HANDLE proc = (HANDLE)(i64)-1;
    void* base = 0;
    size_t region = round_up_page(size);
    NTSTATUS s = NtAllocateVirtualMemory(proc, &base, 0, &region, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (ntstatus_failed(s)) return nterror(s);
    *out = base;
    if (alloc_stats_enabled) {
        alloc_stats_reserve("pages_alloc_fixed", region);
        alloc_stats_commit("pages_alloc_fixed", region);
    }
    return 0;
}

void pages_free_fixed(void* ptr, size_t size)
{
    (void)size;
    HANDLE proc = (HANDLE)(i64)-1;
    void* base = ptr;
    size_t region = 0;
    NTSTATUS s = NtFreeVirtualMemory(proc, &base, &region, MEM_RELEASE);
    ASSERT(!ntstatus_failed(s));
}

Error alloc_pages(Pages* pages, size_t new_size)
{
    HANDLE proc = (HANDLE)(i64)-1;
    size_t commit = round_up_page(new_size);
    size_t old_commit = pages->ptr ? round_up_page(pages->size) : 0;

    if (!pages->ptr) {
        size_t cap = (size_t)pages->handle;          // the caller's initial-reserve hint (0 if none)
        if (cap < RESERVE_FLOOR_BYTES) cap = RESERVE_FLOOR_BYTES;
        if (commit > cap) cap = commit;              // honor a larger first request

        void* base = 0;
        size_t reserve = cap;
        NTSTATUS s = NtAllocateVirtualMemory(proc, &base, 0, &reserve, MEM_RESERVE, PAGE_READWRITE);
        if (ntstatus_failed(s)) return nterror(s);

        void* cbase = base;
        size_t creg = commit;
        s = NtAllocateVirtualMemory(proc, &cbase, 0, &creg, MEM_COMMIT, PAGE_READWRITE);
        if (ntstatus_failed(s)) {
            void* fb = base; size_t fz = 0;
            NtFreeVirtualMemory(proc, &fb, &fz, MEM_RELEASE);   // RegionSize 0 -> release whole reservation
            return nterror(s);
        }

        pages->ptr = base;
        pages->handle = (void*)cap;
        pages->size = new_size;
        if (alloc_stats_enabled) {
            alloc_stats_reserve(pages->site, cap);
            alloc_stats_commit(pages->site, commit - old_commit);
        }
        return 0;
    }

    size_t cap = (size_t)pages->handle;
    if (commit > cap) {
        // outgrew the reservation: reserve a fresh, geometrically larger region, copy the live
        // bytes over, and release the old. The base moves (the movable Pages contract).
        size_t new_cap = cap;
        while (new_cap < commit) new_cap *= 2;

        void* nb = 0;
        size_t nr = new_cap;
        NTSTATUS rs = NtAllocateVirtualMemory(proc, &nb, 0, &nr, MEM_RESERVE, PAGE_READWRITE);
        if (ntstatus_failed(rs)) return nterror(rs);
        void* ncb = nb;
        size_t ncr = commit;
        NTSTATUS cs = NtAllocateVirtualMemory(proc, &ncb, 0, &ncr, MEM_COMMIT, PAGE_READWRITE);
        if (ntstatus_failed(cs)) {
            void* fb = nb; size_t fz = 0;
            NtFreeVirtualMemory(proc, &fb, &fz, MEM_RELEASE);
            return nterror(cs);
        }
        libc_memcpy(nb, pages->ptr, pages->size);
        void* ob = pages->ptr; size_t oz = 0;
        NtFreeVirtualMemory(proc, &ob, &oz, MEM_RELEASE);

        pages->ptr = nb;
        pages->handle = (void*)new_cap;
        pages->size = new_size;
        if (alloc_stats_enabled) {
            alloc_stats_release(pages->site);
            alloc_stats_reserve(pages->site, new_cap);
            alloc_stats_commit(pages->site, commit - old_commit);
        }
        return 0;
    }

    // grow within the reservation: commit a longer prefix in place (re-committing live pages is a no-op)
    void* cbase = pages->ptr;
    size_t creg = commit;
    NTSTATUS s = NtAllocateVirtualMemory(proc, &cbase, 0, &creg, MEM_COMMIT, PAGE_READWRITE);
    if (ntstatus_failed(s)) return nterror(s);
    pages->size = new_size;
    if (alloc_stats_enabled) alloc_stats_commit(pages->site, commit - old_commit);
    return 0;
}

Error lock_pages(Pages* pages)
{
    pages->handle = 0;
    return 0;
}

void pages_deinit(Pages* pages)
{
    if (!pages->ptr) return;
    if (alloc_stats_enabled) alloc_stats_release(pages->site);
    HANDLE proc = (HANDLE)(i64)-1;
    void* base = pages->ptr;
    size_t region = 0;   // 0 + MEM_RELEASE releases the whole reservation
    NTSTATUS s = NtFreeVirtualMemory(proc, &base, &region, MEM_RELEASE);
    ASSERT(!ntstatus_failed(s));
    pages->ptr = 0;
    pages->size = 0;
    pages->handle = 0;
}


////////////////////////////////////////////////////////////////////////////////
#elif defined(__linux__)
////////////////////////////////////////////////////////////////////////////////

#include "linux-syscall.h"

#define PROT_NONE  0x0
#define PROT_READ  0x1
#define PROT_WRITE 0x2

#define MAP_PRIVATE   0x02
#define MAP_ANONYMOUS 0x20
#define MAP_FAILED    ((void*)(long)-1)



#define RESERVE_FLOOR_BYTES (((size_t)1) << 16)   // 64 KiB floor; doubles on demand

Error pages_alloc_fixed(void** out, size_t size)
{
    size_t region = round_up_page(size);
    long base = syscall6(SYS_mmap, 0, (long)region, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base < 0) return (int)(-base);
    *out = (void*)base;
    if (alloc_stats_enabled) {
        alloc_stats_reserve("pages_alloc_fixed", region);
        alloc_stats_commit("pages_alloc_fixed", region);
    }
    return 0;
}

void pages_free_fixed(void* ptr, size_t size)
{
    long r = syscall3(SYS_munmap, (long)ptr, (long)round_up_page(size), 0);
    ASSERT(r >= 0);
}

Error alloc_pages(Pages* pages, size_t new_size)
{
    size_t commit = round_up_page(new_size);
    size_t old_commit = pages->ptr ? round_up_page(pages->size) : 0;

    if (!pages->ptr) {
        size_t cap = (size_t)pages->handle;          // caller's initial-reserve hint (0 if none)
        if (cap < RESERVE_FLOOR_BYTES) cap = RESERVE_FLOOR_BYTES;
        if (commit > cap) cap = commit;
        long base = syscall6(SYS_mmap, 0, (long)cap, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (base < 0) return (int)(-base);
        long r = syscall3(SYS_mprotect, base, (long)commit, PROT_READ | PROT_WRITE);
        if (r < 0) {
            syscall3(SYS_munmap, base, (long)cap, 0);
            return (int)(-r);
        }
        pages->ptr = (u8*)base;
        pages->handle = (void*)cap;
        pages->size = new_size;
        if (alloc_stats_enabled) {
            alloc_stats_reserve(pages->site, cap);
            alloc_stats_commit(pages->site, commit - old_commit);
        }
        return 0;
    }

    size_t cap = (size_t)pages->handle;
    if (commit > cap) {
        // outgrew the reservation: reserve a fresh, larger region, copy the live bytes, drop the old
        size_t new_cap = cap;
        while (new_cap < commit) new_cap *= 2;
        long nb = syscall6(SYS_mmap, 0, (long)new_cap, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (nb < 0) return (int)(-nb);
        long r = syscall3(SYS_mprotect, nb, (long)commit, PROT_READ | PROT_WRITE);
        if (r < 0) { syscall3(SYS_munmap, nb, (long)new_cap, 0); return (int)(-r); }
        libc_memcpy((void*)nb, pages->ptr, pages->size);
        syscall3(SYS_munmap, (long)pages->ptr, (long)cap, 0);
        pages->ptr = (u8*)nb;
        pages->handle = (void*)new_cap;
        pages->size = new_size;
        if (alloc_stats_enabled) {
            alloc_stats_release(pages->site);
            alloc_stats_reserve(pages->site, new_cap);
            alloc_stats_commit(pages->site, commit - old_commit);
        }
        return 0;
    }

    long r = syscall3(SYS_mprotect, (long)pages->ptr, (long)commit, PROT_READ | PROT_WRITE);
    if (r < 0) return (int)(-r);
    pages->size = new_size;
    if (alloc_stats_enabled) alloc_stats_commit(pages->site, commit - old_commit);
    return 0;
}

Error lock_pages(Pages* pages)
{
    size_t cap = (size_t)pages->handle;
    size_t committed = round_up_page(pages->size);
    if (cap > committed) {
        long r = syscall3(SYS_munmap, (long)(pages->ptr + committed), (long)(cap - committed), 0);
        if (r < 0) return (int)(-r);
    }
    pages->handle = 0;
    return 0;
}

void pages_deinit(Pages* pages)
{
    if (!pages->ptr) return;
    if (alloc_stats_enabled) alloc_stats_release(pages->site);
    size_t len = pages->handle ? (size_t)pages->handle : round_up_page(pages->size);
    syscall3(SYS_munmap, (long)pages->ptr, (long)len, 0);
    pages->ptr = 0;
    pages->size = 0;
    pages->handle = 0;
}

////////////////////////////////////////////////////////////////////////////////
#else // macos/posix (not windows nor linux)
////////////////////////////////////////////////////////////////////////////////

#include <errno.h>
#include <sys/mman.h>

#define RESERVE_FLOOR_BYTES (((size_t)1) << 16)   // 64 KiB floor; doubles on demand

Error pages_alloc_fixed(void** out, size_t size)
{
    size_t region = round_up_page(size);
    void* base = mmap(0, region, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (base == MAP_FAILED) return errno;
    *out = base;
    if (alloc_stats_enabled) {
        alloc_stats_reserve("pages_alloc_fixed", region);
        alloc_stats_commit("pages_alloc_fixed", region);
    }
    return 0;
}

void pages_free_fixed(void* ptr, size_t size)
{
    ASSERT(munmap(ptr, round_up_page(size)) == 0);
}

Error alloc_pages(Pages* pages, size_t new_size)
{
    size_t commit = round_up_page(new_size);
    size_t old_commit = pages->ptr ? round_up_page(pages->size) : 0;

    if (!pages->ptr) {
        size_t cap = (size_t)pages->handle;          // caller's initial-reserve hint (0 if none)
        if (cap < RESERVE_FLOOR_BYTES) cap = RESERVE_FLOOR_BYTES;
        if (commit > cap) cap = commit;
        void* base = mmap(0, cap, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
        if (base == MAP_FAILED) return errno;
        if (mprotect(base, commit, PROT_READ | PROT_WRITE) != 0) {
            Error e = errno;
            munmap(base, cap);
            return e;
        }
        pages->ptr = base;
        pages->handle = (void*)cap;
        pages->size = new_size;
        if (alloc_stats_enabled) {
            alloc_stats_reserve(pages->site, cap);
            alloc_stats_commit(pages->site, commit - old_commit);
        }
        return 0;
    }

    size_t cap = (size_t)pages->handle;
    if (commit > cap) {
        // outgrew the reservation: reserve a fresh, larger region, copy the live bytes, drop the old
        size_t new_cap = cap;
        while (new_cap < commit) new_cap *= 2;
        void* nb = mmap(0, new_cap, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
        if (nb == MAP_FAILED) return errno;
        if (mprotect(nb, commit, PROT_READ | PROT_WRITE) != 0) { Error e = errno; munmap(nb, new_cap); return e; }
        libc_memcpy(nb, pages->ptr, pages->size);
        munmap(pages->ptr, cap);
        pages->ptr = nb;
        pages->handle = (void*)new_cap;
        pages->size = new_size;
        if (alloc_stats_enabled) {
            alloc_stats_release(pages->site);
            alloc_stats_reserve(pages->site, new_cap);
            alloc_stats_commit(pages->site, commit - old_commit);
        }
        return 0;
    }

    if (mprotect(pages->ptr, commit, PROT_READ | PROT_WRITE) != 0) return errno;
    pages->size = new_size;
    if (alloc_stats_enabled) alloc_stats_commit(pages->site, commit - old_commit);
    return 0;
}

Error lock_pages(Pages* pages)
{
    // Finalize: release the reserved-but-uncommitted tail, keep the live pages.
    size_t cap = (size_t)pages->handle;
    size_t committed = round_up_page(pages->size);
    if (cap > committed) {
        if (munmap(pages->ptr + committed, cap - committed) != 0) return errno;
    }
    pages->handle = 0;
    return 0;
}

void pages_deinit(Pages* pages)
{
    if (!pages->ptr) return;
    if (alloc_stats_enabled) alloc_stats_release(pages->site);
    size_t len = pages->handle ? (size_t)pages->handle : round_up_page(pages->size);
    munmap(pages->ptr, len);
    pages->ptr = 0;
    pages->size = 0;
    pages->handle = 0;
}

////////////////////////////////////////////////////////////////////////////////
#endif
////////////////////////////////////////////////////////////////////////////////
