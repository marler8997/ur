#include "alloc_stats_enabled.h"

#include "abortmacros.h"
#include "bool.h"
#include "arena.h"
#include "filesink.h"
#include "mem.h"
#include "seglist.h"
#include "sink.h"
#include "stablelist.h"

// Allocation statistics, gathered at runtime when --alloc-stats sets this flag. Site-keyed tables:
// page-level (reserve/commit, from the substrate) and element-level (arena/stablelist/seglist).
// Keyed by the "file:line" string PAGES_INIT()/*_LIST_INIT() inject, merged by string value.
Bool alloc_stats_enabled = 0;

#define SITE_MAX_PAGE 256
#define SITE_MAX_ARENA 64
#define SITE_MAX_STABLELIST 256
#define SITE_MAX_SEGLIST 256

static Bool site_eq(const char* a, const char* b)
{
    if (a == b) return 1;
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

// ---- page-level table ---------------------------------------------------------

typedef struct {
    const char* site;
    u64 reserves;
    u64 releases;
    u64 live;         // currently-live reservations (reserves - releases)
    u64 peak_live;    // max ever simultaneously live
    u64 reserved_bytes;
    u64 commits;
    u64 committed_bytes;
} PageStat;
static PageStat g_pages[SITE_MAX_PAGE];
static size_t g_page_count;

static PageStat* page_find(const char* site)
{
    for (size_t i = 0; i < g_page_count; i++) {
        if (site_eq(g_pages[i].site, site)) return &g_pages[i];
    }
    ASSERT(g_page_count < SITE_MAX_PAGE);
    PageStat* s = &g_pages[g_page_count++];
    s->site = site;
    s->reserves = 0;
    s->releases = 0;
    s->live = 0;
    s->peak_live = 0;
    s->reserved_bytes = 0;
    s->commits = 0;
    s->committed_bytes = 0;
    return s;
}

void alloc_stats_reserve(const char* site, size_t bytes)
{
    PageStat* s = page_find(site);
    s->reserves += 1;
    s->reserved_bytes += bytes;
    s->live += 1;
    if (s->live > s->peak_live) s->peak_live = s->live;
}

void alloc_stats_commit(const char* site, size_t bytes)
{
    PageStat* s = page_find(site);
    s->commits += 1;
    s->committed_bytes += bytes;
}

void alloc_stats_release(const char* site)
{
    PageStat* s = page_find(site);
    s->releases += 1;
    if (s->live) s->live -= 1;
}

// ---- element-level table ------------------------------------------------------

// ---- arena table --------------------------------------------------------------

typedef struct {
    const char* site;     // the ARENA_ALLOC call site
    u64 allocs;           // number of allocations at this site
    u64 total_bytes;      // total bytes requested at this site
    u64 peak_bytes;       // largest single allocation at this site
} ArenaStat;
static ArenaStat g_arenas[SITE_MAX_ARENA];
static size_t g_arena_count;

static ArenaStat* arena_find(const char* site)
{
    for (size_t i = 0; i < g_arena_count; i++) {
        if (site_eq(g_arenas[i].site, site)) return &g_arenas[i];
    }
    ASSERT(g_arena_count < SITE_MAX_ARENA);
    ArenaStat* s = &g_arenas[g_arena_count++];
    s->site = site;
    s->allocs = 0;
    s->total_bytes = 0;
    s->peak_bytes = 0;
    return s;
}

void alloc_stats_arena_alloc(const char* site, size_t bytes)
{
    ArenaStat* s = arena_find(site);
    s->allocs += 1;
    s->total_bytes += bytes;
    if (bytes > s->peak_bytes) s->peak_bytes = bytes;
}

// ---- stablelist table ---------------------------------------------------------

typedef struct {
    const char* site;
    size_t elem_size;
    u64 instances;
    u64 frees;
    u64 live;
    u64 peak_live;
    u64 appends;
    u64 segments;     // segments added across all instances at this site
    u64 peak_count;   // most elements a single list at this site ever held
} StableListStat;
static StableListStat g_stablelists[SITE_MAX_STABLELIST];
static size_t g_stablelist_count;

static StableListStat* stablelist_find(const char* site)
{
    for (size_t i = 0; i < g_stablelist_count; i++) {
        if (site_eq(g_stablelists[i].site, site)) return &g_stablelists[i];
    }
    ASSERT(g_stablelist_count < SITE_MAX_STABLELIST);
    StableListStat* s = &g_stablelists[g_stablelist_count++];
    s->site = site;
    s->elem_size = 0;
    s->instances = 0;
    s->frees = 0;
    s->live = 0;
    s->peak_live = 0;
    s->appends = 0;
    s->segments = 0;
    s->peak_count = 0;
    return s;
}

void alloc_stats_stablelist_init(const char* site, size_t elem_size)
{
    StableListStat* s = stablelist_find(site);
    s->elem_size = elem_size;
    s->instances += 1;
    s->live += 1;
    if (s->live > s->peak_live) s->peak_live = s->live;
}

void alloc_stats_stablelist_deinit(const char* site)
{
    StableListStat* s = stablelist_find(site);
    s->frees += 1;
    if (s->live) s->live -= 1;
}

void alloc_stats_stablelist_append(const char* site, size_t new_count)
{
    StableListStat* s = stablelist_find(site);
    s->appends += 1;
    if (new_count > s->peak_count) s->peak_count = new_count;
}

void alloc_stats_stablelist_grow(const char* site)
{
    stablelist_find(site)->segments += 1;
}

// ---- seglist table ------------------------------------------------------------

typedef struct {
    const char* site;
    size_t elem_size;
    u64 instances;
    u64 frees;
    u64 live;
    u64 peak_live;
    u64 appends;
    u64 segments;     // value segments added across all instances at this site
    u64 refs;         // total seg_list_ref calls
    u64 peak_count;
} SegListStat;
static SegListStat g_seglists[SITE_MAX_SEGLIST];
static size_t g_seglist_count;

static SegListStat* seglist_find(const char* site)
{
    for (size_t i = 0; i < g_seglist_count; i++) {
        if (site_eq(g_seglists[i].site, site)) return &g_seglists[i];
    }
    ASSERT(g_seglist_count < SITE_MAX_SEGLIST);
    SegListStat* s = &g_seglists[g_seglist_count++];
    s->site = site;
    s->elem_size = 0;
    s->instances = 0;
    s->frees = 0;
    s->live = 0;
    s->peak_live = 0;
    s->appends = 0;
    s->segments = 0;
    s->refs = 0;
    s->peak_count = 0;
    return s;
}

void alloc_stats_seg_list_init(const char* site, size_t elem_size)
{
    SegListStat* s = seglist_find(site);
    s->elem_size = elem_size;
    s->instances += 1;
    s->live += 1;
    if (s->live > s->peak_live) s->peak_live = s->live;
}

void alloc_stats_seg_list_deinit(const char* site)
{
    SegListStat* s = seglist_find(site);
    s->frees += 1;
    if (s->live) s->live -= 1;
}

void alloc_stats_seg_list_append(const char* site, size_t new_count)
{
    SegListStat* s = seglist_find(site);
    s->appends += 1;
    if (new_count > s->peak_count) s->peak_count = new_count;
}

void alloc_stats_seg_list_grow(const char* site)
{
    seglist_find(site)->segments += 1;
}

void alloc_stats_seg_list_ref(const char* site)
{
    seglist_find(site)->refs += 1;
}

// ---- dump ---------------------------------------------------------------------

static void page_row(Sink* w, const char* site, const PageStat* s)
{
    MUST(sink_put(w, site, libc_strlen(site)));
    MUST(SINK_LITERAL(w, "\treserves="));
    MUST(sink_format_u64(w, s->reserves));
    MUST(SINK_LITERAL(w, "\treleases="));
    MUST(sink_format_u64(w, s->releases));
    MUST(SINK_LITERAL(w, "\tpeaklive="));
    MUST(sink_format_u64(w, s->peak_live));
    MUST(SINK_LITERAL(w, "\treserved="));
    MUST(sink_format_u64(w, s->reserved_bytes >> 20));
    MUST(SINK_LITERAL(w, "MiB\tcommitted="));
    MUST(sink_format_u64(w, s->committed_bytes >> 10));
    MUST(SINK_LITERAL(w, "KiB\n"));
    MUST(sink_flush(w));   // per-row flush: the buffer never overflows mid-dump
}

static void arena_row(Sink* w, const ArenaStat* s)
{
    MUST(sink_put(w, s->site, libc_strlen(s->site)));
    MUST(SINK_LITERAL(w, "\tallocs="));
    MUST(sink_format_u64(w, s->allocs));
    MUST(SINK_LITERAL(w, "\ttotal="));
    MUST(sink_format_u64(w, s->total_bytes));
    MUST(SINK_LITERAL(w, "B\tpeak="));
    MUST(sink_format_u64(w, s->peak_bytes));
    MUST(SINK_LITERAL(w, "B\n"));
    MUST(sink_flush(w));
}

static void stablelist_row(Sink* w, const StableListStat* s)
{
    MUST(sink_put(w, s->site, libc_strlen(s->site)));
    MUST(SINK_LITERAL(w, "\telem="));
    MUST(sink_format_u64(w, s->elem_size));
    MUST(SINK_LITERAL(w, "\tinstances="));
    MUST(sink_format_u64(w, s->instances));
    MUST(SINK_LITERAL(w, "\tfrees="));
    MUST(sink_format_u64(w, s->frees));
    MUST(SINK_LITERAL(w, "\tpeaklive="));
    MUST(sink_format_u64(w, s->peak_live));
    MUST(SINK_LITERAL(w, "\tpeakcount="));
    MUST(sink_format_u64(w, s->peak_count));
    MUST(SINK_LITERAL(w, "\tappends="));
    MUST(sink_format_u64(w, s->appends));
    MUST(SINK_LITERAL(w, "\tsegments="));
    MUST(sink_format_u64(w, s->segments));
    MUST(SINK_LITERAL(w, "\n"));
    MUST(sink_flush(w));
}

static void seglist_row(Sink* w, const SegListStat* s)
{
    MUST(sink_put(w, s->site, libc_strlen(s->site)));
    MUST(SINK_LITERAL(w, "\telem="));
    MUST(sink_format_u64(w, s->elem_size));
    MUST(SINK_LITERAL(w, "\tinstances="));
    MUST(sink_format_u64(w, s->instances));
    MUST(SINK_LITERAL(w, "\tfrees="));
    MUST(sink_format_u64(w, s->frees));
    MUST(SINK_LITERAL(w, "\tpeaklive="));
    MUST(sink_format_u64(w, s->peak_live));
    MUST(SINK_LITERAL(w, "\tpeakcount="));
    MUST(sink_format_u64(w, s->peak_count));
    MUST(SINK_LITERAL(w, "\tappends="));
    MUST(sink_format_u64(w, s->appends));
    MUST(SINK_LITERAL(w, "\tsegments="));
    MUST(sink_format_u64(w, s->segments));
    MUST(SINK_LITERAL(w, "\trefs="));
    MUST(sink_format_u64(w, s->refs));
    MUST(SINK_LITERAL(w, "\n"));
    MUST(sink_flush(w));
}

void alloc_stats_dump(void)
{
    if (!alloc_stats_enabled) return;

    char buf[512];
    Sink w = sink_init(&stderr_vtable, buf, sizeof(buf));

    MUST(SINK_LITERAL(&w, "\n=== page stats (per PAGES_INIT site) ===\n"));
    MUST(sink_flush(&w));
    PageStat total = { "TOTAL", 0, 0, 0, 0, 0, 0, 0 };
    for (size_t i = 0; i < g_page_count; i++) {
        PageStat* s = &g_pages[i];
        total.reserves += s->reserves; total.releases += s->releases; total.peak_live += s->peak_live;
        total.reserved_bytes += s->reserved_bytes; total.commits += s->commits; total.committed_bytes += s->committed_bytes;
        page_row(&w, s->site, s);
    }
    page_row(&w, "TOTAL", &total);

    MUST(SINK_LITERAL(&w, "\n=== arena stats (per ARENA_ALLOC site) ===\n"));
    MUST(sink_flush(&w));
    for (size_t i = 0; i < g_arena_count; i++) {
        arena_row(&w, &g_arenas[i]);
    }

    MUST(SINK_LITERAL(&w, "\n=== stablelist stats (per STABLE_LIST_INIT site) ===\n"));
    MUST(sink_flush(&w));
    for (size_t i = 0; i < g_stablelist_count; i++) {
        stablelist_row(&w, &g_stablelists[i]);
    }

    MUST(SINK_LITERAL(&w, "\n=== seglist stats (per SEG_LIST_INIT site) ===\n"));
    MUST(sink_flush(&w));
    for (size_t i = 0; i < g_seglist_count; i++) {
        seglist_row(&w, &g_seglists[i]);
    }
}
