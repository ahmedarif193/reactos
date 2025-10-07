/*
 * PROJECT:         ReactOS SEH Library
 * LICENSE:         GPL-2.0-or-later - See COPYING in the top level directory
 * FILE:            sdk/lib/pseh/amd64/framebased.S
 * PURPOSE:         Frame-based SEH support for AMD64
 * COPYRIGHT:       Copyright 2025 Ahmed ARIF (arif.ing@outlook.com)
 */

.text
.intel_syntax noprefix

.globl _SEHCleanHandlerEnvironment
_SEHCleanHandlerEnvironment:
	ret

.globl _SEHCurrentRegistration
_SEHCurrentRegistration:
	ret

.globl _SEHRegisterFrame
_SEHRegisterFrame:
	ret

.globl _SEHUnregisterFrame
_SEHUnregisterFrame:
	ret

.globl _SEHGlobalUnwind
_SEHGlobalUnwind:
	ret

// EOF
