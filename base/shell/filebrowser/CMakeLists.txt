
PROJECT(SHELL)

list(APPEND SOURCE
    filebrowser.c
    filebrowser.rc)

add_executable(filebrowser ${SOURCE})
# Note: This app uses wmain (via _tmain), not wWinMain, so it must be win32cui
set_module_type(filebrowser win32cui UNICODE)
add_importlibs(filebrowser shell32 browseui msvcrt kernel32)

add_cd_file(TARGET filebrowser DESTINATION reactos FOR all)
