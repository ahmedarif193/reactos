#include <setjmp.h>
#include <stdlib.h>

extern "C" {

int __cdecl __intrinsic_setjmp(jmp_buf buffer)
{
    return setjmp(buffer);
}

int __cdecl __intrinsic_setjmpex(jmp_buf, void*);

int __cdecl __wine_setjmpex(jmp_buf buffer, void* context)
{
    return __intrinsic_setjmpex(buffer, context);
}

int __cdecl __acrt_initialize_fma3(void)
{
    return 0;
}

}
