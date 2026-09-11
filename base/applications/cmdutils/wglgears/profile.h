/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF <arif193@gmail.com>
 */
#pragma once

BOOL WglGearsProfileStart(PROC IcdControl);
void WglGearsProfileFrameStart(void);
void WglGearsProfileDrawEnd(void);
void WglGearsProfileFrameEnd(BOOL Presented);
BOOL WglGearsProfileStop(void);
