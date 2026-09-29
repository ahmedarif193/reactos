Microsoft JPEG XR reference codec, imported from Wine's `libs/jxr` at commit
`1012f3d99507b80d4869eabf0853567660a7ecbb`.

The Microsoft redistribution terms are in LICENSE and in each imported source.

Local integration changes:

- Build the portable C implementation, with performance measurement disabled.
- Use `uintptr_t` and `intptr_t` for pointer arithmetic on Windows LLP64 targets.
- Use the compiler's `_WIN32` platform definition consistently.
- Include the intrinsic declarations required by the ReactOS CRT headers.
- Clear terminated decoder context pointers and release outstanding contexts
  when a failed decode is destroyed.
- Bound bitstream prefetch to an optional stream size and reject consumption
  beyond that size, keeping required reads strict while allowing lookahead.

The WIC adapter is `dll/win32/windowscodecs/wmpformat.c`. The reference codec
source does not come from a Windows binary or Windows disassembly.
