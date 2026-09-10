# PROJECT:     ReactOS CSPRNG Library
# LICENSE:     MIT (https://spdx.org/licenses/MIT)
# PURPOSE:     User-mode interface to the kernel random number generator
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>

include_directories(${REACTOS_SOURCE_DIR}/sdk/include/reactos/drivers/ksecdd)

list(APPEND SOURCE
    csprng.c
    ${REACTOS_SOURCE_DIR}/dll/3rdparty/mbedtls/chacha20.c
    ${REACTOS_SOURCE_DIR}/dll/3rdparty/mbedtls/platform_util.c)

add_library(csprng ${SOURCE})
target_include_directories(csprng PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${REACTOS_SOURCE_DIR}/sdk/include/reactos/libs)
target_compile_definitions(csprng PRIVATE MBEDTLS_CONFIG_FILE="csprng_config.h")
add_dependencies(csprng psdk xdk)
