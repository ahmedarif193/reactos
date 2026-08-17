# PROJECT:     ReactOS Build System
# PURPOSE:     Define the ARM64EC runtime target manifest
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>

# Single source of truth for the FEX ARM64EC user-runtime double-build.
#
# These targets are configured in a nested tree whose source architecture is
# still ARM64, but whose compiler target and public ABI are ARM64EC. The set is
# deliberately limited to the ARM64EC system DLLs required by the initial FEX
# runtime. Other dependencies continue to use the native ARM64 system copies.

set(ARM64EC_RUNTIME_MODULES
    advapi32
    advpack
    bcrypt
    browseui
    cabinet
    cfgmgr32
    combase
    comctl32
    comdlg32
    coml2
    crypt32
    cryptnet
    cryptui
    dbghelp
    devmgr
    dhcpcsvc
    dinput8
    dnsapi
    dwmapi
    fmifs
    gdi32
    gdiplus
    hid
    iertutil
    imagehlp
    imm32
    iphlpapi
    jsproxy
    kernel32
    kernel32_vista
    kernelbase_ros
    libjpeg
    libpng
    libtiff
    lpk
    mbedtls
    mmdevapi
    mlang
    mpr
    msacm32
    msctf
    msctfime
    msimg32
    msvcrt
    ncrypt
    netapi32
    newdev
    normaliz
    nsi
    ntdll_chpe
    ntdll_vista
    ole32
    oleaut32
    opengl32
    powrprof
    propsys
    rpcrt4
    samlib
    sechost
    secur32
    shcore
    shell32
    shdocvw
    shlwapi
    setupapi
    ucrtbase
    urlmon
    user32
    user32_vista
    userenv
    usp10
    uxtheme
    version
    windowscodecs
    winhttp
    wininet
    winmm
    winspool
    wintrust
    ws2_32)

# Additional names under which a runtime module must be shipped. Keep the
# implementation name because existing ReactOS modules import it directly.
set(ARM64EC_RUNTIME_ALIASES
    "kernelbase_ros=kernelbase.dll")
