#ifndef _NDK_ARCH_TARGET_H
#define _NDK_ARCH_TARGET_H

#if defined(_M_ARM64) || defined(_M_ARM64EC)
#define NDK_TARGET_ARM64_CODEGEN 1
#else
#define NDK_TARGET_ARM64_CODEGEN 0
#endif

#if defined(_M_AMD64) && !NDK_TARGET_ARM64_CODEGEN
#define NDK_TARGET_AMD64_CODEGEN 1
#else
#define NDK_TARGET_AMD64_CODEGEN 0
#endif

/*
 * ARM64EC uses ARM64 instructions with an AMD64-compatible user-mode ABI.
 * Kernel-mode targets still need ARM64 kernel layouts: trap frames, PCR/PRCB
 * access and MM structures are owned by the native ARM64 kernel.
 */
#if defined(_M_ARM64) || (defined(_M_ARM64EC) && defined(REACTOS_KERNEL_MODE))
#define NDK_TARGET_ARM64_ABI 1
#else
#define NDK_TARGET_ARM64_ABI 0
#endif

#if (defined(_M_AMD64) || defined(_M_ARM64EC)) && !NDK_TARGET_ARM64_ABI
#define NDK_TARGET_AMD64_ABI 1
#else
#define NDK_TARGET_AMD64_ABI 0
#endif

#endif /* _NDK_ARCH_TARGET_H */
