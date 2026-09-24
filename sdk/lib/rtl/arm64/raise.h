/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 Ahmed ARIF
 */

/* Architecture policy for the shared C exception-raising helpers. */
#pragma once

#define RTLP_RAISE_EXCEPTION_NEEDS_CALLER_CONTEXT
#define RTLP_RAISE_USES_RESTORE_CONTEXT
#define RTLP_RAISE_CONTEXT_FLAGS (CONTEXT_FULL | CONTEXT_UNWOUND_TO_CALL)
