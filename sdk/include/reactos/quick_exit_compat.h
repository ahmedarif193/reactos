#ifndef ROS_QUICK_EXIT_COMPAT_H
#define ROS_QUICK_EXIT_COMPAT_H

#ifndef __cdecl
#define __cdecl
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifndef _CRT_QUICK_EXIT_DEFINED
#define _CRT_QUICK_EXIT_DEFINED
int __cdecl at_quick_exit(void (__cdecl *)(void));
void __cdecl quick_exit(int);
#endif

#ifdef __cplusplus
}
#endif

#endif /* ROS_QUICK_EXIT_COMPAT_H */
