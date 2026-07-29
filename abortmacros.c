#include "abortmacros.h"

#include "filesink.h"
#include "mem.h"
#include "processexit.h"

void unreachable(const char* file, unsigned line)
{
    {
        char stderr_buf[300];
        Sink stderr = sink_init(&stderr_vtable, stderr_buf, sizeof(stderr_buf));
        sink_put(&stderr, file, libc_strlen(file));
        SINK_LITERAL(&stderr, ":");
        sink_format_u64(&stderr, line);
        SINK_LITERAL(&stderr, ": UNREACHABLE");
        SINK_LITERAL(&stderr, "\n");
        sink_flush(&stderr);
    }
    process_exit(0xff);
}

void assert_failed(const char* file, unsigned line)
{
    {
        char stderr_buf[300];
        Sink stderr = sink_init(&stderr_vtable, stderr_buf, sizeof(stderr_buf));
        sink_put(&stderr, file, libc_strlen(file));
        SINK_LITERAL(&stderr, ":");
        sink_format_u64(&stderr, line);
        SINK_LITERAL(&stderr, ": ASSERT");
        SINK_LITERAL(&stderr, "\n");
        sink_flush(&stderr);
    }
    process_exit(0xff);
}

void must_failed(Error error, const char* file, unsigned line)
{
    {
        char stderr_buf[300];
        Sink stderr = sink_init(&stderr_vtable, stderr_buf, sizeof(stderr_buf));
        sink_put(&stderr, file, libc_strlen(file));
        SINK_LITERAL(&stderr, ":");
        sink_format_u64(&stderr, line);
        SINK_LITERAL(&stderr, ": MUST: ");
        sink_format_u64(&stderr, error);
        SINK_LITERAL(&stderr, "\n");
        sink_flush(&stderr);
    }
    process_exit(0xff);
}
