/* SPDX-License-Identifier: MIT */

#ifndef CANSUB_WEBSOCKET_H
#define CANSUB_WEBSOCKET_H

/***********************************************************************************************************************
* Includes.
***********************************************************************************************************************/
/* Standard includes. */

/* Project includes. */
#include "common.h"
#include "http.h"

/***********************************************************************************************************************
* Public definitions.
***********************************************************************************************************************/

/** Size of the masking key of a client frame. */
#define CANSUB_WEBSOCKET_MASK_SIZE (4)

/** Length of the base64 encoding of a number of bytes, without the NUL terminator. */
#define CANSUB_WEBSOCKET_BASE64_LENGTH(size) (4 * (((size) + 2) / 3))

/** Random bytes in a Sec-WebSocket-Key (RFC 6455). */
#define CANSUB_WEBSOCKET_NONCE_SIZE (16)

/** Size of the SHA-1 hash a Sec-WebSocket-Accept is computed from. */
#define CANSUB_WEBSOCKET_SHA1_SIZE (20)

/** Length of a Sec-WebSocket-Key, without the NUL terminator. */
#define CANSUB_WEBSOCKET_KEY_LENGTH CANSUB_WEBSOCKET_BASE64_LENGTH(CANSUB_WEBSOCKET_NONCE_SIZE)

/** Length of a Sec-WebSocket-Accept, without the NUL terminator. */
#define CANSUB_WEBSOCKET_ACCEPT_LENGTH CANSUB_WEBSOCKET_BASE64_LENGTH(CANSUB_WEBSOCKET_SHA1_SIZE)

/** Largest payload of a frame. 64 bit payload lengths are not supported. */
#define CANSUB_WEBSOCKET_PAYLOAD_MAX (0xFFFF)

/***********************************************************************************************************************
* Public type declarations.
***********************************************************************************************************************/

typedef enum
{
    CANSUB_WEBSOCKET_OPCODE_CONTINUATION = 0x0,
    CANSUB_WEBSOCKET_OPCODE_BINARY = 0x2,
    CANSUB_WEBSOCKET_OPCODE_CLOSE = 0x8,
} cansub_websocket_opcode;

/**
 * Receive state for frames from the remote.
 *
 * Only binary, continuation and close frames are accepted. The payload of the data frames is treated as one continuous
 * byte stream, message boundaries are not preserved.
 */
typedef struct cansub_websocket_rx
{
    /** Payload bytes left of the current data frame. */
    uint32_t payload_remaining;
    /** A fragmented binary message is in progress, the next data frame must be a continuation. */
    bool in_message;
} cansub_websocket_rx;

/***********************************************************************************************************************
* Public variable declarations.
***********************************************************************************************************************/

/***********************************************************************************************************************
* Public function declarations.
***********************************************************************************************************************/

/**
 * Generate the Sec-WebSocket-Key of an upgrade request: base64 of 16 random bytes.
 *
 * @param[out] key Buffer for the key, NUL terminated. At least CANSUB_WEBSOCKET_KEY_LENGTH + 1 bytes.
 * @param key_size Size of the buffer.
 * @return CANSUB_RESULT_OK, CANSUB_RESULT_MEMORY if the random generator ran out of memory, or CANSUB_RESULT_ERROR.
 */
cansub_result cansub_websocket_key_generate(char *key, size_t key_size);

/**
 * Compute the Sec-WebSocket-Accept the remote must answer for a key from cansub_websocket_key_generate().
 *
 * @param[in] key Sec-WebSocket-Key of the upgrade request.
 * @param[out] accept Buffer for the expected value, NUL terminated. At least CANSUB_WEBSOCKET_ACCEPT_LENGTH + 1 bytes.
 * @param accept_size Size of the buffer.
 * @return CANSUB_RESULT_OK, CANSUB_RESULT_MEMORY if the hash ran out of memory, or CANSUB_RESULT_ERROR.
 */
cansub_result cansub_websocket_accept_compute(char const *key, char *accept, size_t accept_size);

/**
 * Generate a masking key for a client frame.
 *
 * @param[out] mask Masking key.
 * @return CANSUB_RESULT_OK, CANSUB_RESULT_MEMORY if the random generator ran out of memory, or CANSUB_RESULT_ERROR.
 */
cansub_result cansub_websocket_mask_generate(uint8_t mask[CANSUB_WEBSOCKET_MASK_SIZE]);

/**
 * Get the size of a complete client frame.
 *
 * @param payload_size Size of the payload.
 * @return Size of the header, masking key and payload.
 */
size_t cansub_websocket_frame_size(size_t payload_size);

/**
 * Write a complete, masked client frame with FIN set.
 *
 * Nothing is written unless the complete frame fits.
 *
 * @param ringbuffer Destination.
 * @param opcode Frame opcode.
 * @param mask Masking key, see @ref cansub_websocket_mask_generate().
 * @param payload Payload.
 * @param payload_size Size of the payload, at most @ref CANSUB_WEBSOCKET_PAYLOAD_MAX.
 * @return CANSUB_RESULT_OK when written, CANSUB_RESULT_AGAIN if the frame does not fit, CANSUB_RESULT_INVALID_ARG if the payload is too large.
 */
cansub_result cansub_websocket_write_frame(cansub_ringbuffer *ringbuffer, cansub_websocket_opcode opcode, uint8_t const mask[CANSUB_WEBSOCKET_MASK_SIZE], void const *payload, size_t payload_size);

/**
 * Reset the receive state, e.g. for a new connection.
 *
 * @param rx Receive state.
 */
void cansub_websocket_rx_reset(cansub_websocket_rx *rx);

/**
 * Consume frames from the remote and copy the payload of data frames.
 *
 * Data is always returned before a following close frame is reported.
 *
 * @param rx Receive state.
 * @param ringbuffer Received bytes, consumed as they are parsed.
 * @param[out] buffer Destination of the data payload.
 * @param buffer_size Size of the destination.
 * @return Number of payload bytes copied (0 if more data is needed), CANSUB_RESULT_CLOSED if a complete close frame was
 *         consumed, CANSUB_RESULT_CONNECTION_PROTOCOL on a protocol violation.
 */
cansub_result cansub_websocket_read(cansub_websocket_rx *rx, cansub_ringbuffer *ringbuffer, void *buffer, size_t buffer_size);

#endif
