#ifndef _ARENA_H
#define _ARENA_H

#include "size_t.h"
#include "stringify.h"

// A bump allocator for short-lived scratch, backed by a linked list of page-aligned chunks (from
// pages_alloc_fixed, NOT a movable Pages). Allocations never move; a single arena_alloc is always
// contiguous, but separate allocs may land in different chunks, so a growable contiguous array is
// not an arena pattern. arena_reset rewinds to a saved position, releasing segments to potentially
// be reused.
typedef struct struct_ArenaChunk ArenaChunk;
typedef struct {
    ArenaChunk* head;
    ArenaChunk* current;
} Arena;

typedef struct {
    ArenaChunk* chunk;
    size_t used;
} ArenaPosition;

Arena arena_init(void);

// The process-wide scratch arena (one per thread if cc ever goes multithreaded). Any code may use
// it, but must be a good citizen: save a position before allocating and reset back to it when done.
extern Arena global_arena;

// Stats are attributed to the ARENA_ALLOC call site, so allocate via ARENA_ALLOC, not arena_alloc.
void*         arena_alloc(Arena*, size_t bytes, const char* site);
#define ARENA_ALLOC(a, bytes)  arena_alloc((a), (bytes), __FILE__ ":" STRINGIFY(__LINE__))
ArenaPosition arena_position(const Arena*);
void          arena_reset(Arena*, ArenaPosition);
void          arena_deinit(Arena*);

#endif // _ARENA_H
