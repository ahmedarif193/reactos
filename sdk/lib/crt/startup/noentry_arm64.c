#if defined(_M_ARM64) || defined(_ARM64_) || defined(__aarch64__) || defined(__arm64__)

__attribute__((used))
unsigned long __ReactOSNoEntry(void *hinst, unsigned long reason, void *reserved)
{
    (void)hinst;
    (void)reason;
    (void)reserved;
    return 1;
}

#endif
