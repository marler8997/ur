#ifndef _CLI_H
#define _CLI_H

#include "bool.h"
#include "int.h"
#include "size_t.h"

typedef struct struct_CliIterator {
    #ifdef _WIN32
        const u16* cursor;
    #else
        char** cursor;   // points at the next argv entry; the array ends at a NULL
    #endif
} CliIterator;

#ifdef _WIN32
    void cli_iterator_init(CliIterator*);
#else
    void cli_iterator_init(CliIterator*, char** argv);
#endif

typedef struct struct_CliArg {
    #ifdef _WIN32
        const u16* ptr;   // UTF-16 code units
    #else
        const char* ptr;  // UTF-8 bytes
    #endif
    size_t count;
} CliArg;

Bool cli_next(CliIterator*, CliArg*);

#define CLI_ARG_MATCH(arg, lit) cli_arg_match(arg, lit, sizeof(lit) - 1)
Bool cli_arg_match(CliArg, const char* ptr, size_t count);

#endif // _CLI_H
