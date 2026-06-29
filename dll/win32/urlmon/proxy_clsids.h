#pragma once

/* Use the existing CLSID_PSFactoryBuffer defined in urlmon_main.c (via DEFINE_GUID + INITGUID).
 * PROXY_CLSID causes rpcproxy.h to emit 'extern CLSID PROXY_CLSID' (declaration only),
 * avoiding a duplicate definition with urlmon_main.c. */
#ifndef PROXY_CLSID
#define PROXY_CLSID CLSID_PSFactoryBuffer
#endif
