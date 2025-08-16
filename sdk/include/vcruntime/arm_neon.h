/*
 * ARM NEON stub header for ARM64 ReactOS builds
 * This is a minimal stub to allow compilation without actual NEON support
 */

#ifndef _ARM_NEON_H_
#define _ARM_NEON_H_

/* Disable PNG NEON optimizations for now */
#ifdef PNG_ARM_NEON_OPT
#undef PNG_ARM_NEON_OPT
#define PNG_ARM_NEON_OPT 0
#endif

/* TODO: Add actual NEON intrinsics support for ARM64 */

#endif /* _ARM_NEON_H_ */