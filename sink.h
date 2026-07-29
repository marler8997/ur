#ifndef _SINK_H
#define _SINK_H

#include "error.h"
#include "int.h"
#include "size_t.h"

typedef struct struct_Sink Sink;

typedef struct struct_IovConst {
    const void* ptr;
    size_t len;
} IovConst;

typedef struct struct_SinkVtable {
    Error (*drain)(Sink*, const IovConst* iov, size_t iov_count, size_t* iov_written);
} SinkVtable;

typedef struct struct_Sink {
    const SinkVtable* vtable;
    char* buffer;
    size_t capacity;
    size_t end;
} Sink;
Sink sink_init(const SinkVtable*, char* buffer, size_t capacity);
Error sink_flush(Sink*);
Error sink_put_byte(Sink*, u8);
Error sink_put(Sink*, const char*, size_t);
#define SINK_LITERAL(sink, lit) sink_put(sink, lit, sizeof(lit)-1)
Error sink_put_wide(Sink*, const u16*, size_t);

Error sink_format_error(Sink*, Error);
Error sink_format_u64(Sink*, u64);
Error sink_format_u64_hex(Sink*, u64);
Error sink_format_i64(Sink*, i64);

#ifdef _WIN32
    #define sink_put_filename sink_put_wide
#else
    #define sink_put_filename sink_put
#endif

typedef struct struct_CliArg CliArg;
Error cli_put_arg(Sink*, CliArg);

#endif // _SINK_H
