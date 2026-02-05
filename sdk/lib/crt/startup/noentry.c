/*
 * Generic no-entry stub for DLLs that don't need a CRT startup.
 * Used when set_module_type specifies ENTRYPOINT 0.
 */

__attribute__((used))
unsigned long __ReactOSNoEntry(void *hinst, unsigned long reason, void *reserved)
{
    (void)hinst;
    (void)reason;
    (void)reserved;
    return 1;
}
