#include "precomp.h"

START_TEST(SyscallStub)
{
#ifdef _M_AMD64
    const UCHAR *Stub = (const UCHAR *)(ULONG_PTR)NtClose;

    ok(Stub[0] == 0x4c && Stub[1] == 0x8b && Stub[2] == 0xd1 && Stub[3] == 0xb8,
       "NtClose starts with %02x %02x %02x %02x\n",
       Stub[0], Stub[1], Stub[2], Stub[3]);
#endif
}
