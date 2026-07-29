#ifndef _SEGMENT_H
#define _SEGMENT_H

#include "int.h"
#include "size_t.h"

// A pool of page-aligned segments, segregated by power-of-2 size class — the backing for both the
// arena (a linked list of segments) and the segmented list (a directory of segments). Single-page
// segments, by far the common case, are kept in a small bounded free list for cheap reuse;
// anything larger is rare and goes straight back to the OS on release. The caller tracks its
// segment's class and passes it to both reserve and release, so segments carry no header. Reserved
// memory is uninitialized.
typedef u8 SegmentClass;

SegmentClass segment_class(size_t bytes);   // smallest class whose segment holds `bytes`
size_t       segment_size(SegmentClass);    // the byte size of a segment in this class

void* segment_reserve(SegmentClass);
void  segment_release(SegmentClass, void*);

#endif // _SEGMENT_H
