# Both compatibility runtimes share the portable DLL set. Only their
# architecture-specific loader and guest executables remain separate.
include("${CMAKE_CURRENT_LIST_DIR}/compat_runtime_targets.cmake")

set(WOW64_I386_MODULES ${COMPAT_RUNTIME_MODULES} ntdll)
set(WOW64_I386_AUXILIARY_MODULES ${COMPAT_RUNTIME_AUXILIARY_MODULES})
set(WOW64_I386_ALIASES ${COMPAT_RUNTIME_ALIASES})

set(WOW64_I386_EXECUTABLES
    glgears
    glmark2
    glmark2_runner
    msiexec
    notepad
    regsvr32
    wglgears_runner
    winver)

if(ENABLE_ROSTESTS)
    list(APPEND WOW64_I386_EXECUTABLES
        user32_winetest
        win32u_apitest
        win32u_winetest)
endif()

# Native ARM programs keep their canonical, unprefixed command names.  Give
# guest graphics programs architecture-qualified aliases so i386 (and AMD64
# payloads using the same convention) are unambiguous when launched manually.
set(WOW64_I386_ARCH_COMMAND_TARGETS)
foreach(_target IN ITEMS
    glgears
    glmark2
    glmark2_runner
    wglgears_runner)
    if(_target IN_LIST WOW64_I386_EXECUTABLES)
        list(APPEND WOW64_I386_ARCH_COMMAND_TARGETS "${_target}")
        list(APPEND WOW64_I386_ALIASES
            "${_target}=i386_${_target}${CMAKE_EXECUTABLE_SUFFIX}")
    endif()
endforeach()
