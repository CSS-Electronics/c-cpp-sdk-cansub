/*
 * Copyright (c) 2018 Workaround GmbH.
 * Copyright (c) 2017 Intel Corporation.
 * Copyright (c) 2017 Nordic Semiconductor ASA
 * Copyright (c) 2015 Runtime Inc
 * Copyright (c) 2018 Google LLC.
 * Copyright (c) 2022 Meta
 * Copyright (c) 2024 Intercreate, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Adapted for CANsub library:
 * - Removed all Zephyr specific includes, replaced with common types and utilities.
 * - Replaced assert.
 * - Removed unused functions (for CANsub)
 */
/** @file
 * @brief CRC computation function
 */

#ifndef ZEPHYR_INCLUDE_SYS_CRC_H_
#define ZEPHYR_INCLUDE_SYS_CRC_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <assert.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Initial value expected to be used at the beginning of the crc8_ccitt
 * computation.
 */
#define CRC8_CCITT_INITIAL_VALUE 0xFF
#define CRC8_ROHC_INITIAL_VALUE  0xFF

/* Initial value expected to be used at the beginning of the OpenPGP CRC-24 computation. */
#define CRC24_PGP_INITIAL_VALUE 0x00B704CEU
/*
 * The CRC-24 value is stored on a 32-bit value, only the 3 least significant bytes
 * are meaningful. Use the following mask to only keep the CRC-24 value.
 */
#define CRC24_FINAL_VALUE_MASK 0x00FFFFFFU

/**
 * @defgroup checksum Checksum
 * @ingroup os_services
 */

/**
 * @defgroup crc CRC
 * @since 1.8
 * @version 1.0.0
 * @ingroup checksum
 * @{
 */

/**
 * @brief CRC polynomial definitions
 * @anchor CRC_POLYNOMIAL
 *
 * @{
 */

/** CRC4 polynomial */
#define CRC4_POLY 0x3

/** CRC4_TI polynomial */
#define CRC4_REFLECT_POLY 0xC

/** CRC7_BE polynomial */
#define CRC7_BE_POLY 0x09

/** CRC8 polynomial */
#define CRC8_POLY 0x07

/** CRC8_REFLECT polynomial */
#define CRC8_REFLECT_POLY 0xE0

/** CRC16 polynomial */
#define CRC16_POLY 0x8005

/** CRC16_ANSI polynomial */
#define CRC16_REFLECT_POLY 0xA001

/** CRC16_CCITT/CRC16_ITU_T polynomial */
#define CRC16_CCITT_POLY 0x1021

/**  CRC24_PGP polynomial */
#define CRC24_PGP_POLY 0x01864CFBU

/** CRC32_IEEE polynomial */
#define CRC32_IEEE_POLY 0x04C11DB7U

/** CRC32C polynomial */
#define CRC32C_POLY 0x1EDC6F41U

/** CRC32_K_4_2 polynomial */
#define CRC32K_4_2_POLY 0x93A409EBU

/** @} */

/**
 * @brief CRC algorithm enumeration
 *
 * These values should be used with the @ref crc dispatch function.
 */
enum crc_type {
	CRC4,        /**< Use @ref crc4 */
	CRC4_TI,     /**< Use @ref crc4_ti */
	CRC7_BE,     /**< Use @ref crc7_be */
	CRC8,        /**< Use @ref crc8 */
	CRC8_CCITT,  /**< Use @ref crc8_ccitt */
	CRC8_ROHC,   /**< Use @ref crc8_rohc */
	CRC16,       /**< Use @ref crc16 */
	CRC16_ANSI,  /**< Use @ref crc16_ansi */
	CRC16_CCITT, /**< Use @ref crc16_ccitt */
	CRC16_ITU_T, /**< Use @ref crc16_itu_t */
	CRC24_PGP,   /**< Use @ref crc24_pgp */
	CRC32_C,     /**< Use @ref crc32_c */
	CRC32_IEEE,  /**< Use @ref crc32_ieee */
	CRC32_K_4_2, /**< Use @ref crc32_k_4_2_update */
	CRC32_MPEG2, /**< Use @ref crc32_mpeg2 */
};

/**
 * @brief Generate IEEE conform CRC32 checksum.
 *
 * @param  data         Pointer to data on which the CRC should be calculated.
 * @param  len          Data length.
 *
 * @return CRC32 value.
 *
 */
uint32_t crc32_ieee(const uint8_t *data, size_t len);

/**
 * @brief Update an IEEE conforming CRC32 checksum.
 *
 * @param crc   CRC32 checksum that needs to be updated.
 * @param data  Pointer to data on which the CRC should be calculated.
 * @param len   Data length.
 *
 * @return CRC32 value.
 *
 */
uint32_t crc32_ieee_update(uint32_t crc, const uint8_t *data, size_t len);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif
