//
// WinMainCRTStartup.cpp
//
//      Copyright (c) 2024 ReactOS Team  
//
// GUI Application CRT startup for ANSI applications
//
// SPDX-License-Identifier: MIT
//

#include "commonCRTStartup.hpp"

extern "C" unsigned long WinMainCRTStartup(void*)
{
    __security_init_cookie();

    return __commonCRTStartup<decltype(WinMain)>();
}