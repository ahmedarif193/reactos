/*
 * PROJECT:     ReactOS PSDK
 * LICENSE:     CC0-1.0 (https://spdx.org/licenses/CC0-1.0)
 * PURPOSE:     API set compatibility macros
 */

#pragma once

#ifndef _API_SET_H_
#define _API_SET_H_

#define API_SET_OVERRIDE(Symbol)               Symbol##Implementation
#define API_SET_LEGACY_OVERRIDE_DEF(Symbol)    Symbol = API_SET_OVERRIDE(Symbol)
#define API_SET_OVERRIDE_DEF(Symbol)           API_SET_LEGACY_OVERRIDE_DEF(Symbol) PRIVATE

#ifdef _M_HYBRID_X86_ARM64
#define API_SET_CHPE_GUEST X86
#else
#define API_SET_CHPE_GUEST
#endif

#ifdef _API_SET_HOST

#undef API_SET_LIBRARY

#undef API_SET
#undef API_SET_DIR
#undef API_SET_PRIVATE
#undef API_SET_PRIVATE_DIR
#undef API_SET_BY_ORDINAL
#undef API_SET_BY_ORDINAL_DIR
#undef API_SET_BY_ORDINAL_PRIVATE
#undef API_SET_BY_ORDINAL_PRIVATE_DIR

#undef API_SET_LEGACY
#undef API_SET_LEGACY_DIR
#undef API_SET_LEGACY_PRIVATE
#undef API_SET_LEGACY_PRIVATE_DIR
#undef API_SET_LEGACY_BY_ORDINAL
#undef API_SET_LEGACY_BY_ORDINAL_DIR
#undef API_SET_LEGACY_BY_ORDINAL_PRIVATE
#undef API_SET_LEGACY_BY_ORDINAL_PRIVATE_DIR

#define API_SET_LIBRARY(Name)                              LIBRARY Name
#define API_SET(Name)                                      Name PRIVATE
#define API_SET_DIR(Name, Dir)                             Name Dir PRIVATE
#define API_SET_PRIVATE(Name)                              Name PRIVATE
#define API_SET_PRIVATE_DIR(Name, Dir)                     Name Dir PRIVATE
#define API_SET_BY_ORDINAL(Name, Ordinal, PublicOrdinal)   Name @##Ordinal NONAME PRIVATE
#define API_SET_BY_ORDINAL_DIR(Name, Ordinal, PublicOrdinal, Dir) \
                                                            Name @##Ordinal NONAME Dir PRIVATE
#define API_SET_BY_ORDINAL_PRIVATE(Name, Ordinal, PublicOrdinal) \
                                                            Name @##Ordinal NONAME PRIVATE
#define API_SET_BY_ORDINAL_PRIVATE_DIR(Name, Ordinal, PublicOrdinal, Dir) \
                                                            Name @##Ordinal NONAME Dir PRIVATE

#define API_SET_LEGACY(Name, LegacyTarget)                 Name PRIVATE
#define API_SET_LEGACY_DIR(Name, LegacyTarget, Dir)        Name Dir PRIVATE
#define API_SET_LEGACY_PRIVATE(Name, LegacyTarget)         Name PRIVATE
#define API_SET_LEGACY_PRIVATE_DIR(Name, LegacyTarget, Dir) \
                                                            Name Dir PRIVATE
#define API_SET_LEGACY_BY_ORDINAL(Name, LegacyTarget, Ordinal, PublicOrdinal) \
                                                            Name @##Ordinal NONAME PRIVATE
#define API_SET_LEGACY_BY_ORDINAL_DIR(Name, LegacyTarget, Ordinal, PublicOrdinal, Dir) \
                                                            Name @##Ordinal NONAME Dir PRIVATE
#define API_SET_LEGACY_BY_ORDINAL_PRIVATE(Name, LegacyTarget, Ordinal, PublicOrdinal) \
                                                            Name @##Ordinal NONAME PRIVATE
#define API_SET_LEGACY_BY_ORDINAL_PRIVATE_DIR(Name, LegacyTarget, Ordinal, PublicOrdinal, Dir) \
                                                            Name @##Ordinal NONAME Dir PRIVATE

#else

#ifndef _API_SET_LEGACY_TARGET

#define API_SET_LIBRARY(Name)                              LIBRARY Name
#define API_SET(Name)                                      Name
#define API_SET_DIR(Name, Dir)                             Name Dir
#define API_SET_PRIVATE(Name)                              Name PRIVATE
#define API_SET_PRIVATE_DIR(Name, Dir)                     Name Dir PRIVATE
#define API_SET_BY_ORDINAL(Name, Ordinal, PublicOrdinal)   Name @##Ordinal NONAME
#define API_SET_BY_ORDINAL_DIR(Name, Ordinal, PublicOrdinal, Dir) \
                                                            Name @##Ordinal NONAME Dir
