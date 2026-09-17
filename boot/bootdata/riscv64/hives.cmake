# The on-disk hive format is shared with the other ports. Build the existing
# host generator with its host headers, never the target's NT/ELF ABI flags.
if(POLICY CMP0116)
    cmake_policy(SET CMP0116 NEW)
endif()

set(_hive_sources)
foreach(_name binhive cmi mkhive reginf registry rtl)
    list(APPEND _hive_sources "sdk/tools/mkhive/${_name}.c")
endforeach()
foreach(_name cmcheck cminit cmheal cmindex cmkeydel cmname cmse cmvalue
        hivebin hivecell hiveinit hivesum hivewrt)
    list(APPEND _hive_sources "sdk/lib/cmlib/${_name}.c")
endforeach()
foreach(_name infcore infget infput infhostgen infhostget infhostput infhostrtl)
    list(APPEND _hive_sources "sdk/lib/inflib/${_name}.c")
endforeach()
foreach(_name casemap string wctype)
    list(APPEND _hive_sources "sdk/tools/unicode/${_name}.c")
endforeach()
set(_hive_objects)
foreach(_source IN LISTS _hive_sources)
    set(_object "${REACTOS_BINARY_DIR}/host-tools/mkhive/${_source}.o")
    get_filename_component(_object_dir "${_object}" DIRECTORY)
    add_custom_command(OUTPUT "${_object}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_object_dir}"
        COMMAND ${RISCV_HOST_CC} -O2 -fshort-wchar -Wno-multichar -D__REACTOS__
            -D_NTSYSTEM_ -DNASSERT -DMKHIVE_HOST -DINFLIB_HOST -D__NO_CTYPE_INLINES -DTARGET_riscv64
            -DDECLSPEC_HIDDEN= -DNTDDI_VERSION=${REACTOS_TARGET_NTDDI}
            -I "${REACTOS_SOURCE_DIR}/sdk/include/host"
            -I "${REACTOS_SOURCE_DIR}/sdk/lib/cmlib"
            -I "${REACTOS_SOURCE_DIR}/sdk/lib/inflib"
            -I "${REACTOS_SOURCE_DIR}/sdk/lib/rtl"
            -DCMLIB_HOST= -MMD -MF "${_object}.d" -MT "${_object}"
            -c "${REACTOS_SOURCE_DIR}/${_source}" -o "${_object}"
        DEPENDS "${REACTOS_SOURCE_DIR}/${_source}" DEPFILE "${_object}.d" VERBATIM)
    list(APPEND _hive_objects "${_object}")
endforeach()
add_custom_command(OUTPUT "${RISCV_HOST_BIN}/mkhive"
    COMMAND ${RISCV_HOST_CC} ${_hive_objects} -o "${RISCV_HOST_BIN}/mkhive"
    DEPENDS ${_hive_objects} VERBATIM)
add_custom_target(riscv_host_mkhive DEPENDS "${RISCV_HOST_BIN}/mkhive")

find_program(RISCV_HOST_CXX NAMES c++ g++ REQUIRED)
add_custom_command(OUTPUT "${RISCV_HOST_BIN}/utf16le"
    COMMAND ${RISCV_HOST_CXX} -O2 "${REACTOS_SOURCE_DIR}/sdk/tools/utf16le/utf16le.cpp"
        -o "${RISCV_HOST_BIN}/utf16le"
    DEPENDS "${REACTOS_SOURCE_DIR}/sdk/tools/utf16le/utf16le.cpp" VERBATIM)

set(_hive_dir "${REACTOS_BINARY_DIR}/boot/bootdata")
file(MAKE_DIRECTORY "${_hive_dir}")
set(_hive_infs hivecls hivedef hivesft hivesys)
list(APPEND _hive_infs hivesys_riscv64)
if(REACTOS_TARGET_NT GREATER_EQUAL 0xA00)
    list(APPEND _hive_infs hivent10)
endif()
list(APPEND _hive_infs livecd caroots hiveinst)
set(_hive_inputs)
foreach(_inf IN LISTS _hive_infs)
    set(_input "${REACTOS_SOURCE_DIR}/boot/bootdata/${_inf}.inf")
    set(_output "${_hive_dir}/${_inf}_utf16.inf")
    add_custom_command(OUTPUT "${_output}"
        COMMAND "${RISCV_HOST_BIN}/utf16le" "${_input}" "${_output}"
        DEPENDS "${RISCV_HOST_BIN}/utf16le" "${_input}" VERBATIM)
    list(APPEND _hive_inputs "${_output}")
endforeach()
set(_hive_outputs)
foreach(_hive system software default sam security)
    list(APPEND _hive_outputs "${_hive_dir}/${_hive}")
endforeach()
add_custom_command(OUTPUT ${_hive_outputs}
    COMMAND "${RISCV_HOST_BIN}/mkhive" -h:SYSTEM,SOFTWARE,DEFAULT,SAM,SECURITY
        "-d:${_hive_dir}" ${_hive_inputs}
    DEPENDS riscv_host_mkhive "${RISCV_HOST_BIN}/mkhive" ${_hive_inputs} VERBATIM)
add_custom_target(livecd_hives DEPENDS ${_hive_outputs})
