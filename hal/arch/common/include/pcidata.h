/*
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 */

/* Placement of the generated PCI name tables (pci_classes.c, pci_vendors.c) */
#pragma once

#include <ndk/section_attribs.h>

/* MSVC needs the section declared before DATA_SEG allocates into it */
#ifdef _MSC_VER
#pragma section("INITDATA", read, discard)
#endif
