#define PACKAGE_NAME "vkd3d"
#define PACKAGE_STRING "vkd3d 2.0"
#define PACKAGE_VERSION "2.0"
#include <limits.h>
#ifndef PATH_MAX
#define PATH_MAX 1024
#endif
#define SONAME_LIBVULKAN "vulkan-1.dll"
#ifdef __REACTOS__
#define HAVE__STRTOD_L 1
#else
#define HAVE__STRTOF_L 1
#endif
