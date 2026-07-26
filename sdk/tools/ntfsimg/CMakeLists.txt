set(NTFSLIB_ROOT ${REACTOS_SOURCE_DIR}/sdk/lib/fslib/ntfslib_new)

add_library(ntfslib_host STATIC
    ${NTFSLIB_ROOT}/env/linux/linux.cpp
    ${NTFSLIB_ROOT}/src/btree/directory/directory.cpp
    ${NTFSLIB_ROOT}/src/btree/directory/editdir.cpp
    ${NTFSLIB_ROOT}/src/btree/directory/find.cpp
    ${NTFSLIB_ROOT}/src/btree/directory/get.cpp
    ${NTFSLIB_ROOT}/src/btree/btree.cpp
    ${NTFSLIB_ROOT}/src/capi/btree.cpp
    ${NTFSLIB_ROOT}/src/capi/filerecord.cpp
    ${NTFSLIB_ROOT}/src/capi/lfs.cpp
    ${NTFSLIB_ROOT}/src/capi/mft.cpp
    ${NTFSLIB_ROOT}/src/capi/volume.cpp
    ${NTFSLIB_ROOT}/src/filerecord/attributes/addattr.cpp
    ${NTFSLIB_ROOT}/src/filerecord/attributes/findattr.cpp
    ${NTFSLIB_ROOT}/src/filerecord/read/allocation.cpp
    ${NTFSLIB_ROOT}/src/filerecord/read/compression.cpp
    ${NTFSLIB_ROOT}/src/filerecord/read/copy.cpp
    ${NTFSLIB_ROOT}/src/filerecord/read/ea.cpp
    ${NTFSLIB_ROOT}/src/filerecord/read/reparse.cpp
    ${NTFSLIB_ROOT}/src/filerecord/read/security.cpp
    ${NTFSLIB_ROOT}/src/filerecord/read/streams.cpp
    ${NTFSLIB_ROOT}/src/filerecord/read/wof.cpp
    ${NTFSLIB_ROOT}/src/filerecord/write/basic.cpp
    ${NTFSLIB_ROOT}/src/filerecord/write/create.cpp
    ${NTFSLIB_ROOT}/src/filerecord/write/ea.cpp
    ${NTFSLIB_ROOT}/src/filerecord/write/filedata.cpp
    ${NTFSLIB_ROOT}/src/filerecord/write/filename.cpp
    ${NTFSLIB_ROOT}/src/filerecord/write/reparse.cpp
    ${NTFSLIB_ROOT}/src/filerecord/filerecord.cpp
    ${NTFSLIB_ROOT}/src/filerecord/fixup.cpp
    ${NTFSLIB_ROOT}/src/lfs/lfs.cpp
    ${NTFSLIB_ROOT}/src/mft/get.cpp
    ${NTFSLIB_ROOT}/src/mft/create.cpp
    ${NTFSLIB_ROOT}/src/mft/mft.cpp
    ${NTFSLIB_ROOT}/src/mft/namespace.cpp
    ${NTFSLIB_ROOT}/src/NtfsIsNameInExpression.cpp
    ${NTFSLIB_ROOT}/src/volume/metadata/attrdef.cpp
    ${NTFSLIB_ROOT}/src/volume/metadata/bitmap.cpp
    ${NTFSLIB_ROOT}/src/volume/metadata/upcase.cpp
    ${NTFSLIB_ROOT}/src/volume/metadata/volume.cpp
    ${NTFSLIB_ROOT}/src/volume/disk.cpp
    ${NTFSLIB_ROOT}/src/volume/volume.cpp
    ${NTFSLIB_ROOT}/src/util.cpp)

target_compile_features(ntfslib_host PUBLIC cxx_std_17)
target_compile_definitions(ntfslib_host PUBLIC
    NTFSLIB_PORTABLE=1
    _FILE_OFFSET_BITS=64)
target_compile_options(ntfslib_host PUBLIC
    -fshort-wchar
    -Wno-multichar)
target_include_directories(ntfslib_host
    PUBLIC
        ${NTFSLIB_ROOT}/include/public
        ${NTFSLIB_ROOT}/env/linux
    PRIVATE
        ${NTFSLIB_ROOT}/include/internal)

add_library(ntfslib_format_host STATIC
    ${NTFSLIB_ROOT}/src/format/bootsect.cpp
    ${NTFSLIB_ROOT}/src/format/format.cpp
    ${NTFSLIB_ROOT}/src/format/record.cpp
    ${NTFSLIB_ROOT}/src/format/sysfiles.cpp
    ${NTFSLIB_ROOT}/src/format/tables.cpp)
target_compile_features(ntfslib_format_host PUBLIC cxx_std_17)
target_compile_definitions(ntfslib_format_host PUBLIC
    NTFSLIB_PORTABLE=1
    _FILE_OFFSET_BITS=64)
target_compile_options(ntfslib_format_host PUBLIC
    -fshort-wchar
    -Wno-multichar)
target_include_directories(ntfslib_format_host
    PUBLIC ${NTFSLIB_ROOT}/include/public
    PRIVATE ${NTFSLIB_ROOT}/src/format)

add_host_tool(ntfsimg
    ${NTFSLIB_ROOT}/linux/fuse/ntfslib_fuse.cpp)
target_compile_definitions(ntfsimg PRIVATE NTFSLIB_HAVE_FUSE=0)
target_link_libraries(ntfsimg PRIVATE ntfslib_host ntfslib_format_host)
