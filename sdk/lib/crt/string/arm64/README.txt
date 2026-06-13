AArch64 string/memory routines vendored from Arm optimized-routines
(https://github.com/ARM-software/optimized-routines), string/aarch64/.
License: MIT OR Apache-2.0 WITH LLVM-exception (see SPDX header in each .S).
Local additions per file: a public-name alias (.set <name>, __<name>_aarch64)
and the asmdefs.h shim (ENTRY/END/L) for the ReactOS COFF/clang toolchain.
