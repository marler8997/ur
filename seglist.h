#ifndef _SEGLIST_H
#define _SEGLIST_H

#include "int.h"
#include "size_t.h"
#include "stringify.h"

typedef u8 SegmentClass;

// SegList — a typed random-access collection (a directory of fixed-size segments) with three
// properties:
//
//   * O(1) RANDOM access by index: dir[i >> shift][i & mask], two derefs. (This is what sets it
//     apart from StableList, which is append + iterate only.)
//   * NO-COPY growth: growing appends a new segment; existing elements are never moved or copied.
//     Only the directory of segment pointers ever changes, and it holds pointers, not elements.
//   * STABLE element locations: an element never moves once appended, so the pointer that
//     seg_list_append / seg_list_ref returns stays valid for the life of the list.
//
// The directory is lazily allocated as its own segment: while the list fits in a single segment
// (the common case) there is no directory at all — `segs` points straight at that segment. The
// directory appears only when a second segment is needed, and one page of it addresses 512
// segments before it would itself grow (by copying pointers). So there is no fixed capacity, and a
// small list pays no directory overhead. Tradeoffs vs a plain contiguous List: two derefs plus a
// solo-vs-directory branch per access, and a segment holds a power-of-two count so a non-power-of-
// two element size leaves slack. The struct holds a single heap pointer, so it is safe to copy by
// value.
typedef struct struct_SegList {
    void*  segs;             // seg_count <= 1: the lone value segment; seg_count > 1: the directory (void**)
    size_t seg_count;        // value segments in use
    size_t dir_cap;          // directory capacity in pointers (meaningful when seg_count > 1)
    size_t count;            // elements
    size_t elem_size;
    size_t per_seg;          // elements per value segment (power of two)
    u8     shift;            // log2(per_seg)
    SegmentClass seg_class;  // segment class of each value segment
    const char* site;
} SegList;

SegList seg_list_init(size_t elem_size, const char* site);
#define SEG_LIST_INIT(T)  seg_list_init(sizeof(T), __FILE__ ":" STRINGIFY(__LINE__))

void  seg_list_deinit(SegList*);

#define SEG_LIST_APPEND(T, sl, val)  ((T*)seg_list_append((sl), &(val), sizeof(T)))
void* seg_list_append(SegList*, const void* elem, size_t elem_size);   // returns the element's stable address

#define SEG_LIST_REF(T, sl, i)       ((T*)seg_list_ref((sl), (i), sizeof(T)))       // pointer to element i
#define SEG_LIST_VAL(T, sl, i)       (*(T*)seg_list_ref((sl), (i), sizeof(T)))      // element i by value
void* seg_list_ref(const SegList*, size_t index, size_t elem_size);    // O(1) pointer to element `index`

#endif // _SEGLIST_H
