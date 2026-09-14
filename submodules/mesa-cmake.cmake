# SPDX-License-Identifier: GPL-3.0-or-later
# Common arguments for the independent llvm-mingw Mesa CMake build.
# Homebrew's bison is keg-only; Apple's /usr/bin/bison is too old for Mesa.
set(_mesa_bison_hints)
if(CMAKE_HOST_APPLE)
    list(APPEND _mesa_bison_hints /opt/homebrew/opt/bison/bin /usr/local/opt/bison/bin)
endif()
find_program(MESA_BISON NAMES bison HINTS ${_mesa_bison_hints} REQUIRED)
find_program(MESA_FLEX NAMES flex REQUIRED)
set(MESA_CMAKE_ARGS
    -DCMAKE_MAKE_PROGRAM:FILEPATH=${MESA_NINJA}
    -DCMAKE_SYSTEM_NAME:STRING=Windows
    -DCMAKE_SYSTEM_PROCESSOR:STRING=${MESA_CPU}
    -DCMAKE_C_COMPILER:FILEPATH=${MESA_CC}
    -DCMAKE_CXX_COMPILER:FILEPATH=${MESA_CXX}
    -DCMAKE_ASM_COMPILER:FILEPATH=${MESA_CC}
    -DCMAKE_RC_COMPILER:FILEPATH=${MESA_WINDRES}
    -DCMAKE_AR:FILEPATH=${MESA_AR}
    -DCMAKE_C_COMPILER_LAUNCHER:STRING=
    -DCMAKE_CXX_COMPILER_LAUNCHER:STRING=
    -DCMAKE_BUILD_TYPE:STRING=Release
    -DCMAKE_C_FLAGS:STRING=
    -DCMAKE_CXX_FLAGS:STRING=
    -DCMAKE_ASM_FLAGS:STRING=
    -DCMAKE_SHARED_LINKER_FLAGS:STRING=
    -DPython3_EXECUTABLE:FILEPATH=${MESA_PYTHON}
    -DBISON_EXECUTABLE:FILEPATH=${MESA_BISON}
    -DFLEX_EXECUTABLE:FILEPATH=${MESA_FLEX})
