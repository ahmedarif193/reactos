# PROJECT:     ReactOS
# LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
# PURPOSE:     Build KMDF class-extension client stubs and class libraries
# COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>

function(add_wdfcx_stub _target _source)
    add_library(${_target} STATIC ${_source})
    add_dependencies(${_target} xdk)
    target_compile_definitions(${_target} PRIVATE WDF_DEVICE_NO_WDMSEC_H)
    target_include_directories(${_target} PRIVATE
        ${REACTOS_SOURCE_DIR}/sdk/lib/drivers/wdf
        ${REACTOS_SOURCE_DIR}/sdk/lib/drivers/wdf/shared/inc/private/common
        ${REACTOS_SOURCE_DIR}/sdk/lib/drivers/wdf/kmdf/inc/private
        ${REACTOS_SOURCE_DIR}/sdk/include/wdf/kmdf/1.17)
endfunction()

add_wdfcx_stub(msgpioclxstub gpioclxstub.c)
add_wdfcx_stub(spbcxstubs spbcxstub.c)
add_wdfcx_stub(sercx2stubs sercx2stub.c)

function(add_wdfcx_library _target _source)
    add_library(${_target} MODULE ${_source})
    set_module_type(${_target} kmdfdriver KMDF_VERSION 1.33)
    target_include_directories(${_target} PRIVATE
        ${REACTOS_SOURCE_DIR}/sdk/lib/drivers/wdf
        ${REACTOS_SOURCE_DIR}/sdk/lib/drivers/wdf/shared/inc/private/common
        ${REACTOS_SOURCE_DIR}/sdk/lib/drivers/wdf/kmdf/inc/private)
    target_link_libraries(${_target} ntoskrnl_vista libcntpr ${PSEH_LIB})
    add_importlibs(${_target} ntoskrnl hal)
    add_cd_file(TARGET ${_target} DESTINATION reactos/system32/drivers FOR all)
endfunction()

add_wdfcx_library(msgpioclx gpioclx.c)
add_wdfcx_library(spbcx spbcx.c)
add_wdfcx_library(sercx2 sercx2.c)

add_registry_inf(wdfcx_reg.inf)
