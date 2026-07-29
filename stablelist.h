#ifndef _STABLELIST_H
#define _STABLELIST_H

#include "int.h"
#include "size_t.h"
#include "stringify.h"

typedef u8 SegmentClass;

// StableList — a typed append-and-iterate collection with two defining properties:
//
//   * STABLE element locations: an element never moves once appended, so the pointer that
//     stablelist_append returns stays valid for the life of the list. You may save it and hold it
//     across any number of later appends.
//   * NO-COPY growth: growing the list never copies (or touches) the existing elements — it just
//     links on a new segment.
//
// It is a linked list of fixed-size segments (a sibling of the arena, sharing the `segment` pool):
// appending bumps the tail segment and links a fresh one when it fills. There is no random-index
// access by design — if you need that, use a different (random-access) list. A StableList is safe
// to copy by value: head/tail are heap pointers and the elements live in heap segments.
typedef struct struct_StableListSeg StableListSeg;   // a `next` link followed by packed elements

typedef struct {
    StableListSeg* head;     // first segment (0 until the first append)
    StableListSeg* tail;     // the segment appends go to
    size_t tail_count;       // elements in the tail segment
    size_t count;            // total elements
    size_t elem_size;
    size_t per_seg;          // elements per segment
    SegmentClass seg_class;  // segment class of each segment
    const char* site;
} StableList;

#define STABLE_LIST_INIT(T)  stablelist_init(sizeof(T), __FILE__ ":" STRINGIFY(__LINE__))
StableList stablelist_init(size_t elem_size, const char* site);

#define STABLE_LIST_APPEND(T, sl, val)  ((T*)stablelist_append((sl), &(val), sizeof(T)))
void* stablelist_append(StableList*, const void* elem, size_t elem_size);   // returns the element's stable address
void  stablelist_deinit(StableList*);

// Iteration is the only way to read: one segment hop per `per_seg` elements, contiguous within a
// segment. `it.elem` points at the current element (0 when exhausted).
typedef struct {
    const StableList* sl;
    StableListSeg* seg;
    size_t in_seg;
    void* elem;
} StableIter;

StableIter stablelist_iter(const StableList*);
void    stablelist_iter_next(StableIter*);

#define STABLE_LIST_FOREACH(it, slp)  for (StableIter it = stablelist_iter(slp); it.elem; stablelist_iter_next(&it))

#endif // _STABLELIST_H
