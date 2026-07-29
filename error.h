#ifndef _ERROR_H
#define _ERROR_H

#ifdef _WIN32
    #include "int.h"
    typedef u32 Error;
#else
    typedef int Error;
#endif

#endif // _ERROR_H
