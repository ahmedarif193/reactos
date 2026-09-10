/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright 2026 Ahmed ARIF */
#ifndef _ROS_CI_H_
#define _ROS_CI_H_

#include <stddef.h>
#include <stdint.h>

typedef int (*CI_READ_FILE)(void *Context, uint64_t Offset, void *Buffer, size_t Length);

/* The caller supplies an immutable file and seconds since 1970 UTC. */
int CiVerifyMicrosoftImage(CI_READ_FILE Read, void *Context, uint64_t Size, uint64_t Now);
int CiHashFile(CI_READ_FILE Read, void *Context, uint64_t Size, unsigned char Digest[32]);

/* Allocation is supplied by the kernel or the host test runner. */
void *CiAllocate(size_t Count, size_t Size);
void CiFree(void *Allocation);

#endif
