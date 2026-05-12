include_directories(
    ${REACTOS_SOURCE_DIR}/base/applications/mscutils/diskmgmt)

list(APPEND SOURCE
    SnapshotEnum.c
    SnapshotMatch.c
    testlist.c
    ${REACTOS_SOURCE_DIR}/base/applications/mscutils/diskmgmt/snapshotenum.c
    ${REACTOS_SOURCE_DIR}/base/applications/mscutils/diskmgmt/snapshotmatch.c)

add_executable(diskmgmt_apitest ${SOURCE})
target_link_libraries(diskmgmt_apitest wine ${PSEH_LIB})
set_module_type(diskmgmt_apitest win32cui)
add_importlibs(diskmgmt_apitest msvcrt kernel32 ntdll)
add_rostests_file(TARGET diskmgmt_apitest)
