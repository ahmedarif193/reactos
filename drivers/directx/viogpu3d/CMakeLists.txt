# viogpu3d.sys -- Red Hat VirtIO GPU WDDM 3D miniport
#
# This target builds directly from the vendored upstream PR #943 sources in
# .vendor/kvm-guest-drivers-windows-pr943. It is intentionally kept close to
# upstream so compiler errors surface real ReactOS compatibility gaps.

set(VIOGPU3D_VENDOR_DIR ${REACTOS_SOURCE_DIR}/.vendor/kvm-guest-drivers-windows-pr943/viogpu)
set(VIOGPU3D_COMMON_DIR ${VIOGPU3D_VENDOR_DIR}/common)
set(VIOGPU3D_SHARED_DIR ${VIOGPU3D_VENDOR_DIR}/shared)
set(VIOGPU3D_SRC_DIR ${VIOGPU3D_VENDOR_DIR}/viogpu3d)
set(VIOGPU3D_UMD_SCRIPT ${REACTOS_SOURCE_DIR}/scripts/build-virtio-gpu-umd.sh)
set(VIOGPU3D_UMD_BUILD_DIR ${REACTOS_BINARY_DIR}/mesa-virgl-umd-build)
set(VIOGPU3D_WGL_UMD ${VIOGPU3D_UMD_BUILD_DIR}/src/gallium/targets/wgl/viogpu_wgl.dll)
get_filename_component(VIOGPU3D_LLVM_MINGW_BINDIR ${CMAKE_C_COMPILER} DIRECTORY)

add_custom_command(
    OUTPUT ${VIOGPU3D_WGL_UMD}
    COMMAND ${CMAKE_COMMAND} -E env
            LLVM_MINGW_BINDIR=${VIOGPU3D_LLVM_MINGW_BINDIR}
            ${VIOGPU3D_UMD_SCRIPT} ${REACTOS_BINARY_DIR}
    DEPENDS
        ${VIOGPU3D_UMD_SCRIPT}
        ${REACTOS_SOURCE_DIR}/.vendor/mesa/meson.build
        ${REACTOS_SOURCE_DIR}/scripts/mesa-llvm-mingw-x86_64.ini
    USES_TERMINAL
    VERBATIM)

add_custom_target(viogpu3d_umd DEPENDS ${VIOGPU3D_WGL_UMD})

list(APPEND SOURCE
    ${VIOGPU3D_COMMON_DIR}/baseobj.cpp
    ${VIOGPU3D_COMMON_DIR}/bitops.cpp
    ${VIOGPU3D_COMMON_DIR}/edid.cpp
    ${VIOGPU3D_COMMON_DIR}/viogpu_idr.cpp
    ${VIOGPU3D_COMMON_DIR}/viogpu_pci.cpp
    ${VIOGPU3D_COMMON_DIR}/viogpu_queue.cpp
    ${VIOGPU3D_SRC_DIR}/driver.cpp
    ${VIOGPU3D_SRC_DIR}/viogpu_allocation.cpp
    ${VIOGPU3D_SRC_DIR}/viogpu_adapter.cpp
    ${VIOGPU3D_SRC_DIR}/viogpu_command.cpp
    ${VIOGPU3D_SRC_DIR}/viogpu_device.cpp
    ${VIOGPU3D_SRC_DIR}/viogpu_vidpn.cpp)

add_library(viogpu3d MODULE ${SOURCE})

target_include_directories(viogpu3d PRIVATE
    ${VIOGPU3D_COMMON_DIR}
    ${VIOGPU3D_SHARED_DIR}
    ${VIOGPU3D_SRC_DIR}
    ${REACTOS_SOURCE_DIR}/sdk/lib/drivers/virtio)

target_compile_definitions(viogpu3d PRIVATE
    VIOGPU_3D=1)

target_compile_options(viogpu3d PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:-include${CMAKE_CURRENT_SOURCE_DIR}/viogpu3d_reactos.h>"
    "$<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>"
    "$<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>"
    -Wno-missing-field-initializers
    -Wno-unused-parameter
    -Wno-write-strings
    -Wno-narrowing
    -fpermissive)

set_module_type(viogpu3d kernelmodedriver)
target_link_libraries(viogpu3d virtio)
add_importlibs(viogpu3d dxgkrnl ntoskrnl hal wmilib)
add_cd_file(TARGET viogpu3d DESTINATION reactos/system32/drivers FOR all)
set(VIOGPU3D_D3D10_UMD ${VIOGPU3D_UMD_BUILD_DIR}/src/gallium/targets/d3d10umd/viogpu_d3d10.dll)
add_cd_file(FILE ${VIOGPU3D_WGL_UMD} TARGET viogpu3d_umd DESTINATION reactos/system32 FOR livecd)
add_cd_file(FILE ${VIOGPU3D_D3D10_UMD} TARGET viogpu3d_umd DESTINATION reactos/system32 FOR livecd)
