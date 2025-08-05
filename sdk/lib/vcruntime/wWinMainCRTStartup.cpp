//
// wWinMainCRTStartup.cpp
//
//      Copyright (c) 2024 ReactOS Team  
//
// GUI Application CRT startup for Unicode applications
//
// SPDX-License-Identifier: MIT
//

#include "commonCRTStartup.hpp"

extern "C" unsigned long wWinMainCRTStartup(void*)
{
    __security_init_cookie();

    return __commonCRTStartup<decltype(wWinMain)>();
}