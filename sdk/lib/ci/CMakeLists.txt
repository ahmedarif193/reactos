# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 Ahmed ARIF

# Use the existing Mbed TLS RSA/X.509 implementation with kernel allocation.
set(CI_MBEDTLS_SOURCE
    asn1parse.c bignum.c md.c md_wrap.c oid.c pk.c pk_wrap.c pkparse.c
    platform_util.c rsa.c rsa_internal.c sha1.c sha256.c sha512.c x509.c x509_crt.c)
list(TRANSFORM CI_MBEDTLS_SOURCE PREPEND "${REACTOS_SOURCE_DIR}/dll/3rdparty/mbedtls/")

add_library(ci STATIC authenticode.c ${CI_MBEDTLS_SOURCE})
target_include_directories(ci PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${REACTOS_SOURCE_DIR}/sdk/include/reactos/libs)
target_compile_definitions(ci PRIVATE MBEDTLS_CONFIG_FILE="ci_config.h")

if(CMAKE_C_COMPILER_ID STREQUAL "Clang")
    target_compile_options(ci PRIVATE -Wno-tautological-constant-compare)
endif()
