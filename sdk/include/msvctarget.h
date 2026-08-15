#pragma once

/* Translate GCC/Clang target defines to the MS-style architecture macros
 * expected by the Windows-compatible SDK headers. */
#if defined(__arm64ec__)
 #if !defined(_AMD64_)
  #define _AMD64_ 1
 #endif
 #if !defined(_M_AMD64)
  #define _M_AMD64 1
 #endif
 #if !defined(_M_X64)
  #define _M_X64 1
 #endif
 #if !defined(_ARM64EC_)
  #define _ARM64EC_ 1
 #endif
 #if !defined(_M_ARM64EC)
  #define _M_ARM64EC 1
 #endif
#elif defined(__i386__)
 #if !defined(_X86_)
  #define _X86_ 1
 #endif
 #if !defined(_M_IX86)
  #define _M_IX86 1
 #endif
#elif defined(__x86_64__) || defined(__x86_64)
 #if !defined(_AMD64_)
  #define _AMD64_ 1
 #endif
 #if !defined(_M_AMD64)
  #define _M_AMD64 1
 #endif
 #if !defined(_M_X64)
  #define _M_X64 1
 #endif
#elif defined(__arm__)
 #if !defined(_ARM_)
  #define _ARM_ 1
 #endif
 #if !defined(_M_ARM)
  #define _M_ARM 1
 #endif
#elif defined(__aarch64__) || defined(__arm64__)
 #if !defined(_ARM64_)
  #define _ARM64_ 1
 #endif
 #if !defined(_M_ARM64)
  #define _M_ARM64 1
 #endif
#elif defined(__ia64__)
 #if !defined(_IA64_)
  #define _IA64_ 1
 #endif
 #if !defined(_M_IA64)
  #define _M_IA64 1
 #endif
#elif defined(__alpha__)
 #if !defined(_ALPHA_)
  #define _ALPHA_ 1
 #endif
 #if !defined(_M_ALPHA)
  #define _M_ALPHA 1
 #endif
#elif defined(__powerpc__)
 #if !defined(_PPC_)
  #define _PPC_ 1
 #endif
 #if !defined(_M_PPC)
  #define _M_PPC 1
 #endif
#else
 #error Unknown architecture
#endif

/* ARM64EC has the AMD64 public ABI, but Clang emits AArch64 instructions. */
#if defined(_M_ARM64) || defined(_M_ARM64EC)
 #define REACTOS_TARGET_ARM64_CODEGEN 1
#else
 #define REACTOS_TARGET_ARM64_CODEGEN 0
#endif

#if defined(_M_AMD64) && !REACTOS_TARGET_ARM64_CODEGEN
 #define REACTOS_TARGET_AMD64_CODEGEN 1
#else
 #define REACTOS_TARGET_AMD64_CODEGEN 0
#endif
