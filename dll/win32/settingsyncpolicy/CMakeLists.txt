
spec2def(settingsyncpolicy.dll settingsyncpolicy.spec ADD_IMPORTLIB)

list(APPEND SOURCE
    settingsyncpolicy_main.c
    ${CMAKE_CURRENT_BINARY_DIR}/settingsyncpolicy.def)

add_library(settingsyncpolicy MODULE ${SOURCE})
set_module_type(settingsyncpolicy win32dll ENTRYPOINT DllMain 12)
add_importlibs(settingsyncpolicy msvcrt kernel32 ntdll)
add_cd_file(TARGET settingsyncpolicy DESTINATION reactos/system32 FOR all)
