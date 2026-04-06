/*
 * PROJECT:     ReactOS PSDK
 * LICENSE:     CC0-1.0 (https://spdx.org/licenses/CC0-1.0)
 * PURPOSE:     Compatibility shim for banned-API deprecation header
 */

#pragma once

#ifndef _DONTUSE_H_INCLUDED_
#define _DONTUSE_H_INCLUDED_

#include <winapifamily.h>

/*
 * The Windows SDK uses this header to add a large set of deprecation pragmas.
 * ReactOS keeps it as an intentionally lightweight compatibility shim so newer
 * cleanroom headers can include it without forcing policy changes on callers.
 */

#endif /* _DONTUSE_H_INCLUDED_ */
