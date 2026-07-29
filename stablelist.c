#include "stablelist.h"

#include "abortmacros.h"
#include "alloc_stats_enabled.h"
#include "mem.h"
#include "segment.h"

void alloc_stats_stablelist_init(const char* site, size_t elem_size);
void alloc_stats_stablelist_append(const char* site, size_t new_count);
void alloc_stats_stablelist_grow(const char* site);     // a segment was added
void alloc_stats_stablelist_deinit(const char* site);

struct struct_StableListSeg {
    StableListSeg* next;
    // packed elements follow the header in the same segment
};

static u8* seg_data(StableListSeg* s) { return (u8*)(s + 1); }

StableList stablelist_init(size_t elem_size, const char* site)
{
    StableList sl;
    sl.head = 0;
    sl.tail = 0;
    sl.tail_count = 0;
    sl.count = 0;
    sl.elem_size = elem_size;

    // pack as many elements as fit a single-page segment after the header; an element larger than
    // that gets a segment sized to hold exactly one.
    size_t usable = segment_size(0) - sizeof(StableListSeg);
    if (usable / elem_size >= 1) {
        sl.per_seg = usable / elem_size;
        sl.seg_class = 0;
    } else {
        sl.per_seg = 1;
        sl.seg_class = segment_class(sizeof(StableListSeg) + elem_size);
    }
    sl.site = site;
    if (alloc_stats_enabled) alloc_stats_stablelist_init(site, elem_size);
    return sl;
}

void* stablelist_append(StableList* sl, const void* elem, size_t elem_size)
{
    ASSERT(elem_size == sl->elem_size);
    if (!sl->tail || sl->tail_count == sl->per_seg) {
        StableListSeg* s = segment_reserve(sl->seg_class);
        s->next = 0;
        if (sl->tail) sl->tail->next = s; else sl->head = s;
        sl->tail = s;
        sl->tail_count = 0;
        if (alloc_stats_enabled) alloc_stats_stablelist_grow(sl->site);
    }
    void* slot = seg_data(sl->tail) + sl->tail_count * sl->elem_size;
    libc_memcpy(slot, elem, elem_size);
    sl->tail_count += 1;
    sl->count += 1;
    if (alloc_stats_enabled) alloc_stats_stablelist_append(sl->site, sl->count);
    return slot;
}

void stablelist_deinit(StableList* sl)
{
    if (alloc_stats_enabled) alloc_stats_stablelist_deinit(sl->site);
    StableListSeg* s = sl->head;
    while (s) {
        StableListSeg* next = s->next;
        segment_release(sl->seg_class, s);
        s = next;
    }
    sl->head = 0;
    sl->tail = 0;
    sl->tail_count = 0;
    sl->count = 0;
}

StableIter stablelist_iter(const StableList* sl)
{
    StableIter it;
    it.sl = sl;
    it.seg = sl->head;
    it.in_seg = 0;
    it.elem = sl->count ? seg_data(sl->head) : 0;
    return it;
}

void stablelist_iter_next(StableIter* it)
{
    it->in_seg += 1;
    size_t seg_n = (it->seg == it->sl->tail) ? it->sl->tail_count : it->sl->per_seg;
    if (it->in_seg < seg_n) {
        it->elem = (u8*)it->elem + it->sl->elem_size;
    } else {
        it->seg = it->seg->next;
        it->in_seg = 0;
        it->elem = it->seg ? seg_data(it->seg) : 0;
    }
}
