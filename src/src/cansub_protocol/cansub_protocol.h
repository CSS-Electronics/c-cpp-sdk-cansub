/* SPDX-License-Identifier: MIT */

#ifndef CAN_INTERFACE_CANSUB_PROTOCOL_H
#define CAN_INTERFACE_CANSUB_PROTOCOL_H

/***********************************************************************************************************************
* Includes.
***********************************************************************************************************************/
/* Standard includes. */

/* Zephyr includes. */
#include <zephyr/net_buf.h>

/* Project includes. */
#include "cansub/cansub_ffi.h"
#include "../common.h"

/***********************************************************************************************************************
* Public definitions.
***********************************************************************************************************************/
/* The encoded can-frame timestamp is offset by date 2025/01/01 00:00:00 UTC. */
#define CANSUB_PROTOCOL_TIME_BASE_S (1735689600ULL)
#define CANSUB_PROTOCOL_TIME_BASE_US (CANSUB_PROTOCOL_TIME_BASE_S * 1000000)

#define HDLC_BOUNDARY_BYTE  (0x7E)
#define HDLC_ESCAPE_BYTE    (0x7D)

#define MSG_ENCODE_ERR_HEADER_SIZE   (7)
#define MSG_ENCODE_STD_HEADER_SIZE   (7+2)
#define MSG_ENCODE_EXT_HEADER_SIZE   (7+4)
#define MSG_ENCODE_HEADER_MIN_SIZE MIN(MSG_ENCODE_ERR_HEADER_SIZE, MIN(MSG_ENCODE_STD_HEADER_SIZE, MSG_ENCODE_EXT_HEADER_SIZE))
#define MSG_ENCODE_HEADER_MAX_SIZE MAX(MSG_ENCODE_ERR_HEADER_SIZE, MAX(MSG_ENCODE_STD_HEADER_SIZE, MSG_ENCODE_EXT_HEADER_SIZE))

#define MSG_ENCODE_MAX_SIZE( X ) ( MSG_ENCODE_HEADER_MAX_SIZE + ( X ) )

#define CAN_STD_ID_MSK      (0x000007FF)
#define CAN_EXT_ID_MSK      (0x1FFFFFFF)
#define CAN_MAX_DLC         (8)
#define CANFD_MAX_DLC       (15)

/***********************************************************************************************************************
* Public type declarations.
***********************************************************************************************************************/
typedef void (*cansub_protocol_decode_cb_t)(cansub_frame *msg, void* user_data);

/***********************************************************************************************************************
* Public variable declarations.
***********************************************************************************************************************/

/***********************************************************************************************************************
* Public function declarations.
***********************************************************************************************************************/
/**
 * Encodes msg into data. Can be called multiple times to encode multiple messages in the same data buffer. The
 * boundary bytes (start/stop), byte stuffing and, CRC32 will be updated to ensure that the frame in the data buffer
 * is always valid.
 * @param msg Message to encode.
 * @param data Frame data buffer. Can contain already encoded messages.
 * @return <0 on error, else number of bytes in data buffer.
 */
int cansub_protocol_encode(cansub_frame const *msg, struct net_buf_simple *data);

/**
 * Decodes data in buffer and calls msg_cb for each parsed message. Will consume as many bytes as possible.
 * @param data Frame data buffer. Can contain incomplete frames.
 * @param msg_cb Function to call for each parsed message.
 * @param user_data User data pointer provided to the call-back function.
 * @return <0 on error.
 */
int cansub_protocol_decode(struct net_buf_simple *data, cansub_protocol_decode_cb_t msg_cb, void* user_data);

/**
 * Maps DLC to bytes while clamping DLC to valid range.
 *
 * @param is_fd Is frame FD.
 * @param dlc DLC value.
 * @return DLC to bytes.
 */
uint8_t can_dlc_to_bytes_safe(bool is_fd, uint8_t dlc);

/**
 * Maps bytes to DLC while clamping DLC to valid range.
 *
 * @param length Number of bytes.
 * @return DLC.
 */
uint8_t can_bytes_to_dlc(uint8_t length);

#endif
