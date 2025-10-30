cmake_minimum_required(VERSION 3.16)

add_definitions(-D_BCRYPTPRIMITIVES_)

spec2def(bcryptprimitives.dll bcryptprimitives.spec ADD_IMPORTLIB)

add_library(bcryptprimitives MODULE
    bcryptprimitives.c
    ${CMAKE_CURRENT_BINARY_DIR}/bcryptprimitives.def)

# This stub has no CRT and no DllMain; avoid requiring _DllMainCRTStartup@12
set_module_type(bcryptprimitives win32dll ENTRYPOINT 0)
set_subsystem(bcryptprimitives console)

add_importlibs(bcryptprimitives ntdll)
add_cd_file(TARGET bcryptprimitives DESTINATION reactos/system32 FOR all)
