# PROJECT:     ReactOS Build System
# PURPOSE:     Define AMD64 command payloads executed through FEX
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>

set(FEX_AMD64_COMMAND_TARGETS
    glgears
    glmark2
    glmark2_runner
    wglgears_runner)

set(FEX_AMD64_COMMAND_ALIASES)
foreach(_target IN LISTS FEX_AMD64_COMMAND_TARGETS)
    list(APPEND FEX_AMD64_COMMAND_ALIASES
        "${_target}=amd64_${_target}${CMAKE_EXECUTABLE_SUFFIX}")
endforeach()
