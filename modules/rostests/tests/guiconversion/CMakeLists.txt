add_executable(guiconversion guiconversion.c)
set_module_type(guiconversion win32cui)
add_importlibs(guiconversion msvcrt kernel32)
add_rostests_file(TARGET guiconversion)
