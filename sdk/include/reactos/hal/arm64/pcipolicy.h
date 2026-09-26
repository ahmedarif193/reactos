/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 Ahmed ARIF
 */

/* PCI routing policy supplied by this platform HAL. */
#pragma once

/* Message interrupts depend on the interrupt controller: pci.sys asks the HAL. */
#define HAL_PCI_HAS_MSI_SUPPORT_QUERY
