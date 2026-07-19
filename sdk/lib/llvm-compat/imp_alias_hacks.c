/*
 * PROJECT:     ReactOS SDK
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     dllimport-slot aliases for llvm-mingw runtime references
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

/*
 * llvm-mingw's runtime objects reference these CRT functions through dllimport slots. Resolving the slots
 * from the ucrtbase import library pulls import thunks that collide with the static definitions in
 * libmingwex/msvcrtex (lld: "<sym> was replaced"), so bind the slots to the static definitions instead.
 */

#include "imp_alias.h"

IMP_ALIAS(btowc);
IMP_ALIAS(mbrtowc);
IMP_ALIAS(wcrtomb);
IMP_ALIAS(wctob);

#if defined(__i386__)
/* Covered by msvcrtex on amd64; its plain-C slot definitions miss the i386 underscore decoration */
IMP_ALIAS(__acrt_iob_func);
IMP_ALIAS(rand_s);
#endif
