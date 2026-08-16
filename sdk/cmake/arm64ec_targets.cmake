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
    bcrypt
    combase
    comctl32
    comdlg32
    dwmapi
    gdi32
    imm32
    kernel32
    kernelbase_ros
    libpng
    msctf
    msctfime
    msvcrt
    ntdll_chpe
    ole32
    oleaut32
    opengl32
    shell32
    setupapi
    ucrtbase
    user32
    userenv
    usp10
    winhttp
    winmm
    ws2_32)

# Additional names under which a runtime module must be shipped. Keep the
# implementation name because existing ReactOS modules import it directly.
set(ARM64EC_RUNTIME_ALIASES
    "kernelbase_ros=kernelbase.dll")
