#pragma once
#ifndef UNICODE
#error taskmgrv2 uses NDK functions, build must be UNICODE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <math.h>

#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>
#include <wingdi.h>
#include <winnls.h>
#include <winuser.h>
#include <winreg.h>
#include <commctrl.h>
#include <shellapi.h>
#include <strsafe.h>

#include "taskmgrv2.h"
#include "resource.h"
#include "registry.h"
#include "theme.h"
#include "perfdata.h"
#include "cpu_topology.h"
#include "graphctl.h"
#include "pages.h"
#include "shell.h"
#include "page_performance.h"
#include "page_processes.h"
#include "page_details.h"
#include "page_services.h"
#include "page_users.h"
#include "page_startup.h"
#include "page_apphistory.h"
#include "page_settings.h"
