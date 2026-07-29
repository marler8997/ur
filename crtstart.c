#if !__STDC_HOSTED__

#if defined(_WIN32)
    void RtlExitUserProcess(int);
    int crtmain(void);
    void wWinMainCRTStartup(void) { RtlExitUserProcess(crtmain()); }
#elif defined(__linux__)
    // ELF entry: the kernel puts argc, argv[], envp[] on the stack (rsp -> argc). Naked so no
    // prologue touches rsp before we read it.
    __attribute__((naked)) void _start(void)
    {
        __asm__ volatile (
            "xorl %ebp, %ebp\n"       // ABI: clear frame pointer
            "movq (%rsp), %rdi\n"     // argc
            "leaq 8(%rsp), %rsi\n"    // argv
            "andq $-16, %rsp\n"       // align stack to 16 bytes
            "call main\n"
            "movl %eax, %edi\n"       // exit code = main's return value
            "movl $60, %eax\n"        // SYS_exit (x86_64)
            "syscall\n"
        );
    }
#endif

#endif // !__STDC_HOSTED__
