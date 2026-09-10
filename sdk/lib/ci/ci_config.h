/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright 2026 Ahmed ARIF */
#ifndef _ROS_CI_MBEDTLS_CONFIG_H_
#define _ROS_CI_MBEDTLS_CONFIG_H_

#include <reactos/ci.h>
#include <stdio.h>

#define MBEDTLS_PLATFORM_C
#define MBEDTLS_PLATFORM_MEMORY
#define MBEDTLS_PLATFORM_NO_STD_FUNCTIONS
#define MBEDTLS_PLATFORM_CALLOC_MACRO CiAllocate
#define MBEDTLS_PLATFORM_FREE_MACRO CiFree
#define MBEDTLS_PLATFORM_SNPRINTF_MACRO snprintf
#define MBEDTLS_ASN1_PARSE_C
#define MBEDTLS_BIGNUM_C
#define MBEDTLS_MD_C
#define MBEDTLS_OID_C
#define MBEDTLS_PK_C
#define MBEDTLS_PK_PARSE_C
#define MBEDTLS_PKCS1_V15
#define MBEDTLS_PKCS1_V21
#define MBEDTLS_RSA_C
#define MBEDTLS_SHA1_C
#define MBEDTLS_SHA256_C
#define MBEDTLS_SHA512_C
#define MBEDTLS_X509_USE_C
#define MBEDTLS_X509_CRT_PARSE_C
#define MBEDTLS_X509_CHECK_KEY_USAGE
#define MBEDTLS_X509_CHECK_EXTENDED_KEY_USAGE

/* Verification uses an explicitly supplied time, including trusted timestamps. */
#include <mbedtls/check_config.h>
#endif
