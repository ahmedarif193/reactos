if(ARCH STREQUAL "amd64")
    # On amd64, source a 32-bit binary from the multilib subbuild and
    # package it into SysWOW64. Ensure it builds via the subbuild stage.
    if(WOW64_MULTILIB AND WOW64_MULTILIB_SYSROOT)
        add_cd_file(
            TARGET wow64_multilib_stage
            FILE ${REACTOS_TOP_BINARY_DIR}/_wow64_i386/base/applications/cmdutils/wow64smoke/wow64smoke.exe
            DESTINATION reactos/SysWOW64
            FOR all)
    endif()
    return()
else()
    add_executable(wow64smoke wow64smoke.c wow64smoke.rc)
    set_module_type(wow64smoke win32cui)
    add_importlibs(wow64smoke msvcrt kernel32)
    add_cd_file(TARGET wow64smoke DESTINATION reactos/system32 FOR all)
endif()
