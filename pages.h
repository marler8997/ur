#ifndef _PAGES_H
#define _PAGES_H

#include "error.h"
#include "int.h"
#include "size_t.h"
#include "stringify.h"

Error pages_alloc_fixed(void** out, size_t size);
void  pages_free_fixed(void* ptr, size_t size);

// Pages -- a CONTIGUOUS, MOVABLE, page-aligned, expandable region of memory. It is the strategy
// for the one shape a segmented list can't serve: a single unbroken span you index as a flat
// array, pass as a `u8*`, hand to the OS as a buffer/file image, or memcpy out in one shot (cc's
// exe image, code section, object blob, and preprocessor output).
//
// Growth tries to AVOID COPYING and FALLS BACK TO COPYING only when it must:
//   * A region reserves address space ahead of what it has committed. Growing within that
//     reservation just commits more pages in place -- no copy, base unchanged.
//   * When a grow outgrows the reservation, alloc_pages reserves a fresh, geometrically larger
//     region (the reservation DOUBLES), copies the live bytes over, and releases the old one.
//     This is the only case that copies, and the base MOVES.
//
// Because of that fallback the region is MOVABLE: never hold a raw pointer into a Pages across a
// grow -- hold an offset and re-fetch `.ptr` (this is what `Cursor` does). The reservation is a
// growth floor that doubles, NOT a ceiling: the real limit is whatever VA the OS will give, and a
// grow fails only when the OS refuses. A caller that knows its eventual size can pre-size the
// first reservation with PAGES_INIT_RESERVE(n) to skip the relocate-copies entirely.
//
// vs the non-contiguous lists: when you do NOT need one contiguous span, prefer SegList/StableList
// (no reservation, never copy, never move). See Allocation.md for the full decision table.
typedef struct struct_Pages {
    u8* ptr;          // base of the live region; MOVES when a grow relocates (see above)
    size_t size;      // bytes in use (committed prefix actually written)
    void* handle;     // before the first alloc_pages: the caller's reserve hint; after: the
                      // current reservation cap (commit ceiling before the next relocate)
    const char* site; // "file:line" of the originating PAGES_INIT, for --alloc-stats
} Pages;

static inline Pages pages_init(const char* site, size_t reserve_hint)
{
    Pages pages;
    pages.ptr = 0;
    pages.size = 0;
    pages.handle = (void*)reserve_hint;   // the first alloc_pages reserves at least this much
    pages.site = site;
    return pages;
}

// PAGES_INIT(): start with the default floor and let it grow (and occasionally relocate-copy).
// PAGES_INIT_RESERVE(n): start with n bytes reserved -- use when you know the eventual size, so a
//   buffer that ends up <= n never relocates.
#define PAGES_INIT()           pages_init(__FILE__ ":" STRINGIFY(__LINE__), 0)
#define PAGES_INIT_RESERVE(n)  pages_init(__FILE__ ":" STRINGIFY(__LINE__), (n))

Error alloc_pages(Pages*, size_t new_size);   // grow the committed size to new_size (may relocate)
Error lock_pages(Pages*);                     // finalize: drop the reserved-but-uncommitted tail; no more growth
void  pages_deinit(Pages*);                   // release the whole region

#endif // _PAGES_H
