/* SPDX-License-Identifier: MIT */

#ifndef CANSUB_THREADING_ALT_H
#define CANSUB_THREADING_ALT_H

/***********************************************************************************************************************
* Includes.
***********************************************************************************************************************/
/* Standard includes. */

/* Project includes. */

/***********************************************************************************************************************
* Public definitions.
***********************************************************************************************************************/

/***********************************************************************************************************************
* Public type declarations.
***********************************************************************************************************************/

/* Included by mbedtls/threading.h when MBEDTLS_THREADING_ALT is set, which cansub_crypto_config.h does on Windows.
 *
 * The types stand in for SRWLOCK and CONDITION_VARIABLE, which both hold a single pointer. Declaring them opaque keeps
 * <windows.h> out of every source file including an mbedTLS header. threading_alt.c checks that the layouts match. */

typedef struct mbedtls_platform_mutex_t
{
    void *opaque;
} mbedtls_platform_mutex_t;

typedef struct mbedtls_platform_condition_variable_t
{
    void *opaque;
} mbedtls_platform_condition_variable_t;

/***********************************************************************************************************************
* Public variable declarations.
***********************************************************************************************************************/

/***********************************************************************************************************************
* Public function declarations.
***********************************************************************************************************************/

/**
 * Register the native Windows threading primitives with mbedTLS, which also initializes its global mutexes.
 *
 * Must be called before any other mbedTLS function, and paired with mbedtls_threading_free_alt() after the last one.
 */
void cansub_threading_alt_set(void);

#endif
