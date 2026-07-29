#ifndef _PROCESSEXIT_H
#define _PROCESSEXIT_H

#include "hedley.h"

#ifdef _WIN32
    HEDLEY_NO_RETURN void RtlExitUserProcess(int);
    #define process_exit(code) RtlExitUserProcess(code);
#else
    HEDLEY_NO_RETURN void _exit(int);
    #define process_exit(code) _exit(code);
#endif

#endif // _PROCESSEXIT_H
