#if defined(_WIN32)
    #include "filesink-windows.c"
#elif defined(__linux__)
    #include "filesink-linux.c"
#else
    #include "filesink-posix.c"
#endif
