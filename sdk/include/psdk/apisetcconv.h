/*
 * PROJECT:     ReactOS PSDK
 * LICENSE:     CC0-1.0 (https://spdx.org/licenses/CC0-1.0)
 * PURPOSE:     Legacy Win32 calling convention import macros for API sets
 */

#pragma once

#ifndef _APISETCCONV_
#define _APISETCCONV_

#ifndef DECLSPEC_IMPORT
#define DECLSPEC_IMPORT __declspec(dllimport)
#endif

#if !defined(WINADVAPI)
#if !defined(_ADVAPI32_)
#define WINADVAPI DECLSPEC_IMPORT
#else
#define WINADVAPI
#endif
#endif

#if !defined(WINBASEAPI)
#if !defined(_KERNEL32_)
#define WINBASEAPI DECLSPEC_IMPORT
#else
#define WINBASEAPI
#endif
#endif

#if !defined(ZAWPROXYAPI)
#if !defined(_ZAWPROXY_)
#define ZAWPROXYAPI DECLSPEC_IMPORT
#else
#define ZAWPROXYAPI
#endif
#endif

#if !defined(WINUSERAPI)
#if !defined(_USER32_)
#define WINUSERAPI DECLSPEC_IMPORT
#define WINABLEAPI DECLSPEC_IMPORT
#else
#define WINUSERAPI
#define WINABLEAPI
#endif
#endif

#if !defined(WINABLEAPI)
#if !defined(_USER32_)
#define WINABLEAPI DECLSPEC_IMPORT
#else
#define WINABLEAPI
#endif
#endif

#if !defined(WINCFGMGR32API)
#if !defined(_SETUPAPI_)
#define WINCFGMGR32API DECLSPEC_IMPORT
#else
#define WINCFGMGR32API
#endif
#endif

#if !defined(WINDEVQUERYAPI)
#if !defined(_CFGMGR32_)
#define WINDEVQUERYAPI DECLSPEC_IMPORT
#else
#define WINDEVQUERYAPI
#endif
#endif

#if !defined(WINSWDEVICEAPI)
#if !defined(_CFGMGR32_)
#define WINSWDEVICEAPI DECLSPEC_IMPORT
#else
#define WINSWDEVICEAPI
#endif
#endif

#if !defined(CMAPI)
#if !defined(_CFGMGR32_)
#define CMAPI DECLSPEC_IMPORT
#else
#define CMAPI
#endif
#endif

#if !defined(WINPATHCCHAPI)
#if !defined(STATIC_PATHCCH)
#define WINPATHCCHAPI WINBASEAPI
#else
#define WINPATHCCHAPI
#endif
#endif

#endif /* _APISETCCONV_ */
