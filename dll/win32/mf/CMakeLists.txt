spec2def(mf.dll mf.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    clock.c
    copier.c
    evr.c
    main.c
    quality.c
    sac.c
    samplegrabber.c
    sar.c
    scheme_handler.c
    session.c
    topology.c
    topology_loader.c)

set_source_files_properties(mf.rc PROPERTIES OBJECT_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/mf.rgs)
add_library(mf MODULE ${SOURCE} mf.rc ${CMAKE_CURRENT_BINARY_DIR}/mf.def ${CMAKE_CURRENT_BINARY_DIR}/mf_stubs.c)
add_idl_reg_scripts(mf registry mf.idl)
set_module_type(mf win32dll)
target_compile_definitions(mf PRIVATE __WINESRC__)
target_link_libraries(mf mfuuid strmiids uuid wine wine_dll_canunload wine_dll_register)
add_importlibs(mf advapi32 mfplat ole32 kernelbase msvcrt kernel32 ntdll)
add_delay_importlibs(mf evr urlmon user32)
add_cd_file(TARGET mf DESTINATION reactos/system32 FOR all)
set_wine_module(mf)
