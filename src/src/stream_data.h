/* SPDX-License-Identifier: MIT */

#ifndef CANSUB_STREAM_DATA_H
#define CANSUB_STREAM_DATA_H

/***********************************************************************************************************************
* Includes.
***********************************************************************************************************************/
/* Standard includes. */

/* Third-party includes. */
#include <zephyr/net_buf.h>

/* Project includes. */
#include "common.h"
#include "http.h"
#include "stream.h"
#include "websocket.h"

/***********************************************************************************************************************
* Public definitions.
***********************************************************************************************************************/

/** Size of the WebSocket buffer in each direction: The upgrade, WebSocket frames and the HDLC frames they carry. */
#define CANSUB_DATA_WEBSOCKET_BUFFER_SIZE (4096 * 4)

/** Accumulator for received HDLC frames. The remote sends HDLC frames of at most 1400 bytes, including the boundary
 * bytes, the CRC and byte stuffing (one Ethernet frame), so its largest frame fits exactly. */
#define CANSUB_DATA_HDLC_RX_SIZE (1400)

/** Outgoing HDLC frame, kept within one Ethernet frame as recommended for the remote. */
#define CANSUB_DATA_HDLC_TX_SIZE (1400)

/** Received CAN frames waiting for cansub_frame_recv(). Must hold the worst case of one HDLC frame. Power of two. */
#define CANSUB_DATA_FRAME_QUEUE_SIZE (256)

/***********************************************************************************************************************
* Public type declarations.
***********************************************************************************************************************/

typedef enum
{
    CANSUB_DATA_STREAM_STATE_IDLE = 0,
    CANSUB_DATA_STREAM_STATE_UPGRADE_SENT,
    CANSUB_DATA_STREAM_STATE_UPGRADE_WAIT,
    CANSUB_DATA_STREAM_STATE_OPEN,
    CANSUB_DATA_STREAM_STATE_CLOSE_SENT,
    CANSUB_DATA_STREAM_STATE_REMOTE_CLOSE,
    CANSUB_DATA_STREAM_STATE_FINISHED,
    CANSUB_DATA_STREAM_STATE_COUNT,
} cansub_data_stream_state;


typedef struct cansub_stream_data
{
    cansub_stream_obj base;

    cansub_data_stream_state state;

    /** Sec-WebSocket-Key of the upgrade request (base64 of 16 random bytes), kept to verify Sec-WebSocket-Accept. */
    char websocket_key[CANSUB_WEBSOCKET_KEY_LENGTH + 1];

    /** Response to the upgrade request. */
    cansub_http_response upgrade_response;

    /** Error code for closing. */
    cansub_result error;

    /** Clean close. */
    bool clean_close;

    /** The close frame (sent or answered) has been written to tx. */
    bool close_queued;

    /** Masking key for the close frame. */
    uint8_t close_mask[CANSUB_WEBSOCKET_MASK_SIZE];

    /** Raw WebSocket bytes. */
    cansub_ringbuffer rx;
    cansub_ringbuffer tx;

    /** WebSocket receive state. */
    cansub_websocket_rx websocket_rx;

    /** Received WebSocket payload, not yet decoded as HDLC frames. */
    struct net_buf_simple hdlc_rx;
    uint8_t hdlc_rx_storage[CANSUB_DATA_HDLC_RX_SIZE];

    /** Outgoing HDLC frame, written as one WebSocket binary frame. */
    struct net_buf_simple hdlc_tx;
    uint8_t hdlc_tx_storage[CANSUB_DATA_HDLC_TX_SIZE];
    /** Masking key for the WebSocket frame of hdlc_tx. */
    uint8_t hdlc_tx_mask[CANSUB_WEBSOCKET_MASK_SIZE];

    /** Received CAN frames. */
    cansub_frame frames[CANSUB_DATA_FRAME_QUEUE_SIZE];
    uint32_t frames_read;
    uint32_t frames_write;
} cansub_stream_data;

/***********************************************************************************************************************
* Public variable declarations.
***********************************************************************************************************************/

/***********************************************************************************************************************
* Public function declarations.
***********************************************************************************************************************/

cansub_result cansub_stream_data_init(cansub_stream_data *stream);
void cansub_stream_data_destroy(cansub_stream_data *stream);

bool cansub_stream_data_is_open(cansub_stream_data const *stream);
cansub_result cansub_stream_data_error(cansub_stream_data const *stream);

/**
 * Whether the remote has closed the WebSocket without a close from this end.
 *
 * @param stream Data stream.
 * @return true if the remote initiated the close.
 */
bool cansub_stream_data_remote_closed(cansub_stream_data const *stream);

/**
 * Whether the close from this end waits for the confirmation of the remote, which an abort can speed up.
 *
 * @param stream Data stream.
 * @return true while the closing handshake started by this end is in progress.
 */
bool cansub_stream_data_awaiting_close(cansub_stream_data const *stream);

cansub_result cansub_stream_data_open(cansub_stream_data *stream, uint32_t channel, char const *host);

/**
 * Start the closing handshake. Pending CAN frames are sent before the close frame.
 *
 * Does nothing unless the WebSocket is open.
 *
 * @param stream Data stream.
 * @return CANSUB_RESULT_OK, or CANSUB_RESULT_ERROR if no masking key could be generated.
 */
cansub_result cansub_stream_data_close(cansub_stream_data *stream);

/**
 * Abandon the stream before the WebSocket is open, e.g. a close while opening.
 *
 * Pending data is discarded and the stream no longer wants a transport. The stream ends without an error.
 *
 * @param stream Data stream.
 */
void cansub_stream_data_abort(cansub_stream_data *stream);

/**
 * Check whether a CAN frame can be sent, see the malformed rules of @ref cansub_frame.
 *
 * @param frame CAN frame.
 * @param[out] faults Every rule the frame violates, see @ref cansub_frame_fault_code. 0 for a valid frame. Ignored if NULL.
 * @return true if the frame is valid for sending.
 */
bool cansub_stream_data_frame_valid(cansub_frame const *frame, cansub_frame_faults *faults);

/**
 * Encode a valid CAN frame for sending.
 *
 * @param stream Data stream.
 * @param frame CAN frame, see @ref cansub_stream_data_frame_valid().
 * @return CANSUB_RESULT_OK when enqueued, CANSUB_RESULT_AGAIN if there is no space, CANSUB_RESULT_CLOSED if the
 *         WebSocket is not open, or CANSUB_RESULT_ERROR.
 */
cansub_result cansub_stream_data_send(cansub_stream_data *stream, cansub_frame const *frame);

/**
 * Read a received CAN frame.
 *
 * Does not process held back data, call the rx_done of the stream afterwards.
 *
 * @param stream Data stream.
 * @param[out] frame CAN frame.
 * @return CANSUB_RESULT_OK when read, CANSUB_RESULT_AGAIN if no frame is available.
 */
cansub_result cansub_stream_data_recv(cansub_stream_data *stream, cansub_frame *frame);

#endif