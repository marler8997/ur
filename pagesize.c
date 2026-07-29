#include "pagesize.h"

#include "abortmacros.h"
#include "filesink.h"
#include "processexit.h"

#if defined(_WIN32)

#include "windows-ntstatus.h"

#if defined(_MSC_VER)
    #pragma comment(lib, "ntdll")
#endif

#define SystemBasicInformation 0
typedef struct {
    u32  Reserved;
    u32  TimerResolution;
    u32  PageSize;
    u32  NumberOfPhysicalPages;
    u32  LowestPhysicalPageNumber;
    u32  HighestPhysicalPageNumber;
    u32  AllocationGranularity;
    size_t MinimumUserModeAddress;
    size_t MaximumUserModeAddress;
    size_t ActiveProcessorsAffinityMask;
    char   NumberOfProcessors;
} SYSTEM_BASIC_INFORMATION;
NTSTATUS NtQuerySystemInformation(
    u32 SystemInformationClass,
    void* SystemInformation,
    u32 SystemInformationLength,
    u32* ReturnLength
);

size_t query_page_size(void)
{
    SYSTEM_BASIC_INFORMATION info;
    NTSTATUS status = NtQuerySystemInformation(SystemBasicInformation, &info, sizeof(info), 0);
    if (ntstatus_failed(status)) {
        char stderr_buf[1000];
        Sink stderr = sink_init(&stderr_vtable, stderr_buf, sizeof(stderr_buf));
        MUST(SINK_LITERAL(&stderr, "NtQuerySystemInformation failed, status="));
        MUST(sink_format_i64(&stderr, status));
        MUST(SINK_LITERAL(&stderr, "\n"));
        MUST(sink_flush(&stderr));
        process_exit(status);
    }
    if (info.PageSize < PAGE_SIZE_MIN) {
        char stderr_buf[1000];
        Sink stderr = sink_init(&stderr_vtable, stderr_buf, sizeof(stderr_buf));
        MUST(SINK_LITERAL(&stderr, "page size < min: "));
        MUST(sink_format_u64(&stderr, info.PageSize));
        MUST(SINK_LITERAL(&stderr, "\n"));
        MUST(sink_flush(&stderr));
        process_exit(status);
    }
    return info.PageSize;
}

#elif defined(__linux__)
    //
#else
    size_t query_page_size(void);
#endif

size_t page_size(void)
{
#if defined(__linux__)
    return PAGE_SIZE_LINUX;
#else
    static size_t static_page_size = 0;
    if (!static_page_size) {
        static_page_size = query_page_size();
        ASSERT(static_page_size);
    }
    return static_page_size;
#endif
}
