list(APPEND SOURCE
    FeatureStaging.c
    ForwardedOrdinals.c
    RegistryHelpers.c
    Win81ExportSurface.c
    testlist.c)

add_executable(shcore_apitest ${SOURCE})
set_module_type(shcore_apitest win32cui)
target_link_libraries(shcore_apitest ${PSEH_LIB} uuid)
add_importlibs(shcore_apitest shlwapi ole32 user32 advapi32 msvcrt kernel32)
add_rostests_file(TARGET shcore_apitest)
