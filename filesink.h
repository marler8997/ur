#ifndef _FILESINK_H
#define _FILESINK_H

#include "size_t.h"
#include "int.h"
#include "fs.h"
#include "sink.h"

extern const struct struct_SinkVtable stdout_vtable;
extern const struct struct_SinkVtable stderr_vtable;

typedef struct struct_FileSink {
    Sink base;
    File file;
} FileSink;
extern const SinkVtable file_vtable;
static inline FileSink file_sink_init(File file, char* buffer, size_t capacity)
{
    FileSink fw;
    fw.base = sink_init(&file_vtable, buffer, capacity);
    fw.file = file;
    return fw;
}

#endif // _FILESINK_H
