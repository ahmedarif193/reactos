/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include <stdio.h>
#include <string.h>
int main(void)
{
    DECLSPEC_ALIGN(16) ULONG64 stack[32];
    DECLSPEC_ALIGN(16) BYTE image[512] = {0};
    RUNTIME_FUNCTION function;
    CONTEXT context;
    KNONVOLATILE_CONTEXT_POINTERS pointers;
    ULONG64 frame;
    PVOID handler;
    unsigned i, cycle, failures = 0;
    setvbuf(stdout, NULL, _IONBF, 0);
    for (i = 0; i < 32; i++) stack[i] = 0x12340000 + i;
    for (cycle = 0; cycle < 10000; cycle++)
    {
        memset(&context, 0, sizeof(context));
        memset(&pointers, 0, sizeof(pointers));
        context.Sp = (ULONG_PTR)stack;
        function.BeginAddress = 0;
        function.UnwindData = 2 | (64 << 2) | (1 << 13) | (2 << 16) | (1 << 21) | (4 << 23);
        RtlVirtualUnwind(0, (ULONG_PTR)image, (ULONG_PTR)image + 128, &function, &context, &handler, &frame, &pointers);
        if (context.X19 != stack[2] || context.X20 != stack[3] || context.Lr != stack[4] || context.V[8].Low != stack[5] || context.V[9].Low != stack[6] || context.Sp != (ULONG_PTR)(stack + 8)) failures++;
        if (pointers.X19 != stack+2 || pointers.X20 != stack+3 || pointers.Lr != stack+4 || pointers.D8 != stack+5 || pointers.D9 != stack+6 || pointers.X21 != NULL || pointers.Fp != NULL) failures++;
        memset(&context, 0, sizeof(context));
        memset(&pointers, 0, sizeof(pointers));
        context.Sp = (ULONG_PTR)stack;
        function.UnwindData = 256;
        *(DWORD *)(image+256) = 64 | (2u << 27);
        memcpy(image+260, "\xc8\x00\xd8\x02\x44\x04\xe4\xe3", 8);
        RtlVirtualUnwind(0, (ULONG_PTR)image, (ULONG_PTR)image + 128, &function, &context, &handler, &frame, &pointers);
        if (context.X19 != stack[0] || context.X20 != stack[1] || context.Fp != stack[4] || context.Lr != stack[5] || context.V[8].Low != stack[2] || context.Sp != (ULONG_PTR)(stack+8)) failures++;
        if (pointers.X19 != stack || pointers.X20 != stack+1 || pointers.Fp != stack+4 || pointers.Lr != stack+5 || pointers.D8 != stack+2 || pointers.D9 != stack+3 || pointers.X21 != NULL) failures++;
    }
    printf("UNWIND_POINTERS_DONE cases=20000 failures=%u\n", failures);
    return failures ? 1 : 0;
}
