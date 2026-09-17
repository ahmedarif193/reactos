
list(APPEND SOURCE
    AppContainer.c
    Broker.c
    Desktop.c
    Integrity.c
    JobLifecycle.c
    JobMemory.c
    JobUi.c
    KernelBase.c
    Mitigations.c
    PrivateNamespace.c
    RestrictedToken.c
    SecurityApi.c
    Uipi.c)

list(APPEND PCH_SKIP_SOURCE
    testlist.c)

add_executable(sandbox_apitest
    ${SOURCE}
    ${PCH_SKIP_SOURCE})

add_executable(sandbox_helper
    helper.c)

add_library(sandbox_unsigned MODULE
    unsigned.c)

target_link_libraries(sandbox_apitest wine ${PSEH_LIB})
set_module_type(sandbox_apitest win32cui)
add_importlibs(sandbox_apitest advapi32 user32 gdi32 userenv ole32 msvcrt kernel32 ntdll)
add_pch(sandbox_apitest precomp.h "${PCH_SKIP_SOURCE}")
add_dependencies(sandbox_apitest sandbox_helper sandbox_unsigned)

set_module_type(sandbox_helper win32gui ENTRYPOINT mainCRTStartup)
add_importlibs(sandbox_helper msvcrt kernel32 ntdll)

set_module_type(sandbox_unsigned win32dll ENTRYPOINT DllMain 12)
add_importlibs(sandbox_unsigned kernel32)

add_rostests_file(TARGET sandbox_apitest)
add_rostests_file(TARGET sandbox_helper)
add_rostests_file(TARGET sandbox_unsigned)
