add_message_headers(UNICODE RESOURCE_ONLY mferror.mc)

add_library(mferror MODULE ${CMAKE_CURRENT_BINARY_DIR}/mferror.rc)
add_dependencies(mferror mferror_message_resources)
set_module_type(mferror win32dll ENTRYPOINT 0)
add_cd_file(TARGET mferror DESTINATION reactos/system32 FOR all)
