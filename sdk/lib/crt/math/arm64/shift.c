/*
 * PROJECT:     LiberNT CRT
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ARM64 helper routines emitted by MS-compatible code
 */

unsigned long long
__ull_rshift(unsigned long long Value, int Shift)
{
    return Value >> Shift;
}

unsigned long long
__ll_rshift(unsigned long long Value, int Shift)
{
    return (unsigned long long)((long long)Value >> Shift);
}

unsigned long long
__ll_lshift(unsigned long long Value, int Shift)
{
    return Value << Shift;
}
