/*
 * PROJECT:     ReactOS Display Driver Model
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Compatibility include for legacy dxgmms_interface.h users.
 */

#ifndef _DXGMMS_INTERFACE_H_
#define _DXGMMS_INTERFACE_H_

/*
 * Keep DXGK public DDI definitions in d3dkmddi.h.  Duplicating those structs
 * here risks include-order ABI drift and can suppress d3dkmddi.h entirely if
 * both headers share an include guard.
 */
#include <d3dkmddi.h>

#endif /* _DXGMMS_INTERFACE_H_ */
