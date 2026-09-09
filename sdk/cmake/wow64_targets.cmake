# Both compatibility runtimes share the portable DLL set. Only their
# architecture-specific loader and guest executables remain separate.
include("${CMAKE_CURRENT_LIST_DIR}/compat_runtime_targets.cmake")

set(WOW64_I386_MODULES ${COMPAT_RUNTIME_MODULES} ntdll)
set(WOW64_I386_AUXILIARY_MODULES ${COMPAT_RUNTIME_AUXILIARY_MODULES})
set(WOW64_I386_ALIASES ${COMPAT_RUNTIME_ALIASES})

set(WOW64_I386_EXECUTABLES
    notepad
    regsvr32
    winver)

if(ENABLE_ROSTESTS)
    list(APPEND WOW64_I386_EXECUTABLES
        user32_winetest
        win32u_apitest
        win32u_winetest)
endif()
