#include "sink.h"

#include "cli.h"
#include "mem.h"
#include "unicode.h"

Sink sink_init(const SinkVtable* vtable, char* buffer, size_t capacity)
{
    Sink sink;
    sink.vtable = vtable;
    sink.buffer = buffer;
    sink.capacity = capacity;
    sink.end = 0;
    return sink;
}

Error sink_flush(Sink* sink)
{
    while (sink->end) {
        size_t written;
        Error error = sink->vtable->drain(sink, 0, 0, &written);
        if (error) return error;
    }
    return 0;
}


Error sink_put_byte(Sink* sink, u8 byte)
{
    while (sink->capacity - sink->end == 0) {
        // unlikely
        size_t iov_written;
        IovConst iov[1] = { {&byte, 1 } };
        Error error = sink->vtable->drain(sink, iov, 1, &iov_written);
        if (error) return error;
        if (iov_written) return 0;
    }

    // LIKELY
    sink->buffer[sink->end] = byte;
    sink->end += 1;
    return 0;
}

Error sink_put(Sink* sink, const char* ptr, size_t len)
{
    if (sink->end + len <= sink->capacity) {
        // LIKELY
        libc_memcpy(sink->buffer + sink->end, ptr, len);
        sink->end += len;
        return 0; // success
    }
    IovConst iov[1] = { {ptr, len} };
    size_t iov_written;
    return sink->vtable->drain(sink, iov, 1, &iov_written);
}

Error sink_put_wide(Sink* sink, const u16* ptr, size_t len)
{
    size_t i = 0;
    while (i < len) {
        u32 cp = wtf16_decode_next(ptr, len, &i);
        u8 bytes[4];
        int n = utf8_encode(cp, bytes);
        for (int b = 0; b < n; b++) {
            Error err = sink_put_byte(sink, bytes[b]);
            if (err != 0) {
                return err;
            }
        }
    }
    return 0;
}

static u8 format_u64(char* buf, u64 value)
{
    if (value == 0) {
        buf[0] = '0';
        buf[1] = 0;
        return 1;
    }
    char tmp[40];
    u8 n = 0;
    while (value > 0) {
        tmp[n++] = (char)('0' + (value % 10));
        value = value / 10;
    }
    u8 save_n = n;
    int i = 0;
    while (n > 0) {
        n--;
        buf[i++] = tmp[n];      /* reverse: most-significant digit first */
    }
    buf[i] = 0;
    return save_n;
}

static u8 format_i64(char* buf, i64 value)
{
    if (value < 0) {
        buf[0] = '-';
        return format_u64(buf + 1, -(u64)value) + 1;
    }
    return format_u64(buf, (u64)value);
}

Error sink_format_error(Sink* sink, Error error)
{
    #ifdef _WIN32
        Error err = SINK_LITERAL(sink, "0x");
        if (err) return err;
        return sink_format_u64_hex(sink, error);
    #else
        return sink_format_i64(sink, error);
    #endif
}
Error sink_format_u64(Sink* sink, u64 value)
{
    char buf[40];
    u8 char_count = format_u64(buf, value);
    return sink_put(sink, buf, char_count);
}
Error sink_format_u64_hex(Sink* sink, u64 value)
{
    char buf[32];
    char* end = buf + 32;
    char* p = end;
    do {
        *--p = "0123456789abcdef"[value & 0xF];
        value >>= 4;
    } while (value);
    return sink_put(sink, p, (size_t)(end - p));
}
Error sink_format_i64(Sink* sink, i64 value)
{
    char buf[40];
    u8 char_count = format_i64(buf, value);
    return sink_put(sink, buf, char_count);
}

Error cli_put_arg(Sink* sink, CliArg arg)
{
#ifdef _WIN32
    return sink_put_wide(sink, arg.ptr, arg.count);
#else
    return sink_put(sink, arg.ptr, arg.count);
#endif
}
