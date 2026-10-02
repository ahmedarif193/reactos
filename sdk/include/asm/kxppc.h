/*
 * PROJECT:     LiberNT SDK
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Windows NT PowerPC assembly helpers
 *
 * A Windows NT PowerPC function has two symbols: the code entry "..Name"
 * that direct calls branch to, and the two-word function descriptor "Name"
 * {..Name, .toc} that function pointers and export tables refer to.
 */

#pragma once

/* Start a function: define its code entry. */
#define LEAF_ENTRY(Name) \
    .text; .p2align 2; .globl ..Name; ..Name:

#define NESTED_ENTRY(Name) LEAF_ENTRY(Name)

/* End a function: emit its descriptor. */
#define LEAF_END(Name) \
    .section .rdata,"dr"; .p2align 2; .globl Name; Name: .long ..Name; .long .toc; .text

#define NESTED_END(Name) LEAF_END(Name)

/* Direct call to a function that may live in another image. */
#define CALL_EXTERNAL(Name) \
    bl ..Name; .znop ..Name
