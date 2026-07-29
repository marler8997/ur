#include "cli.h"

#include "mem.h"

////////////////////////////////////////////////////////////////////////////////
#ifdef _WIN32
////////////////////////////////////////////////////////////////////////////////

#include "windows-peb.h"

void cli_iterator_init(CliIterator* it)
{
    it->cursor = peb()->ProcessParameters->CommandLine.ptr;
    /* skip argv[0] (the exe path) so iteration yields only the real arguments */
    CliArg ignored;
    cli_next(it, &ignored);
}

Bool cli_next(CliIterator* it, CliArg* arg)
{
    const u16* s = it->cursor;
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    if (*s == 0) {                          /* no more arguments */
        it->cursor = s;
        return 0;
    }
    const u16* start;
    const u16* end;
    if (*s == '"') {                        /* quoted token */
        s++;
        start = s;
        while (*s && *s != '"') {
            s++;
        }
        end = s;
        if (*s == '"') {
            s++;
        }
    } else {                                /* bare token */
        start = s;
        while (*s && *s != ' ' && *s != '\t') {
            s++;
        }
        end = s;
    }
    it->cursor = s;
    arg->ptr = start;
    arg->count = (size_t)(end - start);
    return 1;
}

Bool cli_arg_match(CliArg arg, const char* str, size_t count)
{
    if (arg.count != count) return 0;
    for (size_t i = 0; i < count; i++) {
        // should be corrent to widen our ascii str to compare with WTF16
        if (arg.ptr[i] != (u16)(u8)str[i]) return 0;
    }
    return 1;
}

////////////////////////////////////////////////////////////////////////////////
#else // !defined(_WIN32)
////////////////////////////////////////////////////////////////////////////////

void cli_iterator_init(CliIterator* it, char** argv)
{
    it->cursor = argv + 1;   // skip argv[0]
}

Bool cli_next(CliIterator* it, CliArg* arg)
{
    char* s = *it->cursor;
    if (s == 0) return 0;
    it->cursor++;
    arg->ptr = s;
    arg->count = libc_strlen(s);
    return 1;
}

Bool cli_arg_match(CliArg arg, const char* str, size_t count)
{
    if (arg.count != count) return 0;
    return 0 == libc_memcmp(arg.ptr, str, count);
}

#endif
