spec2def(kdcom.dll ../kdcom.spec ADD_IMPORTLIB)

add_riscv_native_image(kdcom kdcom.dll 0 SOURCES stubs.c
    EXPORTS KdDebuggerInitialize0 KdDebuggerInitialize1 KdSave KdRestore
        KdD0Transition KdD3Transition KdReceivePacket KdSendPacket
    IMPORTS ntoskrnl.exe:KiRiscvUnimplemented)
target_compile_definitions(kdcom PRIVATE _NTSYSTEM_)
target_include_directories(kdcom PRIVATE ${REACTOS_SOURCE_DIR}/ntoskrnl/include)
add_dependencies(kdcom xdk psdk bugcodes)
add_cd_file(TARGET kdcom DESTINATION reactos/system32 NO_CAB FOR all)
