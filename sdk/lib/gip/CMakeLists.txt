# SPDX-License-Identifier: MIT
add_library(gip STATIC gip.c)
target_include_directories(gip PUBLIC ${REACTOS_SOURCE_DIR}/sdk/include/reactos/libs/gip)
