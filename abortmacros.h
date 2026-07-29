#ifndef _ABORT_MACROS_H
#define _ABORT_MACROS_H

#include "error.h"
#include "hedley.h"
#include "size_t.h"

#define UNREACHABLE() do { \
    unreachable(__FILE__, __LINE__); \
} while (0)
HEDLEY_NO_RETURN void unreachable(const char* file, unsigned line);

#define ASSERT(condition_expression) do { \
    if (!(condition_expression)) { \
        assert_failed(__FILE__, __LINE__); \
    } \
} while (0)
HEDLEY_NO_RETURN void assert_failed(const char* file, unsigned line);

#define MUST(error_expression) do {                                 \
        Error _must_err = error_expression;                          \
        if (_must_err) must_failed(_must_err, __FILE__, __LINE__);   \
} while (0)
HEDLEY_NO_RETURN void must_failed(Error error, const char* file, unsigned line);

#endif // _ABORT_MACROS_H
