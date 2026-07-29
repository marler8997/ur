#include "seglist.h"

#include "abortmacros.h"
#include "alloc_stats_enabled.h"
#include "clzll.h"
#include "mem.h"
#include "segment.h"

void alloc_stats_seg_list_init(const char* site, size_t elem_size);
void alloc_stats_seg_list_append(const char* site, size_t new_count);
void alloc_stats_seg_list_grow(const char* site);    // a value segment was added
void alloc_stats_seg_list_ref(const char* site);
void alloc_stats_seg_list_deinit(const char* site);

// largest k with POW2(k) <= v  (floor log2)
static u8 log2_floor(size_t v)
{
    return v <= 1 ? 0 : (u8)(63 - clzll((unsigned long long)v));
}

SegList seg_list_init(size_t elem_size, const char* site)
{
    SegList sl;
    sl.segs = 0;
    sl.seg_count = 0;
    sl.dir_cap = 0;
    sl.count = 0;
    sl.elem_size = elem_size;

    size_t per_page = segment_size(0) / elem_size;   // elements that fit one (single-page) segment
    if (per_page < 1) per_page = 1;                   // a huge element gets one element per segment
    sl.shift = log2_floor(per_page);
    sl.per_seg = (size_t)1 << sl.shift;
    sl.seg_class = segment_class(sl.per_seg * elem_size);
    sl.site = site;
    if (alloc_stats_enabled) alloc_stats_seg_list_init(site, elem_size);
    return sl;
}

// the value segment holding element `index`
static void* seg_for(const SegList* sl, size_t index)
{
    return (sl->seg_count > 1) ? ((void**)sl->segs)[index >> sl->shift] : sl->segs;
}

static void seg_list_add_segment(SegList* sl)
{
    void* nseg = segment_reserve(sl->seg_class);
    if (sl->seg_count == 0) {
        sl->segs = nseg;                                   // first segment: no directory yet
    } else if (sl->seg_count == 1) {
        void* lone = sl->segs;                             // promote: move the lone segment into a directory
        SegmentClass dc = segment_class(2 * sizeof(void*));
        void** dir = segment_reserve(dc);
        dir[0] = lone;
        dir[1] = nseg;
        sl->segs = dir;
        sl->dir_cap = segment_size(dc) / sizeof(void*);
    } else {
        void** dir = sl->segs;
        if (sl->seg_count == sl->dir_cap) {                // directory full: grow it (copies pointers, not elements)
            SegmentClass nc = segment_class((sl->dir_cap * 2) * sizeof(void*));
            void** nd = segment_reserve(nc);
            libc_memcpy(nd, dir, sl->seg_count * sizeof(void*));
            segment_release(segment_class(sl->dir_cap * sizeof(void*)), dir);
            dir = nd;
            sl->segs = nd;
            sl->dir_cap = segment_size(nc) / sizeof(void*);
        }
        dir[sl->seg_count] = nseg;
    }
    sl->seg_count += 1;
    if (alloc_stats_enabled) alloc_stats_seg_list_grow(sl->site);
}

void* seg_list_append(SegList* sl, const void* elem, size_t elem_size)
{
    ASSERT(elem_size == sl->elem_size);
    if (sl->count == sl->seg_count * sl->per_seg) seg_list_add_segment(sl);
    size_t i = sl->count;
    void* slot = (u8*)seg_for(sl, i) + (i & (sl->per_seg - 1)) * sl->elem_size;
    libc_memcpy(slot, elem, elem_size);
    sl->count += 1;
    if (alloc_stats_enabled) alloc_stats_seg_list_append(sl->site, sl->count);
    return slot;
}

void* seg_list_ref(const SegList* sl, size_t index, size_t elem_size)
{
    ASSERT(elem_size == sl->elem_size);
    ASSERT(index < sl->count);
    if (alloc_stats_enabled) alloc_stats_seg_list_ref(sl->site);
    return (u8*)seg_for(sl, index) + (index & (sl->per_seg - 1)) * sl->elem_size;
}

void seg_list_deinit(SegList* sl)
{
    if (alloc_stats_enabled) alloc_stats_seg_list_deinit(sl->site);
    if (sl->seg_count == 1) {
        segment_release(sl->seg_class, sl->segs);
    } else if (sl->seg_count > 1) {
        void** dir = sl->segs;
        for (size_t i = 0; i < sl->seg_count; i++) segment_release(sl->seg_class, dir[i]);
        segment_release(segment_class(sl->dir_cap * sizeof(void*)), dir);
    }
    sl->segs = 0;
    sl->seg_count = 0;
    sl->dir_cap = 0;
    sl->count = 0;
}
