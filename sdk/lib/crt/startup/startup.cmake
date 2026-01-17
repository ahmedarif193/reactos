
list(APPEND CRT_STARTUP_SOURCE
    startup/_matherr.c
    startup/crtexe.c
    startup/wcrtexe.c
    startup/crt_handler.c
    startup/crtdll.c
    startup/_newmode.c
    startup/wildcard.c
    startup/tlssup.c
    startup/mingw_helpers.c
    startup/natstart.c
    startup/charmax.c
    startup/atonexit.c
    startup/dllmain.c
    startup/pesect.c
    startup/tlsmcrt.c
    startup/tlsthrd.c
    startup/tlsmthread.c
    startup/cinitexe.c
    startup/gs_support.c
    startup/dll_argv.c
    startup/dllargv.c
    startup/wdllargv.c
    # Note: crt0_c.c and crt0_w.c are NOT included here because they are
    # compiled directly into GUI applications via set_module_type() in
    # CMakeMacros.cmake. This avoids conflicts with console applications
    # that define their own main/wmain functions.
    startup/dllentry.c
    startup/reactos.c
)

if(MSVC)
    list(APPEND CRT_STARTUP_SOURCE
        startup/mscmain.c
        startup/threadSafeInit.c
    )
else()
    list(APPEND CRT_STARTUP_SOURCE
        startup/gccmain.c
        startup/pseudo-reloc.c
        startup/pseudo-reloc-list.c
    )
endif()
