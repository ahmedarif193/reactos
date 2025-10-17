/*
 * PROJECT:     Freeldr UEFI Extension
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Uefi freeldr core header
 * COPYRIGHT:   Copyright 2022 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

/* INCLUDES ******************************************************************/
#include <freeldr.h>

/* UEFI Headers */
#include <Uefi.h>
#include <DevicePath.h>
#include <LoadedImage.h>
#include <GraphicsOutput.h>
#include <UgaDraw.h>
#include <BlockIo.h>
#include <Acpi.h>
#include <GlobalVariable.h>
#include <debug.h>
#include <machuefi.h>

//TODO: this version of the struct is temporary
#include <framebuffer.h>

typedef FREELDR_FRAMEBUFFER_INFO REACTOS_INTERNAL_BGCONTEXT;
typedef FREELDR_FRAMEBUFFER_INFO *PREACTOS_INTERNAL_BGCONTEXT;

VOID __cdecl BootMain(IN PCCH CmdLine);
