
spec2def(usp10.dll usp10.spec ADD_IMPORTLIB)

add_library(usp10 MODULE ${CMAKE_CURRENT_BINARY_DIR}/usp10.def)
set_target_properties(usp10 PROPERTIES LINKER_LANGUAGE C)
set_module_type(usp10 win32dll ENTRYPOINT 0)
add_dependencies(usp10 gdi32)
add_cd_file(TARGET usp10 DESTINATION reactos/system32 FOR all)
set_wine_module(usp10)
