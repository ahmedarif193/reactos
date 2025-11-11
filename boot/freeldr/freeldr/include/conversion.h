/*
 * PROJECT:         EFI Windows Loader
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            boot/freeldr/freeldr/windows/conversion.c
 * PURPOSE:         Physical <-> Virtual addressing mode conversions (arch-specific)
 * PROGRAMMERS:     Aleksey Bragin (aleksey@reactos.org)
 */

#pragma once

PVOID
VaToPa(PVOID Va);

PVOID
PaToVa(PVOID Pa);
