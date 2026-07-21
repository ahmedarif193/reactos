# Single source of truth for the WoW64 i386 double-build.
#
# Every target named here is built a second time as i386 in the nested
# build tree (_wow64_i386) and shipped to reactos/SysWOW64. Nothing is
# discovered implicitly: when a listed module starts importing a DLL that
# is not listed here, configuration fails and names the targets to add.
#
# List a module when the 32-bit loader must be able to find it: every DLL
# imported by another listed module, DLLs loaded dynamically at run time,
# and every 32-bit program we ship.

set(WOW64_I386_MODULES
    advapi32
    advapi32_vista
    advpack
    bcrypt
    browseui
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
    fmifs
    gdi32
    gdiplus
    hid
    ieframe
    imagehlp
    imm32
    iphlpapi
    kernel32
    kernel32_vista
    kernelbase_ros
    libjpeg
    libpng
    libtiff
    lpk
    mbedtls
    mlang
    mpr
    msacm32
    msimg32
    msvcrt
    netapi32
    newdev
    normaliz
    ntdll
    ntdll_vista
    ole32
    oleaut32
    powrprof
    propsys
    psapi
    rpcrt4
    samlib
    secur32
    setupapi
    shcore
    shdocvw
    shell32
    shlwapi
    urlmon
    user32
    userenv
    usp10
    uxtheme
    version
    windowscodecs
    wininet
    winmm
    winspool
    wintrust
    ws2_32
    ws2help)

set(WOW64_I386_EXECUTABLES
    notepad
    winver)

if(ENABLE_ROSTESTS)
    # The win32u tests import win32u.dll directly; the regular guest runtime
    # reaches it through its own syscall stubs instead.
    list(APPEND WOW64_I386_MODULES
        win32u)
    list(APPEND WOW64_I386_EXECUTABLES
        user32_winetest
        win32u_apitest
        win32u_winetest)
endif()
