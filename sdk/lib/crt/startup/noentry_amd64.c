#if defined(_M_AMD64) || defined(_AMD64_) || defined(__x86_64__)

__attribute__((used))
unsigned long __ReactOSNoEntry(void *hinst, unsigned long reason, void *reserved)
{
    (void)hinst;
    (void)reason;
    (void)reserved;
    return 1;
}

#endif
