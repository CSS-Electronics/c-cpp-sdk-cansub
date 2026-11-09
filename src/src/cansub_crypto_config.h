/* SPDX-License-Identifier: MIT */

#ifndef CANSUB_CRYPTO_CONFIG_H
#define CANSUB_CRYPTO_CONFIG_H

/***********************************************************************************************************************
* Includes.
***********************************************************************************************************************/
/* Standard includes. */

/* Project includes. */

/***********************************************************************************************************************
* Public definitions.
***********************************************************************************************************************/

#define PSA_WANT_ALG_ECDH                        1
#define PSA_WANT_ALG_ECDSA                       1
#define PSA_WANT_ALG_GCM                         1
#define PSA_WANT_ALG_SHA_1                       1      /* WebSocket upgrade (Sec-WebSocket-Accept), not used by TLS. */
#define PSA_WANT_ALG_SHA_256                     1
#define PSA_WANT_ALG_TLS12_PRF                   1
#define PSA_WANT_ECC_SECP_R1_256                 1
#define PSA_WANT_KEY_TYPE_AES                    1
#define PSA_WANT_KEY_TYPE_ECC_KEY_PAIR_BASIC     1
#define PSA_WANT_KEY_TYPE_ECC_KEY_PAIR_IMPORT    1
#define PSA_WANT_KEY_TYPE_ECC_KEY_PAIR_GENERATE  1

#define MBEDTLS_PSA_CRYPTO_C
#define MBEDTLS_PSA_BUILTIN_GET_ENTROPY
#define MBEDTLS_CTR_DRBG_C
#define MBEDTLS_PSA_KEY_STORE_DYNAMIC

#define MBEDTLS_HAVE_ASM
#define MBEDTLS_HAVE_TIME
#define MBEDTLS_HAVE_TIME_DATE

#define MBEDTLS_ASN1_PARSE_C
#define MBEDTLS_ASN1_WRITE_C
#define MBEDTLS_PK_C
#define MBEDTLS_PK_PARSE_C
#define MBEDTLS_BASE64_C
#define MBEDTLS_PEM_PARSE_C

/* Speed only. */
#define MBEDTLS_ECP_NIST_OPTIM
#define MBEDTLS_AESNI_C
#define MBEDTLS_AESCE_C

/* Threading. */
#define MBEDTLS_THREADING_C
#if defined(_WIN32)
#define MBEDTLS_THREADING_ALT
#else
#define MBEDTLS_THREADING_PTHREAD
#endif

/***********************************************************************************************************************
* Public type declarations.
***********************************************************************************************************************/

/***********************************************************************************************************************
* Public variable declarations.
***********************************************************************************************************************/

/***********************************************************************************************************************
* Public function declarations.
***********************************************************************************************************************/

#endif