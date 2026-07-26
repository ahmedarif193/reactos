cmake_minimum_required(VERSION 3.16)

project(ntfslib_linux LANGUAGES CXX)

include(CTest)

option(NTFSLIB_DEBUG "Enable verbose ntfslib diagnostics" OFF)
option(NTFSLIB_BUILD_FUSE "Build the FUSE/image-inspection frontend" ON)
option(
    NTFSLIB_STATIC_CXX_RUNTIME
    "Link GNU C++ support into Linux test executables to avoid loader-dominated startup"
    ON
)

set(NTFSLIB_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")

add_library(ntfslib_linux STATIC
    "${NTFSLIB_ROOT}/env/linux/linux.cpp"
    "${NTFSLIB_ROOT}/src/btree/directory/directory.cpp"
    "${NTFSLIB_ROOT}/src/btree/directory/editdir.cpp"
    "${NTFSLIB_ROOT}/src/btree/directory/find.cpp"
    "${NTFSLIB_ROOT}/src/btree/directory/get.cpp"
    "${NTFSLIB_ROOT}/src/btree/btree.cpp"
    "${NTFSLIB_ROOT}/src/capi/btree.cpp"
    "${NTFSLIB_ROOT}/src/capi/filerecord.cpp"
    "${NTFSLIB_ROOT}/src/capi/lfs.cpp"
    "${NTFSLIB_ROOT}/src/capi/mft.cpp"
    "${NTFSLIB_ROOT}/src/capi/volume.cpp"
    "${NTFSLIB_ROOT}/src/filerecord/attributes/addattr.cpp"
    "${NTFSLIB_ROOT}/src/filerecord/attributes/findattr.cpp"
    "${NTFSLIB_ROOT}/src/filerecord/read/allocation.cpp"
    "${NTFSLIB_ROOT}/src/filerecord/read/compression.cpp"
    "${NTFSLIB_ROOT}/src/filerecord/read/copy.cpp"
    "${NTFSLIB_ROOT}/src/filerecord/read/ea.cpp"
    "${NTFSLIB_ROOT}/src/filerecord/read/reparse.cpp"
    "${NTFSLIB_ROOT}/src/filerecord/read/security.cpp"
    "${NTFSLIB_ROOT}/src/filerecord/read/streams.cpp"
    "${NTFSLIB_ROOT}/src/filerecord/read/wof.cpp"
    "${NTFSLIB_ROOT}/src/filerecord/write/basic.cpp"
    "${NTFSLIB_ROOT}/src/filerecord/write/create.cpp"
    "${NTFSLIB_ROOT}/src/filerecord/write/ea.cpp"
    "${NTFSLIB_ROOT}/src/filerecord/write/filedata.cpp"
    "${NTFSLIB_ROOT}/src/filerecord/write/filename.cpp"
    "${NTFSLIB_ROOT}/src/filerecord/write/reparse.cpp"
    "${NTFSLIB_ROOT}/src/filerecord/filerecord.cpp"
    "${NTFSLIB_ROOT}/src/filerecord/fixup.cpp"
    "${NTFSLIB_ROOT}/src/lfs/lfs.cpp"
    "${NTFSLIB_ROOT}/src/mft/get.cpp"
    "${NTFSLIB_ROOT}/src/mft/create.cpp"
    "${NTFSLIB_ROOT}/src/mft/mft.cpp"
    "${NTFSLIB_ROOT}/src/mft/namespace.cpp"
    "${NTFSLIB_ROOT}/src/NtfsIsNameInExpression.cpp"
    "${NTFSLIB_ROOT}/src/volume/metadata/attrdef.cpp"
    "${NTFSLIB_ROOT}/src/volume/metadata/bitmap.cpp"
    "${NTFSLIB_ROOT}/src/volume/metadata/upcase.cpp"
    "${NTFSLIB_ROOT}/src/volume/metadata/volume.cpp"
    "${NTFSLIB_ROOT}/src/volume/disk.cpp"
    "${NTFSLIB_ROOT}/src/volume/volume.cpp"
    "${NTFSLIB_ROOT}/src/util.cpp"
)

target_compile_features(ntfslib_linux PUBLIC cxx_std_17)
target_compile_definitions(ntfslib_linux
    PUBLIC
        NTFSLIB_PORTABLE=1
        _FILE_OFFSET_BITS=64
)
if(NTFSLIB_DEBUG)
    target_compile_definitions(ntfslib_linux PRIVATE NTFSLIB_DEBUG=1)
endif()

target_compile_options(ntfslib_linux PUBLIC
    -fshort-wchar
    -Wall
    -Wextra
    -Wno-multichar
)

target_include_directories(ntfslib_linux
    PUBLIC
        "${NTFSLIB_ROOT}/include/public"
        "${NTFSLIB_ROOT}/env/linux"
    PRIVATE
        "${NTFSLIB_ROOT}/include/internal"
)

if(NTFSLIB_BUILD_FUSE)
    add_subdirectory(fuse)
endif()

if(BUILD_TESTING)
    add_executable(ntfslib_posix_bench
        tests/posix_bench.cpp
    )
    target_compile_features(ntfslib_posix_bench PRIVATE cxx_std_17)
    target_compile_options(ntfslib_posix_bench PRIVATE
        -Wall
        -Wextra
    )

    add_executable(ntfslib_resident_update_test
        tests/resident_update.cpp
    )
    target_compile_features(ntfslib_resident_update_test PRIVATE cxx_std_17)
    target_compile_options(ntfslib_resident_update_test PRIVATE
        -Wall
        -Wextra
    )
    target_include_directories(ntfslib_resident_update_test PRIVATE
        "${NTFSLIB_ROOT}/include/internal"
    )
    target_link_libraries(ntfslib_resident_update_test PRIVATE ntfslib_linux)
    add_test(
        NAME ntfslib_linux_resident_update
        COMMAND ntfslib_resident_update_test
    )

    add_executable(ntfslib_lznt1_roundtrip_test
        tests/lznt1_roundtrip.cpp
    )
    target_compile_features(ntfslib_lznt1_roundtrip_test PRIVATE cxx_std_17)
    target_compile_options(ntfslib_lznt1_roundtrip_test PRIVATE
        -Wall
        -Wextra
    )
    target_include_directories(ntfslib_lznt1_roundtrip_test PRIVATE
        "${NTFSLIB_ROOT}/include/internal"
    )
    target_link_libraries(ntfslib_lznt1_roundtrip_test PRIVATE ntfslib_linux)
    add_test(
        NAME ntfslib_linux_lznt1_roundtrip
        COMMAND ntfslib_lznt1_roundtrip_test
    )
endif()

if(BUILD_TESTING AND NTFSLIB_BUILD_FUSE)
    find_program(MKNTFS_EXECUTABLE NAMES mkntfs mkfs.ntfs)
    find_program(NTFSCP_EXECUTABLE NAMES ntfscp)
    find_program(NTFSTRUNCATE_EXECUTABLE NAMES ntfstruncate)
    find_program(NTFSLS_EXECUTABLE NAMES ntfsls)
    find_program(NTFSLABEL_EXECUTABLE NAMES ntfslabel)
    find_program(NTFSFIX_EXECUTABLE NAMES ntfsfix)
    find_program(NTFSINFO_EXECUTABLE NAMES ntfsinfo)
    find_program(NTFSCAT_EXECUTABLE NAMES ntfscat)
    find_program(NTFSCLUSTER_EXECUTABLE NAMES ntfscluster)
    find_program(NTFS3G_EXECUTABLE NAMES ntfs-3g)
    find_program(FUSERMOUNT3_EXECUTABLE NAMES fusermount3)
    find_program(NTFSSECAUDIT_EXECUTABLE NAMES ntfssecaudit)
    find_program(FAKEROOT_EXECUTABLE NAMES fakeroot)
    find_program(XXD_EXECUTABLE NAMES xxd)
    find_library(WIM_LIBRARY NAMES wim libwim.so.15)
    if(WIM_LIBRARY)
        add_executable(ntfslib_wof_fixture
            tests/wof_fixture.cpp
        )
        target_compile_features(ntfslib_wof_fixture PRIVATE cxx_std_17)
        target_compile_options(ntfslib_wof_fixture PRIVATE
            -Wall
            -Wextra
        )
        target_link_libraries(ntfslib_wof_fixture PRIVATE "${WIM_LIBRARY}")
    endif()
    if(MKNTFS_EXECUTABLE AND
       NTFSCP_EXECUTABLE AND
       NTFSTRUNCATE_EXECUTABLE AND
       NTFSLS_EXECUTABLE AND
       NTFSCAT_EXECUTABLE AND
       NTFSINFO_EXECUTABLE)
        add_test(
            NAME ntfslib_linux_image_smoke
            COMMAND /bin/sh
                "${CMAKE_CURRENT_LIST_DIR}/tests/image_smoke.sh"
                "$<TARGET_FILE:ntfslib_fuse>"
                "${MKNTFS_EXECUTABLE}"
                "${NTFSCP_EXECUTABLE}"
                "${NTFSLIB_ROOT}/../../../../README.md"
                "${NTFSLIB_ROOT}/env/fl/CMakeLists.txt"
                "${NTFSLIB_ROOT}/env/fl/ntfs_fl.h"
                "${NTFSTRUNCATE_EXECUTABLE}"
                "${NTFSLS_EXECUTABLE}"
                "${NTFSCAT_EXECUTABLE}"
                "${NTFSINFO_EXECUTABLE}"
        )
    endif()
    if(MKNTFS_EXECUTABLE AND
       NTFSLABEL_EXECUTABLE AND
       NTFSFIX_EXECUTABLE)
        add_test(
            NAME ntfslib_linux_label_write
            COMMAND /bin/sh
                "${CMAKE_CURRENT_LIST_DIR}/tests/label_write_smoke.sh"
                "$<TARGET_FILE:ntfslib_fuse>"
                "${MKNTFS_EXECUTABLE}"
                "${NTFSLABEL_EXECUTABLE}"
                "${NTFSFIX_EXECUTABLE}"
        )
    endif()
    if(MKNTFS_EXECUTABLE AND
       NTFSCP_EXECUTABLE AND
       NTFSCAT_EXECUTABLE AND
       NTFSFIX_EXECUTABLE)
        add_test(
            NAME ntfslib_linux_data_write
            COMMAND /bin/sh
                "${CMAKE_CURRENT_LIST_DIR}/tests/data_write_smoke.sh"
                "$<TARGET_FILE:ntfslib_fuse>"
                "${MKNTFS_EXECUTABLE}"
                "${NTFSCP_EXECUTABLE}"
                "${NTFSCAT_EXECUTABLE}"
                "${NTFSFIX_EXECUTABLE}"
        )
        if(NTFSINFO_EXECUTABLE AND
           NTFSCLUSTER_EXECUTABLE)
            add_test(
                NAME ntfslib_linux_ads
                COMMAND /bin/sh
                    "${CMAKE_CURRENT_LIST_DIR}/tests/ads_smoke.sh"
                    "$<TARGET_FILE:ntfslib_fuse>"
                    "${MKNTFS_EXECUTABLE}"
                    "${NTFSCP_EXECUTABLE}"
                    "${NTFSCAT_EXECUTABLE}"
                    "${NTFSFIX_EXECUTABLE}"
                    "${NTFSINFO_EXECUTABLE}"
                    "${NTFSCLUSTER_EXECUTABLE}"
            )
            add_test(
                NAME ntfslib_linux_ea_write
                COMMAND /bin/sh
                    "${CMAKE_CURRENT_LIST_DIR}/tests/ea_write_smoke.sh"
                    "$<TARGET_FILE:ntfslib_fuse>"
                    "${MKNTFS_EXECUTABLE}"
                    "${NTFSCP_EXECUTABLE}"
                    "${NTFSCAT_EXECUTABLE}"
                    "${NTFSINFO_EXECUTABLE}"
                    "${NTFSFIX_EXECUTABLE}"
            )
            if(XXD_EXECUTABLE)
                add_test(
                    NAME ntfslib_linux_reparse_write
                    COMMAND /bin/sh
                        "${CMAKE_CURRENT_LIST_DIR}/tests/reparse_write_smoke.sh"
                        "$<TARGET_FILE:ntfslib_fuse>"
                        "${MKNTFS_EXECUTABLE}"
                        "${NTFSCP_EXECUTABLE}"
                        "${NTFSCAT_EXECUTABLE}"
                        "${NTFSINFO_EXECUTABLE}"
                        "${NTFSFIX_EXECUTABLE}"
                        "${XXD_EXECUTABLE}"
                )
            endif()
        endif()
        add_test(
            NAME ntfslib_linux_growth_write
            COMMAND /bin/sh
                "${CMAKE_CURRENT_LIST_DIR}/tests/growth_write_smoke.sh"
                "$<TARGET_FILE:ntfslib_fuse>"
                "${MKNTFS_EXECUTABLE}"
                "${NTFSCP_EXECUTABLE}"
                "${NTFSCAT_EXECUTABLE}"
                "${NTFSFIX_EXECUTABLE}"
        )
        add_test(
            NAME ntfslib_linux_truncate
            COMMAND /bin/sh
                "${CMAKE_CURRENT_LIST_DIR}/tests/truncate_smoke.sh"
                "$<TARGET_FILE:ntfslib_fuse>"
                "${MKNTFS_EXECUTABLE}"
                "${NTFSCP_EXECUTABLE}"
                "${NTFSCAT_EXECUTABLE}"
                "${NTFSFIX_EXECUTABLE}"
        )
    endif()
    if(MKNTFS_EXECUTABLE AND
       NTFSLS_EXECUTABLE AND
       NTFSCAT_EXECUTABLE AND
       NTFSINFO_EXECUTABLE AND
       NTFSFIX_EXECUTABLE AND
       NTFS3G_EXECUTABLE AND
       FUSERMOUNT3_EXECUTABLE)
        add_test(
            NAME ntfslib_linux_create
            COMMAND /bin/sh
                "${CMAKE_CURRENT_LIST_DIR}/tests/create_smoke.sh"
                "$<TARGET_FILE:ntfslib_fuse>"
                "${MKNTFS_EXECUTABLE}"
                "${NTFSLS_EXECUTABLE}"
                "${NTFSCAT_EXECUTABLE}"
                "${NTFSINFO_EXECUTABLE}"
                "${NTFSFIX_EXECUTABLE}"
                "${NTFS3G_EXECUTABLE}"
                "${FUSERMOUNT3_EXECUTABLE}"
        )
    endif()
    if(MKNTFS_EXECUTABLE AND
       NTFSLS_EXECUTABLE AND
       NTFSCAT_EXECUTABLE AND
       NTFSINFO_EXECUTABLE AND
       NTFSFIX_EXECUTABLE)
        add_test(
            NAME ntfslib_linux_namespace
            COMMAND /bin/sh
                "${CMAKE_CURRENT_LIST_DIR}/tests/namespace_smoke.sh"
                "$<TARGET_FILE:ntfslib_fuse>"
                "${MKNTFS_EXECUTABLE}"
                "${NTFSLS_EXECUTABLE}"
                "${NTFSCAT_EXECUTABLE}"
                "${NTFSINFO_EXECUTABLE}"
                "${NTFSFIX_EXECUTABLE}"
        )
        set_tests_properties(
            ntfslib_linux_namespace
            PROPERTIES TIMEOUT 600
        )
    endif()
    if(MKNTFS_EXECUTABLE AND
       NTFS3G_EXECUTABLE AND
       FUSERMOUNT3_EXECUTABLE AND
       NTFSCAT_EXECUTABLE AND
       NTFSCLUSTER_EXECUTABLE AND
       NTFSINFO_EXECUTABLE AND
       NTFSFIX_EXECUTABLE)
        add_test(
            NAME ntfslib_linux_sparse_write
            COMMAND /bin/sh
                "${CMAKE_CURRENT_LIST_DIR}/tests/sparse_write_smoke.sh"
                "$<TARGET_FILE:ntfslib_fuse>"
                "${MKNTFS_EXECUTABLE}"
                "${NTFS3G_EXECUTABLE}"
                "${FUSERMOUNT3_EXECUTABLE}"
                "${NTFSCAT_EXECUTABLE}"
                "${NTFSINFO_EXECUTABLE}"
                "${NTFSFIX_EXECUTABLE}"
                "${NTFSCLUSTER_EXECUTABLE}"
        )
        set_tests_properties(
            ntfslib_linux_sparse_write
            PROPERTIES SKIP_RETURN_CODE 77
        )
    endif()
    if(MKNTFS_EXECUTABLE AND
       NTFS3G_EXECUTABLE AND
       FUSERMOUNT3_EXECUTABLE AND
       NTFSLS_EXECUTABLE AND
       NTFSCAT_EXECUTABLE AND
       NTFSFIX_EXECUTABLE)
        add_test(
            NAME ntfslib_linux_mount_write
            COMMAND /bin/sh
                "${CMAKE_CURRENT_LIST_DIR}/tests/mount_write_smoke.sh"
                "$<TARGET_FILE:ntfslib_fuse>"
                "${MKNTFS_EXECUTABLE}"
                "${NTFS3G_EXECUTABLE}"
                "${FUSERMOUNT3_EXECUTABLE}"
                "${NTFSLS_EXECUTABLE}"
                "${NTFSCAT_EXECUTABLE}"
                "${NTFSFIX_EXECUTABLE}"
        )
        set_tests_properties(
            ntfslib_linux_mount_write
            PROPERTIES SKIP_RETURN_CODE 77
        )
    endif()
    if(MKNTFS_EXECUTABLE AND
       NTFSCP_EXECUTABLE AND
       NTFSLS_EXECUTABLE AND
       NTFSCAT_EXECUTABLE)
        add_test(
            NAME ntfslib_linux_fuzz
            COMMAND /bin/sh
                "${CMAKE_CURRENT_LIST_DIR}/tests/fuzz_smoke.sh"
                "$<TARGET_FILE:ntfslib_fuse>"
                "${MKNTFS_EXECUTABLE}"
                "${NTFSCP_EXECUTABLE}"
                "${NTFSLS_EXECUTABLE}"
                "${NTFSCAT_EXECUTABLE}"
        )
        set_tests_properties(
            ntfslib_linux_fuzz
            PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 900
        )
    endif()
    if(MKNTFS_EXECUTABLE AND
       NTFS3G_EXECUTABLE AND
       FUSERMOUNT3_EXECUTABLE AND
       NTFSCAT_EXECUTABLE AND
       NTFSFIX_EXECUTABLE)
        add_test(
            NAME ntfslib_linux_crash_consistency
            COMMAND /bin/sh
                "${CMAKE_CURRENT_LIST_DIR}/tests/crash_consistency_smoke.sh"
                "$<TARGET_FILE:ntfslib_fuse>"
                "${MKNTFS_EXECUTABLE}"
                "${NTFS3G_EXECUTABLE}"
                "${FUSERMOUNT3_EXECUTABLE}"
                "${NTFSCAT_EXECUTABLE}"
                "${NTFSFIX_EXECUTABLE}"
        )
        set_tests_properties(
            ntfslib_linux_crash_consistency
            PROPERTIES SKIP_RETURN_CODE 77
        )
    endif()
    if(MKNTFS_EXECUTABLE AND
       NTFS3G_EXECUTABLE AND
       FUSERMOUNT3_EXECUTABLE AND
       NTFSCAT_EXECUTABLE AND
       NTFSINFO_EXECUTABLE AND
       NTFSFIX_EXECUTABLE)
        add_test(
            NAME ntfslib_linux_compressed_write
            COMMAND /bin/sh
                "${CMAKE_CURRENT_LIST_DIR}/tests/compressed_write_smoke.sh"
                "$<TARGET_FILE:ntfslib_fuse>"
                "${MKNTFS_EXECUTABLE}"
                "${NTFS3G_EXECUTABLE}"
                "${FUSERMOUNT3_EXECUTABLE}"
                "${NTFSCAT_EXECUTABLE}"
                "${NTFSINFO_EXECUTABLE}"
                "${NTFSFIX_EXECUTABLE}"
        )
        set_tests_properties(
            ntfslib_linux_compressed_write
            PROPERTIES SKIP_RETURN_CODE 77
        )
    endif()
    if(MKNTFS_EXECUTABLE AND
       NTFSCP_EXECUTABLE AND
       NTFS3G_EXECUTABLE AND
       FUSERMOUNT3_EXECUTABLE AND
       NTFSFIX_EXECUTABLE)
        add_test(
            NAME ntfslib_linux_security_write
            COMMAND /bin/sh
                "${CMAKE_CURRENT_LIST_DIR}/tests/security_write_smoke.sh"
                "$<TARGET_FILE:ntfslib_fuse>"
                "${MKNTFS_EXECUTABLE}"
                "${NTFSCP_EXECUTABLE}"
                "${NTFS3G_EXECUTABLE}"
                "${FUSERMOUNT3_EXECUTABLE}"
                "${NTFSFIX_EXECUTABLE}"
        )
        set_tests_properties(
            ntfslib_linux_security_write
            PROPERTIES SKIP_RETURN_CODE 77
        )
    endif()
    if(MKNTFS_EXECUTABLE AND
       NTFSCP_EXECUTABLE AND
       NTFSCAT_EXECUTABLE AND
       NTFSFIX_EXECUTABLE AND
       NTFSINFO_EXECUTABLE AND
       XXD_EXECUTABLE AND
       WIM_LIBRARY)
        add_test(
            NAME ntfslib_linux_wof
            COMMAND /bin/sh
                "${CMAKE_CURRENT_LIST_DIR}/tests/wof_smoke.sh"
                "$<TARGET_FILE:ntfslib_fuse>"
                "$<TARGET_FILE:ntfslib_wof_fixture>"
                "${MKNTFS_EXECUTABLE}"
                "${NTFSCP_EXECUTABLE}"
                "${NTFSFIX_EXECUTABLE}"
                "${XXD_EXECUTABLE}"
                "${NTFSCAT_EXECUTABLE}"
                "${NTFSINFO_EXECUTABLE}"
        )
    endif()
    if(MKNTFS_EXECUTABLE AND
       NTFSCP_EXECUTABLE AND
       NTFSINFO_EXECUTABLE AND
       NTFSFIX_EXECUTABLE)
        add_test(
            NAME ntfslib_linux_basic_info
            COMMAND /bin/sh
                "${CMAKE_CURRENT_LIST_DIR}/tests/basic_info_smoke.sh"
                "$<TARGET_FILE:ntfslib_fuse>"
                "${MKNTFS_EXECUTABLE}"
                "${NTFSCP_EXECUTABLE}"
                "${NTFSINFO_EXECUTABLE}"
                "${NTFSFIX_EXECUTABLE}"
        )
    endif()
    if(MKNTFS_EXECUTABLE AND
       NTFSCP_EXECUTABLE AND
       NTFSCAT_EXECUTABLE AND
       NTFSSECAUDIT_EXECUTABLE AND
       FAKEROOT_EXECUTABLE AND
       XXD_EXECUTABLE)
        add_test(
            NAME ntfslib_linux_security
            COMMAND /bin/sh
                "${CMAKE_CURRENT_LIST_DIR}/tests/security_smoke.sh"
                "$<TARGET_FILE:ntfslib_fuse>"
                "${MKNTFS_EXECUTABLE}"
                "${NTFSCP_EXECUTABLE}"
                "${NTFSCAT_EXECUTABLE}"
                "${NTFSSECAUDIT_EXECUTABLE}"
                "${FAKEROOT_EXECUTABLE}"
                "${XXD_EXECUTABLE}"
        )
    endif()
endif()
