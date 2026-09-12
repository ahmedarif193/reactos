/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Firmware-stage CRT adapters (no process, locale, or NT imports).
 */

#include <freeldr.h>
#include <limits.h>

void __cdecl _invalid_parameter(const wchar_t *Expression, const wchar_t *Function,
                               const wchar_t *File, unsigned int Line, uintptr_t Reserved)
{
    RtlAssert("invalid CRT parameter", "RISC-V firmware runtime", Line, NULL);
}

unsigned long __cdecl strtoul(const char *Text, char **End, int Base)
{
    unsigned long long Value = strtoull(Text, End, Base);
    return Value > ULONG_MAX ? ULONG_MAX : (unsigned long)Value;
}

int __cdecl wctomb(char *Text, wchar_t Character)
{
    if (!Text) return 0;
    /* FreeLdr's firmware-stage CRT has an invariant single-byte locale. */
    if (Character > 0x7f) return -1;
    *Text = Character;
    return 1;
}
