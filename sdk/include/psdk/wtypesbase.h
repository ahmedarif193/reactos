#ifndef _WTYPESBASE_H_
#define _WTYPESBASE_H_
#pragma once

/* ReactOS exposes the COM base types through the generated wtypes.h header.
 * Provide the lighter Windows SDK entry point without forcing windows.h. */
#ifndef COM_NO_WINDOWS_H
#define COM_NO_WINDOWS_H
#define __ROS_WTYPESBASE_UNDEF_COM_NO_WINDOWS_H
#endif

#include <wtypes.h>

#ifdef __ROS_WTYPESBASE_UNDEF_COM_NO_WINDOWS_H
#undef __ROS_WTYPESBASE_UNDEF_COM_NO_WINDOWS_H
#undef COM_NO_WINDOWS_H
#endif

#endif /* _WTYPESBASE_H_ */
