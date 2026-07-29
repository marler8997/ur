#include "segment.h"

#include "abortmacros.h"
#include "clzll.h"
#include "pages.h"
#include "pagesize.h"

#define POW2(v) (((size_t)1) << (v))
#define SEGMENT_CLASS_MAX 32     // up to 2^31 pages; far beyond any real allocation
#define PAGE_CACHE_MAX    32     // single-page segments cached for reuse; the rest go to the OS

// smallest k such that POW2(k) >= v  (ceil log2)
static u8 log2_ceil(size_t v)
{
    return v <= 1 ? 0 : (u8)(64 - clzll((unsigned long long)(v - 1)));
}

SegmentClass segment_class(size_t bytes)
{
    size_t page = page_size();
    return log2_ceil((bytes + page - 1) / page);   // class = ceil log2 of the page count
}

size_t segment_size(SegmentClass c)
{
    return POW2(c) * page_size();
}

// class 0 (a single page) is the hot case. A compile keeps the whole program's IR resident, so
// nothing is released mid-compile and a per-segment VirtualAlloc would mean one syscall per SegList
// segment (≈one per function — thousands on a large TU). Instead, bump-allocate single pages from a
// geometrically-growing slab (one reservation per slab), and recycle released pages through a small
// cache. Larger classes are rare: reserved/released straight from/to the OS.
#define SLAB_PAGES_INIT 16
#define SLAB_PAGES_MAX  8192

static void*  g_page_cache[PAGE_CACHE_MAX];
static size_t g_page_cache_count;

static u8*    g_slab;          // current bump slab (older, exhausted slabs stay live, freed at exit)
static size_t g_slab_used;     // pages handed out from g_slab
static size_t g_slab_pages;    // g_slab's capacity in pages

void* segment_reserve(SegmentClass c)
{
    ASSERT(c < SEGMENT_CLASS_MAX);
    if (c == 0) {
        if (g_page_cache_count) return g_page_cache[--g_page_cache_count];
        if (!g_slab || g_slab_used == g_slab_pages) {
            g_slab_pages = g_slab_pages == 0 ? SLAB_PAGES_INIT
                         : g_slab_pages < SLAB_PAGES_MAX ? g_slab_pages * 2 : SLAB_PAGES_MAX;
            MUST(pages_alloc_fixed((void**)&g_slab, g_slab_pages * page_size()));
            g_slab_used = 0;
        }
        return g_slab + (g_slab_used++) * page_size();
    }
    void* p;
    MUST(pages_alloc_fixed(&p, segment_size(c)));
    return p;
}

void segment_release(SegmentClass c, void* p)
{
    ASSERT(c < SEGMENT_CLASS_MAX);
    if (c == 0) {
        if (g_page_cache_count < PAGE_CACHE_MAX) g_page_cache[g_page_cache_count++] = p;
        return;   // a slab interior page can't be freed individually; reclaimed in bulk at process exit
    }
    pages_free_fixed(p, segment_size(c));
}
