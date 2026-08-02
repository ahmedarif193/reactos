
add_typelib(std_ole_v2.idl)
spec2def(stdole2.tlb stdole2.tlb.spec)

list(APPEND SOURCE
    rsrc.rc
    ${CMAKE_CURRENT_BINARY_DIR}/stdole2.def)

add_library(stdole2.tlb MODULE ${SOURCE})
set_module_type(stdole2.tlb module)
add_idl_reg_scripts(stdole2.tlb regtypelib std_ole_v2.idl)
add_dependencies(stdole2.tlb xdk)
set_target_properties(stdole2.tlb PROPERTIES SUFFIX "")
add_cd_file(TARGET stdole2.tlb DESTINATION reactos/system32 FOR all)
set_wine_module(stdole2.tlb)
