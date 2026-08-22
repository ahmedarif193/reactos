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
    apphelp
    avrt
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
    dsound
    dwmapi
    fmifs
    gdi32
    gdiplus
    glu32
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
    msafd
    msctf
    msctfime
    msimg32
    msvcrt
    mswsock
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
    psapi
    propsys
    riched20
    rsaenh
    rpcrt4
    samlib
    sechost
    secur32
    shcore
    shell32
    shdocvw
    shfolder
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
    wsock32
    ws2_32
    wshtcpip
    xinput1_1
    xinput1_2
    xinput1_3
    xinput1_4
    xinput9_1_0)

# SxS implementations need a distinct packaged identity because the native
# ARM64 and ARM64EC double-builds cannot occupy the same assembly path.
set(ARM64EC_RUNTIME_AUXILIARY_MODULES
    comctl32_v6)

# Additional names under which a runtime module must be shipped. Keep the
# implementation name because existing ReactOS modules import it directly.
set(ARM64EC_RUNTIME_ALIASES
    "comctl32_v6=comctl32_v6.dll"
    "kernelbase_ros=kernelbase.dll")
