# PROJECT:     ReactOS CSPRNG Library
# LICENSE:     MIT (https://spdx.org/licenses/MIT)
# PURPOSE:     User-mode interface to the kernel random number generator
# COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>

include_directories(${REACTOS_SOURCE_DIR}/sdk/include/reactos/drivers/ksecdd)

list(APPEND SOURCE
    csprng.c)

add_library(csprng ${SOURCE})
add_dependencies(csprng psdk xdk)
