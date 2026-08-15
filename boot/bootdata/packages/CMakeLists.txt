#reactos.dff

# reactos.dff is the concatenation of two files:
# - reactos.dff.in, which is configured for the target architecture and can be
#   altered to add custom modules/files to reactos.cab
# - reactos.dff.dyn (dyn as in dynamic) which is generated at generation
#   time by our cmake scripts (from reactos.dff.cmake, which contains
#   generator expressions)
# If you want to slip-stream anything into the bootcd, then you want to alter reactos.dff.in

# Idea taken from there : http://www.cmake.org/pipermail/cmake/2010-July/038028.html
set(REACTOS_DFF_ARCH_DIRECTORIES)

if(ARCH STREQUAL "i386")
    string(APPEND REACTOS_DFF_ARCH_DIRECTORIES [=[
56 = winsxs\x86_microsoft.windows.common-controls_6595b64144ccf1df_5.82.2600.2982_none_deadbeef
57 = winsxs\x86_microsoft.windows.common-controls_6595b64144ccf1df_6.0.2600.2982_none_deadbeef
58 = winsxs\x86_microsoft.windows.gdiplus_6595b64144ccf1df_1.1.7601.23038_none_deadbeef
61 = winsxs\x86_microsoft.windows.gdiplus_6595b64144ccf1df_1.0.14393.0_none_deadbeef
]=])
elseif(ARCH STREQUAL "amd64")
    if(ENABLE_WOW64)
        string(APPEND REACTOS_DFF_ARCH_DIRECTORIES [=[
56 = winsxs\x86_microsoft.windows.common-controls_6595b64144ccf1df_5.82.2600.2982_none_deadbeef
57 = winsxs\x86_microsoft.windows.common-controls_6595b64144ccf1df_6.0.2600.2982_none_deadbeef
58 = winsxs\x86_microsoft.windows.gdiplus_6595b64144ccf1df_1.1.7601.23038_none_deadbeef
61 = winsxs\x86_microsoft.windows.gdiplus_6595b64144ccf1df_1.0.14393.0_none_deadbeef
]=])
    endif()
    string(APPEND REACTOS_DFF_ARCH_DIRECTORIES [=[
66 = system32\spool\prtprocs\x64
67 = winsxs\amd64_microsoft.windows.common-controls_6595b64144ccf1df_5.82.2600.2982_none_deadbeef
68 = winsxs\amd64_microsoft.windows.common-controls_6595b64144ccf1df_6.0.2600.2982_none_deadbeef
69 = winsxs\amd64_microsoft.windows.gdiplus_6595b64144ccf1df_1.1.7601.23038_none_deadbeef
71 = winsxs\amd64_microsoft.windows.gdiplus_6595b64144ccf1df_1.0.14393.0_none_deadbeef
]=])
    if(ENABLE_WOW64)
        string(APPEND REACTOS_DFF_ARCH_DIRECTORIES [=[
82 = SysWOW64
]=])
    endif()
elseif(ARCH STREQUAL "arm")
    string(APPEND REACTOS_DFF_ARCH_DIRECTORIES [=[
72 = winsxs\arm_microsoft.windows.common-controls_6595b64144ccf1df_5.82.2600.2982_none_deadbeef
73 = winsxs\arm_microsoft.windows.common-controls_6595b64144ccf1df_6.0.2600.2982_none_deadbeef
74 = winsxs\arm_microsoft.windows.gdiplus_6595b64144ccf1df_1.1.7601.23038_none_deadbeef
76 = winsxs\arm_microsoft.windows.gdiplus_6595b64144ccf1df_1.0.14393.0_none_deadbeef
]=])
elseif(ARCH STREQUAL "arm64")
    string(APPEND REACTOS_DFF_ARCH_DIRECTORIES [=[
77 = winsxs\arm64_microsoft.windows.common-controls_6595b64144ccf1df_5.82.2600.2982_none_deadbeef
78 = winsxs\arm64_microsoft.windows.common-controls_6595b64144ccf1df_6.0.2600.2982_none_deadbeef
79 = winsxs\arm64_microsoft.windows.gdiplus_6595b64144ccf1df_1.1.7601.23038_none_deadbeef
81 = winsxs\arm64_microsoft.windows.gdiplus_6595b64144ccf1df_1.0.14393.0_none_deadbeef
84 = system32\spool\prtprocs\arm64
]=])
    if(ENABLE_FEX_ARM64EC)
        string(APPEND REACTOS_DFF_ARCH_DIRECTORIES [=[
85 = system32\arm64ec
]=])
    endif()
endif()

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/reactos.dff.in
    ${CMAKE_CURRENT_BINARY_DIR}/reactos.dff.base
    @ONLY)

file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/concat.cmake "
    file(READ \${SRC1} S1)
    file(READ \${SRC2} S2)
    file(WRITE \${DST} \"\${S1}\${S2}\")
")

file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/reactos.dff.cmake "")

# This generates reactos.dff.dyn by processing the generator expressions
file(GENERATE
     OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/reactos.dff.$<CONFIG>.dyn
     INPUT ${CMAKE_CURRENT_BINARY_DIR}/reactos.dff.cmake)

# This finalizes reactos.dff by concat-ing the two files: one generated and one static containing the optional file.
# please keep it this way as it permits to add files to reactos.dff.in without having to run cmake again
# and also avoids rebuilding reactos.cab in case nothing changes after a cmake run
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/reactos.dff
    COMMAND ${CMAKE_COMMAND} -D SRC1=${CMAKE_CURRENT_BINARY_DIR}/reactos.dff.base
                             -D SRC2=${CMAKE_CURRENT_BINARY_DIR}/reactos.dff.$<CONFIG>.dyn
                             -D DST=${CMAKE_CURRENT_BINARY_DIR}/reactos.dff.maydiff
                             -P ${CMAKE_CURRENT_BINARY_DIR}/concat.cmake
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                             ${CMAKE_CURRENT_BINARY_DIR}/reactos.dff.maydiff
                             ${CMAKE_CURRENT_BINARY_DIR}/reactos.dff
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/reactos.dff.base
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/reactos.dff.$<CONFIG>.dyn)

add_custom_command(
    COMMAND native-cabman -C ${CMAKE_CURRENT_BINARY_DIR}/reactos.dff -L ${CMAKE_CURRENT_BINARY_DIR} -I -P ${REACTOS_SOURCE_DIR}
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/reactos.dff native-cabman
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/reactos.inf)

add_custom_target(
    reactos_cab_inf DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/reactos.inf)

add_cd_file(
    TARGET reactos_cab_inf
    FILE ${CMAKE_CURRENT_BINARY_DIR}/reactos.inf
    DESTINATION reactos
    NO_CAB FOR bootcd regtest)
