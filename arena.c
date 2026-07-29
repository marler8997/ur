#include "arena.h"

#include "abortmacros.h"
#include "alloc_stats_enabled.h"
#include "segment.h"

struct struct_ArenaChunk {
    ArenaChunk* next;
    size_t used;
    size_t cap;
    SegmentClass size_class;   // the segment class this chunk came from, for segment_release
    // chunk data follows the header in the same page-aligned allocation
};

static u8* chunk_data(ArenaChunk* c) { return (u8*)(c + 1); }

Arena global_arena;   // zero-initialized: head=current=NULL is a valid empty arena

Arena arena_init(void)
{
    Arena a;
    a.head = 0;
    a.current = 0;
    return a;
}

// reserve a fresh segment big enough for `need`, appended at the tail as a chunk
static ArenaChunk* arena_grow(Arena* a, size_t need)
{
    SegmentClass cls = segment_class(sizeof(ArenaChunk) + need);
    ArenaChunk* c = segment_reserve(cls);
    c->next = 0;
    c->used = 0;
    c->cap = segment_size(cls) - sizeof(ArenaChunk);
    c->size_class = cls;
    if (!a->head) {
        a->head = c;
    } else {
        ArenaChunk* last = a->head;
        while (last->next) last = last->next;
        last->next = c;
    }
    return c;
}

void alloc_stats_arena_alloc(const char* site, size_t bytes);

void* arena_alloc(Arena* a, size_t bytes, const char* site)
{
    size_t need = (bytes + 7) & ~(size_t)7;
    ArenaChunk* c = a->current;
    while (c && c->used + need > c->cap) c = c->next;   // skip full chunks (reuse later ones after a reset)
    if (!c) c = arena_grow(a, need);
    a->current = c;
    void* p = chunk_data(c) + c->used;
    c->used += need;
    if (alloc_stats_enabled) alloc_stats_arena_alloc(site, bytes);
    return p;
}

ArenaPosition arena_position(const Arena* a)
{
    ArenaPosition pos;
    pos.chunk = a->current;
    pos.used = a->current ? a->current->used : 0;
    return pos;
}

void arena_reset(Arena* a, ArenaPosition pos)
{
    if (pos.chunk) {
        Bool in_arena = 0;
        for (ArenaChunk* c = a->head; c; c = c->next) if (c == pos.chunk) { in_arena = 1; break; }
        ASSERT(in_arena && pos.used <= pos.chunk->used);   // a real position not already rewound past
        pos.chunk->used = pos.used;
        for (ArenaChunk* c = pos.chunk->next; c; c = c->next) c->used = 0;
        a->current = pos.chunk;
    } else {
        for (ArenaChunk* c = a->head; c; c = c->next) c->used = 0;
        a->current = a->head;
    }
}

void arena_deinit(Arena* a)
{
    ArenaChunk* c = a->head;
    while (c) {
        ArenaChunk* next = c->next;
        segment_release(c->size_class, c);
        c = next;
    }
    a->head = 0;
    a->current = 0;
}
