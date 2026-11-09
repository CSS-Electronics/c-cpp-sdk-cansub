/* SPDX-License-Identifier: MIT */

#ifndef CANSUB_MBEDTLS_CONFIG_H
#define CANSUB_MBEDTLS_CONFIG_H

/***********************************************************************************************************************
* Includes.
***********************************************************************************************************************/
/* Standard includes. */

/* Project includes. */

/***********************************************************************************************************************
* Public definitions.
***********************************************************************************************************************/

/* TLS 1.2 client, only the cipher suite the CANsub offers. */
#define MBEDTLS_SSL_TLS_C
#define MBEDTLS_SSL_CLI_C
#define MBEDTLS_SSL_PROTO_TLS1_2
#define MBEDTLS_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED
#define MBEDTLS_SSL_CIPHERSUITES MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256

/* The host is sent as the TLS server name, see cansub_transport_connected(). */
#define MBEDTLS_SSL_SERVER_NAME_INDICATION
#define MBEDTLS_SSL_EXTENDED_MASTER_SECRET

#define MBEDTLS_X509_USE_C
#define MBEDTLS_X509_CRT_PARSE_C

/* mbedtls_strerror() is used in cansub_error(). */
#define MBEDTLS_ERROR_C

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