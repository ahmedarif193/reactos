#include <stdlib.h>

#undef _lrotl
#undef _lrotr
#undef _rotl
#undef _rotl64
#undef _rotr
#undef _rotr64

/* These helpers bridge the prefixed clang names back to the exports the
 * DEF file expects. The real implementations live in rotl.cpp/rotr.cpp and
 * get renamed to the triple-underscore forms by the build defines. */
unsigned long __cdecl ___lrotl(unsigned long value, int shift);
unsigned long __cdecl ___lrotr(unsigned long value, int shift);
unsigned __cdecl ___rotl(unsigned value, int shift);
unsigned __cdecl ___rotr(unsigned value, int shift);
unsigned __int64 __cdecl ___rotl64(unsigned __int64 value, int shift);
unsigned __int64 __cdecl ___rotr64(unsigned __int64 value, int shift);

unsigned long __cdecl Arm64Lrotl(unsigned long value, int shift) __asm__("_lrotl");
unsigned long __cdecl Arm64Lrotr(unsigned long value, int shift) __asm__("_lrotr");
unsigned __cdecl Arm64Rotl(unsigned value, int shift) __asm__("_rotl");
unsigned __cdecl Arm64Rotr(unsigned value, int shift) __asm__("_rotr");
unsigned __int64 __cdecl Arm64Rotl64(unsigned __int64 value, int shift) __asm__("_rotl64");
unsigned __int64 __cdecl Arm64Rotr64(unsigned __int64 value, int shift) __asm__("_rotr64");

unsigned long __cdecl Arm64Lrotl(unsigned long value, int shift)
{
    return ___lrotl(value, shift);
}

unsigned long __cdecl Arm64Lrotr(unsigned long value, int shift)
{
    return ___lrotr(value, shift);
}

unsigned __cdecl Arm64Rotl(unsigned value, int shift)
{
    return ___rotl(value, shift);
}

unsigned __cdecl Arm64Rotr(unsigned value, int shift)
{
    return ___rotr(value, shift);
}

unsigned __int64 __cdecl Arm64Rotl64(unsigned __int64 value, int shift)
{
    return ___rotl64(value, shift);
}

unsigned __int64 __cdecl Arm64Rotr64(unsigned __int64 value, int shift)
{
    return ___rotr64(value, shift);
}
