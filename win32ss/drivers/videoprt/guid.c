/* DO NOT USE THE PRECOMPILED HEADER FOR THIS FILE! */

#include <initguid.h>
#include <wdmguid.h>
#include <potypes.h>

/* ReactOS: wdmguid.h does not carry the video power GUID, so emit it here. */
DEFINE_GUID(GUID_VIDEO_POWERDOWN_TIMEOUT,
            0x3C0BC021, 0xC8A8, 0x4E07,
            0xA9, 0x73, 0x6B, 0x14, 0xCB, 0xCB, 0x2B, 0x7E);

/* NO CODE HERE, THIS IS JUST REQUIRED FOR THE GUID DEFINITIONS */
