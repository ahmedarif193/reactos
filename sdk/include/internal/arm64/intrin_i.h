/*
 * PROJECT:     ReactOS SDK
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     ARM64 internal intrinsics
 * COPYRIGHT:   Copyright 2025 ReactOS Team
 */

#pragma once

/* ARM64 doesn't have these x86-specific intrinsics */
/* Provide stubs for compatibility */

/* No page table CR registers on ARM64 */
#define __readcr0() 0
#define __readcr2() 0
#define __readcr3() 0
#define __readcr4() 0

#define __writecr0(x) ((void)0)
#define __writecr2(x) ((void)0)
#define __writecr3(x) ((void)0)
#define __writecr4(x) ((void)0)

/* CPUID doesn't exist on ARM64 */
#define __cpuid(a,b) ((void)0)
#define __cpuidex(a,b,c) ((void)0)

/* No MSRs in the x86 sense on ARM64 */
#define __readmsr(x) 0
#define __writemsr(x,y) ((void)0)

/* Memory fences are provided differently on ARM64 */
#define __faststorefence() __dmb(_ARM64_BARRIER_ST)