#define API_SET_BY_ORDINAL_PRIVATE(Name, Ordinal, PublicOrdinal) \
                                                            Name @##Ordinal NONAME PRIVATE
#define API_SET_BY_ORDINAL_PRIVATE_DIR(Name, Ordinal, PublicOrdinal, Dir) \
                                                            Name @##Ordinal NONAME Dir PRIVATE

#define API_SET_LEGACY(Name, LegacyTarget)                 Name
#define API_SET_LEGACY_DIR(Name, LegacyTarget, Dir)        Name Dir
#define API_SET_LEGACY_PRIVATE(Name, LegacyTarget)         Name PRIVATE
#define API_SET_LEGACY_PRIVATE_DIR(Name, LegacyTarget, Dir) \
                                                            Name Dir PRIVATE
#define API_SET_LEGACY_BY_ORDINAL(Name, LegacyTarget, Ordinal, PublicOrdinal) \
                                                            Name @##Ordinal NONAME
#define API_SET_LEGACY_BY_ORDINAL_DIR(Name, LegacyTarget, Ordinal, PublicOrdinal, Dir) \
                                                            Name @##Ordinal NONAME Dir
#define API_SET_LEGACY_BY_ORDINAL_PRIVATE(Name, LegacyTarget, Ordinal, PublicOrdinal) \
                                                            Name @##Ordinal NONAME PRIVATE
#define API_SET_LEGACY_BY_ORDINAL_PRIVATE_DIR(Name, LegacyTarget, Ordinal, PublicOrdinal, Dir) \
                                                            Name @##Ordinal NONAME Dir PRIVATE

#else

#define API_SET_LIBRARY(Name)                              LIBRARY Name
#define API_SET(Name)                                      Name = _API_SET_LEGACY_TARGET##.##Name
#define API_SET_DIR(Name, Dir)                             Name = _API_SET_LEGACY_TARGET##.##Name Dir
#define API_SET_PRIVATE(Name)                              Name = _API_SET_LEGACY_TARGET##.##Name PRIVATE
#define API_SET_PRIVATE_DIR(Name, Dir)                     Name = _API_SET_LEGACY_TARGET##.##Name Dir PRIVATE
#define API_SET_BY_ORDINAL(Name, Ordinal, PublicOrdinal)   Name = _API_SET_LEGACY_TARGET##.##PublicOrdinal @##Ordinal NONAME
#define API_SET_BY_ORDINAL_DIR(Name, Ordinal, PublicOrdinal, Dir) \
                                                            Name = _API_SET_LEGACY_TARGET##.##PublicOrdinal @##Ordinal NONAME Dir
#define API_SET_BY_ORDINAL_PRIVATE(Name, Ordinal, PublicOrdinal) \
                                                            Name = _API_SET_LEGACY_TARGET##.##PublicOrdinal @##Ordinal NONAME PRIVATE
#define API_SET_BY_ORDINAL_PRIVATE_DIR(Name, Ordinal, PublicOrdinal, Dir) \
                                                            Name = _API_SET_LEGACY_TARGET##.##PublicOrdinal @##Ordinal NONAME Dir PRIVATE

#define API_SET_LEGACY(Name, LegacyTarget)                 Name = LegacyTarget##.##Name
#define API_SET_LEGACY_DIR(Name, LegacyTarget, Dir)        Name = LegacyTarget##.##Name Dir
#define API_SET_LEGACY_PRIVATE(Name, LegacyTarget)         Name = LegacyTarget##.##Name PRIVATE
#define API_SET_LEGACY_PRIVATE_DIR(Name, LegacyTarget, Dir) \
                                                            Name = LegacyTarget##.##Name Dir PRIVATE
#define API_SET_LEGACY_BY_ORDINAL(Name, LegacyTarget, Ordinal, PublicOrdinal) \
                                                            Name = LegacyTarget##.##PublicOrdinal @##Ordinal NONAME
#define API_SET_LEGACY_BY_ORDINAL_DIR(Name, LegacyTarget, Ordinal, PublicOrdinal, Dir) \
                                                            Name = LegacyTarget##.##PublicOrdinal @##Ordinal NONAME Dir
#define API_SET_LEGACY_BY_ORDINAL_PRIVATE(Name, LegacyTarget, Ordinal, PublicOrdinal) \
                                                            Name = LegacyTarget##.##PublicOrdinal @##Ordinal NONAME PRIVATE
#define API_SET_LEGACY_BY_ORDINAL_PRIVATE_DIR(Name, LegacyTarget, Ordinal, PublicOrdinal, Dir) \
                                                            Name = LegacyTarget##.##PublicOrdinal @##Ordinal NONAME Dir PRIVATE

#endif /* _API_SET_LEGACY_TARGET */

#endif /* _API_SET_HOST */

#endif /* _API_SET_H_ */
