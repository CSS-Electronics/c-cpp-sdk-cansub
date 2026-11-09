/* SPDX-License-Identifier: MIT */

/***********************************************************************************************************************
 * Includes.
 **********************************************************************************************************************/
/* Standard includes. */
#include <assert.h>
#include <inttypes.h>
#include <string.h>
#include <stdio.h>

/* Third-party includes. */

/* Project includes. */
#include "cansub_protocol/cansub_protocol.h"
#include "common.h"
#include "http.h"
#include "stream_data.h"
#include "websocket.h"

/***********************************************************************************************************************
 * External declarations.
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Public variable definition.
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Private definitions.
 **********************************************************************************************************************/

/* Smallest serialized CAN frame (an error frame). */
#define CANSUB_DATA_MESSAGE_MIN_SIZE (MSG_ENCODE_ERR_HEADER_SIZE)

/* Boundary bytes and CRC32 around the payload of an HDLC frame. */
#define CANSUB_DATA_HDLC_OVERHEAD (2 + 4)

/* Most CAN frames in any HDLC frame that fits into hdlc_rx: Only error frames and no stuffing, (1400 - 6) / 7 = 199. */
#define CANSUB_DATA_FRAMES_WORST ((CANSUB_DATA_HDLC_RX_SIZE - CANSUB_DATA_HDLC_OVERHEAD) / CANSUB_DATA_MESSAGE_MIN_SIZE)

/* Space needed in hdlc_tx to append any CAN frame: message and CRC32 fully stuffed, plus both boundary bytes. */
#define CANSUB_DATA_HDLC_TX_WORST (2 * (MSG_ENCODE_MAX_SIZE(64) + 4) + 2)

/* WebSocket close status for a normal closure (1000). */
#define CANSUB_DATA_CLOSE_STATUS_HI (0x03)
#define CANSUB_DATA_CLOSE_STATUS_LO (0xE8)

/* Data stream state masks for CANSUB_STATE_IN(). */
#define CANSUB_DATA_MASK_IDLE         CANSUB_STATE_BIT(CANSUB_DATA_STREAM_STATE_IDLE)
#define CANSUB_DATA_MASK_UPGRADE_SENT CANSUB_STATE_BIT(CANSUB_DATA_STREAM_STATE_UPGRADE_SENT)
#define CANSUB_DATA_MASK_UPGRADE_WAIT CANSUB_STATE_BIT(CANSUB_DATA_STREAM_STATE_UPGRADE_WAIT)
#define CANSUB_DATA_MASK_OPEN         CANSUB_STATE_BIT(CANSUB_DATA_STREAM_STATE_OPEN)
#define CANSUB_DATA_MASK_CLOSE_SENT   CANSUB_STATE_BIT(CANSUB_DATA_STREAM_STATE_CLOSE_SENT)
#define CANSUB_DATA_MASK_REMOTE_CLOSE CANSUB_STATE_BIT(CANSUB_DATA_STREAM_STATE_REMOTE_CLOSE)
#define CANSUB_DATA_MASK_FINISHED     CANSUB_STATE_BIT(CANSUB_DATA_STREAM_STATE_FINISHED)
_Static_assert(CANSUB_DATA_STREAM_STATE_COUNT < 32, "Too many states");

/** Not opened yet, so no closing handshake applies. */
#define CANSUB_DATA_MASK_PRE_OPEN     (CANSUB_DATA_MASK_IDLE | CANSUB_DATA_MASK_UPGRADE_SENT | CANSUB_DATA_MASK_UPGRADE_WAIT)
/** WebSocket frames are exchanged. */
#define CANSUB_DATA_MASK_WEBSOCKET    (CANSUB_DATA_MASK_OPEN | CANSUB_DATA_MASK_CLOSE_SENT)
/** The upgrade response or WebSocket frames are received. */
#define CANSUB_DATA_MASK_RECEIVING    (CANSUB_DATA_MASK_UPGRADE_WAIT | CANSUB_DATA_MASK_WEBSOCKET)
/** A close frame from this end goes out after the pending CAN frames. */
#define CANSUB_DATA_MASK_CLOSING      (CANSUB_DATA_MASK_CLOSE_SENT | CANSUB_DATA_MASK_REMOTE_CLOSE)
/** Not started or finished, no transport is needed. */
#define CANSUB_DATA_MASK_INACTIVE     (CANSUB_DATA_MASK_IDLE | CANSUB_DATA_MASK_FINISHED)

_Static_assert(CANSUB_RINGBUFFER_SIZE_EXACT(CANSUB_DATA_WEBSOCKET_BUFFER_SIZE), "WebSocket buffer size must be a power of two");
_Static_assert((CANSUB_DATA_FRAME_QUEUE_SIZE & (CANSUB_DATA_FRAME_QUEUE_SIZE - 1)) == 0, "Frame queue size must be a power of two");
_Static_assert(CANSUB_DATA_FRAME_QUEUE_SIZE >= CANSUB_DATA_FRAMES_WORST, "Frame queue cannot hold the worst case of one HDLC frame");
_Static_assert(CANSUB_DATA_HDLC_TX_SIZE >= CANSUB_DATA_HDLC_TX_WORST, "HDLC TX buffer cannot hold one CAN frame");
_Static_assert(CANSUB_DATA_HDLC_TX_SIZE <= CANSUB_WEBSOCKET_PAYLOAD_MAX, "HDLC TX buffer exceeds a WebSocket frame");

/***********************************************************************************************************************
 * Private type definitions.
 **********************************************************************************************************************/

/***********************************************************************************************************************
* Private variable definitions.
***********************************************************************************************************************/

/***********************************************************************************************************************
 * Private function declarations.
 **********************************************************************************************************************/

static cansub_ringbuffer *cansub_data_tx_source(cansub_stream_obj *base);
static cansub_result cansub_data_tx_done(cansub_stream_obj *base);
static cansub_ringbuffer *cansub_data_rx_sink(cansub_stream_obj *base);
static cansub_result cansub_data_rx_done(cansub_stream_obj *base);
static cansub_ready cansub_data_ready(cansub_stream_obj const *base);
static void cansub_data_failed(cansub_stream_obj *base, cansub_result reason);
static void cansub_data_ended(cansub_stream_obj *base, bool opened, bool final);
static bool cansub_data_wants_transport(cansub_stream_obj const *base);

/**
 * Parse the HTTP upgrade response.
 *
 * @param stream Data stream.
 * @return CANSUB_RESULT_OK (also while incomplete) or a CONNECTION_* code.
 */
static cansub_result cansub_data_upgrade(cansub_stream_data *stream);

/**
 * Move received WebSocket payload through the HDLC decoder into the frame queue.
 *
 * Stops while the frame queue cannot hold the worst case of the next HDLC frame, holding back the received data.
 *
 * @param stream Data stream.
 * @return CANSUB_RESULT_OK, CANSUB_RESULT_CLOSED when the transport can be closed, or an error code.
 */
static cansub_result cansub_data_process(cansub_stream_data *stream);

/**
 * Handle a close frame from the remote.
 *
 * @param stream Data stream.
 * @return CANSUB_RESULT_OK when a close frame must still be sent, CANSUB_RESULT_CLOSED when the handshake is complete,
 *         or CANSUB_RESULT_ERROR.
 */
static cansub_result cansub_data_remote_close(cansub_stream_data *stream);

/**
 * Get the size of the first complete HDLC frame in hdlc_rx, discarding bytes before its start boundary.
 *
 * @param stream Data stream.
 * @return Size including both boundary bytes, or 0 if there is no complete frame.
 */
static uint16_t cansub_data_hdlc_span(cansub_stream_data *stream);

/**
 * Write hdlc_tx as one WebSocket binary frame into tx.
 *
 * @param stream Data stream.
 * @return CANSUB_RESULT_OK when written or empty, CANSUB_RESULT_AGAIN if tx has no space.
 */
static cansub_result cansub_data_flush(cansub_stream_data *stream);

/**
 * Write pending CAN frames and the close frame into tx, as far as the state allows.
 *
 * @param stream Data stream.
 */
static void cansub_data_tx_fill(cansub_stream_data *stream);

/* Decoder callback, stores a CAN frame in the frame queue. */
static void cansub_data_frame_decoded(cansub_frame *frame, void *user_data);

static uint32_t cansub_data_frames_count(cansub_stream_data const *stream);
static uint32_t cansub_data_frames_space(cansub_stream_data const *stream);

static cansub_stream_vtable const cansub_stream_data_vtable = {
    .tx_source = cansub_data_tx_source,
    .tx_done = cansub_data_tx_done,
    .rx_sink = cansub_data_rx_sink,
    .rx_done = cansub_data_rx_done,
    .ready = cansub_data_ready,
    .failed = cansub_data_failed,
    .ended = cansub_data_ended,
    .wants_transport = cansub_data_wants_transport,
};

/***********************************************************************************************************************
* Public function definitions.
***********************************************************************************************************************/

cansub_result cansub_stream_data_init(cansub_stream_data *stream)
{
    assert(stream);

    cansub_result result = CANSUB_RESULT_ERROR;

    do
    {
        result = cansub_ringbuffer_init(&stream->rx, CANSUB_DATA_WEBSOCKET_BUFFER_SIZE);
        if (result != CANSUB_RESULT_OK)
        {
            break;
        }
        result = cansub_ringbuffer_init(&stream->tx, CANSUB_DATA_WEBSOCKET_BUFFER_SIZE);
        if (result != CANSUB_RESULT_OK)
        {
            break;
        }

        net_buf_simple_init_with_data(&stream->hdlc_rx, stream->hdlc_rx_storage, sizeof(stream->hdlc_rx_storage));
        net_buf_simple_reset(&stream->hdlc_rx);
        net_buf_simple_init_with_data(&stream->hdlc_tx, stream->hdlc_tx_storage, sizeof(stream->hdlc_tx_storage));
        net_buf_simple_reset(&stream->hdlc_tx);
        cansub_websocket_rx_reset(&stream->websocket_rx);

        stream->base.vtable = &cansub_stream_data_vtable;

        result = CANSUB_RESULT_OK;
    } while (0);

    if (result != CANSUB_RESULT_OK)
    {
        cansub_ringbuffer_destroy(&stream->rx);
        cansub_ringbuffer_destroy(&stream->tx);
    }

    return result;
}

void cansub_stream_data_destroy(cansub_stream_data *stream)
{
    assert(stream);

    cansub_ringbuffer_destroy(&stream->rx);
    cansub_ringbuffer_destroy(&stream->tx);
}

bool cansub_stream_data_is_open(cansub_stream_data const *stream)
{
    assert(stream);

    return CANSUB_STATE_IN(stream->state, CANSUB_DATA_MASK_OPEN);
}

cansub_result cansub_stream_data_error(cansub_stream_data const *stream)
{
    assert(stream);

    return stream->error;
}

bool cansub_stream_data_remote_closed(cansub_stream_data const *stream)
{
    assert(stream);

    return CANSUB_STATE_IN(stream->state, CANSUB_DATA_MASK_REMOTE_CLOSE) && !stream->clean_close;
}

bool cansub_stream_data_awaiting_close(cansub_stream_data const *stream)
{
    assert(stream);

    return CANSUB_STATE_IN(stream->state, CANSUB_DATA_MASK_CLOSE_SENT);
}

cansub_result cansub_stream_data_open(cansub_stream_data *stream, uint32_t channel, char const *host)
{
    assert(stream);

    cansub_result result = CANSUB_RESULT_ERROR;

    do
    {
        /* Reset buffers. */
        cansub_ringbuffer_reset(&stream->rx);
        cansub_ringbuffer_reset(&stream->tx);
        net_buf_simple_reset(&stream->hdlc_rx);
        net_buf_simple_reset(&stream->hdlc_tx);
        cansub_websocket_rx_reset(&stream->websocket_rx);
        stream->frames_read = 0;
        stream->frames_write = 0;
        stream->close_queued = false;

        /* Room for the largest channel, UINT32_MAX. */
        char path[sizeof("/api/can/4294967295/ws")];
        snprintf(path, sizeof(path), "/api/can/%" PRIu32 "/ws", channel);

        /* Generate the key. */
        result = cansub_websocket_key_generate(stream->websocket_key, sizeof(stream->websocket_key));
        if (result != CANSUB_RESULT_OK)
        {
            break;
        }

        cansub_http_header const headers[] = {
            { .name = "Upgrade", .value = "websocket" },
            { .name = "Connection", .value = "Upgrade" },
            { .name = "Sec-WebSocket-Key", .value = stream->websocket_key },
            { .name = "Sec-WebSocket-Version", .value = "13" },
        };

        /* Prepare the upgrade request. */
        cansub_http_request upgrade_request = {
            .method = "GET",
            .path = path,
            .host = host,
            .headers = headers,
            .header_count = ARRAY_SIZE(headers),
        };

        result = cansub_http_write_request(&upgrade_request, &stream->tx);
        if (result < 0)
        {
            break;
        }

        stream->state = CANSUB_DATA_STREAM_STATE_UPGRADE_SENT;

        result = CANSUB_RESULT_OK;
    } while (0);

    return result;
}

cansub_result cansub_stream_data_close(cansub_stream_data *stream)
{
    assert(stream);

    cansub_result result = CANSUB_RESULT_ERROR;

    do
    {
        if (!CANSUB_STATE_IN(stream->state, CANSUB_DATA_MASK_OPEN))
        {
            result = CANSUB_RESULT_OK;
            break;
        }

        result = cansub_websocket_mask_generate(stream->close_mask);
        if (result != CANSUB_RESULT_OK)
        {
            break;
        }

        /* The close frame follows the pending CAN frames, see cansub_data_tx_fill(). */
        stream->close_queued = false;
        stream->state = CANSUB_DATA_STREAM_STATE_CLOSE_SENT;

        result = CANSUB_RESULT_OK;
    } while (0);

    return result;
}

void cansub_stream_data_abort(cansub_stream_data *stream)
{
    assert(stream);

    if (!CANSUB_STATE_IN(stream->state, CANSUB_DATA_MASK_PRE_OPEN))
    {
        /* Open or already closing, the closing handshake applies. */
        return;
    }

    cansub_ringbuffer_reset(&stream->rx);
    cansub_ringbuffer_reset(&stream->tx);

    /* Nothing was opened, so ending the transport is not a lost connection. */
    stream->clean_close = true;
    stream->state = CANSUB_DATA_STREAM_STATE_FINISHED;
}

bool cansub_stream_data_frame_valid(cansub_frame const *frame, cansub_frame_faults *faults)
{
    uint8_t const allowed = CANSUB_FRAME_FLAG_RTR | CANSUB_FRAME_FLAG_IDE | CANSUB_FRAME_FLAG_FDF | CANSUB_FRAME_FLAG_BRS | CANSUB_FRAME_FLAG_ESI;
    uint8_t const rx_only = CANSUB_FRAME_FLAG_ERROR | CANSUB_FRAME_FLAG_TX;
    bool const fd = (frame->flags & CANSUB_FRAME_FLAG_FDF) != 0;
    uint32_t const id_mask = (frame->flags & CANSUB_FRAME_FLAG_IDE) ? CAN_EXT_ID_MSK : CAN_STD_ID_MSK;
    cansub_frame_faults found = 0;

    /* Reserved flags and the reserved byte must be 0. */
    if ((frame->flags & ~(allowed | rx_only)) || (frame->reserved[0] != 0))
    {
        found |= CANSUB_FRAME_FAULT_RESERVED;
    }

    /* Error frames and transmit acknowledgements cannot be sent. */
    if (frame->flags & rx_only)
    {
        found |= CANSUB_FRAME_FAULT_RX_ONLY;
    }

    /* FD flags are only allowed on FD frames. */
    if (!fd && (frame->flags & (CANSUB_FRAME_FLAG_BRS | CANSUB_FRAME_FLAG_ESI)))
    {
        found |= CANSUB_FRAME_FAULT_FD_ONLY;
    }

    /* RTR is not allowed on FD frames. */
    if (fd && (frame->flags & CANSUB_FRAME_FLAG_RTR))
    {
        found |= CANSUB_FRAME_FAULT_RTR_FD;
    }

    /* Only the bits of the arbitration ID type may be set. */
    if (frame->arbitration_id & ~id_mask)
    {
        found |= CANSUB_FRAME_FAULT_ID;
    }

    /* The length can only be checked against a DLC in the valid range. Also rejects lengths which are not valid CAN data lengths. */
    if (frame->dlc > CANFD_MAX_DLC)
    {
        found |= CANSUB_FRAME_FAULT_DLC;
    }
    else if (frame->length != can_dlc_to_bytes_safe(fd, frame->dlc))
    {
        found |= CANSUB_FRAME_FAULT_LENGTH;
    }

    if (faults)
    {
        *faults = found;
    }

    return found == 0;
}

cansub_result cansub_stream_data_send(cansub_stream_data *stream, cansub_frame const *frame)
{
    cansub_result result = CANSUB_RESULT_ERROR;

    do
    {
        if (!CANSUB_STATE_IN(stream->state, CANSUB_DATA_MASK_OPEN))
        {
            result = CANSUB_RESULT_CLOSED;
            break;
        }

        /* An encode failure would leave a broken HDLC frame behind, so ensure the worst case fits. */
        if (net_buf_simple_tailroom(&stream->hdlc_tx) < CANSUB_DATA_HDLC_TX_WORST)
        {
            result = cansub_data_flush(stream);
            if (result != CANSUB_RESULT_OK)
            {
                break;
            }
        }

        /* A new WebSocket message starts with the first CAN frame in hdlc_tx. */
        if (stream->hdlc_tx.len == 0)
        {
            result = cansub_websocket_mask_generate(stream->hdlc_tx_mask);
            if (result != CANSUB_RESULT_OK)
            {
                break;
            }
        }

        /* The timestamp is ignored on sending, but the encoder rejects timestamps before its time base. */
        cansub_frame encoded = *frame;
        encoded.timestamp_us = CANSUB_PROTOCOL_TIME_BASE_US;

        if (cansub_protocol_encode(&encoded, &stream->hdlc_tx) < 0)
        {
            result = CANSUB_RESULT_ERROR;
            break;
        }

        result = CANSUB_RESULT_OK;
    } while (0);

    return result;
}

cansub_result cansub_stream_data_recv(cansub_stream_data *stream, cansub_frame *frame)
{
    if (cansub_data_frames_count(stream) == 0)
    {
        return CANSUB_RESULT_AGAIN;
    }

    *frame = stream->frames[stream->frames_read & (CANSUB_DATA_FRAME_QUEUE_SIZE - 1)];
    stream->frames_read += 1;

    return CANSUB_RESULT_OK;
}

/***********************************************************************************************************************
* Private function definitions.
***********************************************************************************************************************/

static cansub_ringbuffer *cansub_data_tx_source(cansub_stream_obj *base)
{
    cansub_stream_data *stream = CONTAINER_OF(base, cansub_stream_data, base);

    if (CANSUB_STATE_IN(stream->state, CANSUB_DATA_MASK_INACTIVE))
    {
        return NULL;
    }

    cansub_data_tx_fill(stream);

    if (cansub_ringbuffer_empty(&stream->tx))
    {
        return NULL;
    }

    return &stream->tx;
}

static cansub_result cansub_data_tx_done(cansub_stream_obj *base)
{
    cansub_stream_data *stream = CONTAINER_OF(base, cansub_stream_data, base);

    if (CANSUB_STATE_IN(stream->state, CANSUB_DATA_MASK_UPGRADE_SENT) && cansub_ringbuffer_empty(&stream->tx))
    {
        stream->state = CANSUB_DATA_STREAM_STATE_UPGRADE_WAIT;
    }

    if (CANSUB_STATE_IN(stream->state, CANSUB_DATA_MASK_REMOTE_CLOSE) && stream->close_queued && cansub_ringbuffer_empty(&stream->tx))
    {
        /* The close of the remote has been answered. */
        return CANSUB_RESULT_CLOSED;
    }

    return CANSUB_RESULT_OK;
}

static cansub_ringbuffer *cansub_data_rx_sink(cansub_stream_obj *base)
{
    cansub_stream_data *stream = CONTAINER_OF(base, cansub_stream_data, base);

    if (cansub_ringbuffer_full(&stream->rx))
    {
        return NULL;
    }

    /* Received data waits while frames are unread. Only read on while any HDLC frame still fits into the frame queue:
     * then cansub_data_process() never holds anything back, so everything received is processed before the transport
     * is read again. E.g. a WebSocket close is never left behind while the remote's close_notify is read. */
    if (cansub_data_frames_space(stream) < CANSUB_DATA_FRAMES_WORST)
    {
        return NULL;
    }

    if (!CANSUB_STATE_IN(stream->state, CANSUB_DATA_MASK_RECEIVING))
    {
        return NULL;
    }

    return &stream->rx;
}

static cansub_result cansub_data_rx_done(cansub_stream_obj *base)
{
    cansub_stream_data *stream = CONTAINER_OF(base, cansub_stream_data, base);
    cansub_result result = CANSUB_RESULT_OK;

    if (CANSUB_STATE_IN(stream->state, CANSUB_DATA_MASK_UPGRADE_WAIT))
    {
        result = cansub_data_upgrade(stream);
    }

    /* WebSocket data may follow the upgrade response directly. */
    if ((result == CANSUB_RESULT_OK) && CANSUB_STATE_IN(stream->state, CANSUB_DATA_MASK_WEBSOCKET))
    {
        result = cansub_data_process(stream);
    }

    return result;
}

static cansub_ready cansub_data_ready(cansub_stream_obj const *base)
{
    cansub_stream_data const *stream = CONTAINER_OF_CONST(base, cansub_stream_data, base);
    cansub_ready result = 0;

    if (cansub_data_frames_count(stream) > 0)
    {
        result |= CANSUB_READY_CAN_RX;
    }

    if (CANSUB_STATE_IN(stream->state, CANSUB_DATA_MASK_OPEN))
    {
        /* Either the frame fits directly, or hdlc_tx can be flushed to make room for it. */
        bool const fits = net_buf_simple_tailroom(&stream->hdlc_tx) >= CANSUB_DATA_HDLC_TX_WORST;
        bool const flushable = cansub_ringbuffer_space(&stream->tx) >= cansub_websocket_frame_size(stream->hdlc_tx.len);

        if (fits || flushable)
        {
            result |= CANSUB_READY_CAN_TX;
        }
    }

    return result;
}

static void cansub_data_failed(cansub_stream_obj *base, cansub_result reason)
{
    cansub_stream_data *stream = CONTAINER_OF(base, cansub_stream_data, base);

    /* The first error is the cause, later ones are consequences. */
    if (stream->error == CANSUB_RESULT_OK)
    {
        stream->error = reason;
    }

    /* Stop processing. */
    stream->state = CANSUB_DATA_STREAM_STATE_FINISHED;
}

static void cansub_data_ended(cansub_stream_obj *base, bool opened, bool final)
{
    (void)opened;
    (void)final;
    cansub_stream_data *stream = CONTAINER_OF(base, cansub_stream_data, base);

    if ((stream->error == CANSUB_RESULT_OK) && !stream->clean_close)
    {
        /* No error yet, no clean close set.
         * Could not be opened, or lost before the close completed. A rejected upgrade already failed in rx_done. */
        stream->error = CANSUB_RESULT_CONNECTION_LOST;
    }

    stream->state = CANSUB_DATA_STREAM_STATE_FINISHED;
}

static bool cansub_data_wants_transport(cansub_stream_obj const *base)
{
    cansub_stream_data const *stream = CONTAINER_OF_CONST(base, cansub_stream_data, base);

    return !CANSUB_STATE_IN(stream->state, CANSUB_DATA_MASK_INACTIVE);
}

static cansub_result cansub_data_upgrade(cansub_stream_data *stream)
{
    /* Parse the result (there may be additional websocket data after this). */
    cansub_result upgrade_result = cansub_http_parse_response(&stream->upgrade_response, &stream->rx);
    if ((stream->upgrade_response.header_size != 0) && (stream->upgrade_response.status != 101))
    {
        /* Rejected, the body of the response is not needed. */
        return CANSUB_RESULT_CONNECTION_REJECTED;
    }
    if (upgrade_result != CANSUB_RESULT_OK)
    {
        /* Incomplete, or an invalid response. */
        return (upgrade_result == CANSUB_RESULT_AGAIN) ? CANSUB_RESULT_OK : upgrade_result;
    }

    if (stream->upgrade_response.status != 101)
    {
        return CANSUB_RESULT_CONNECTION_REJECTED;
    }

    /* Check the headers. The parser left the response linear at the start of rx. */
    void const *const data = cansub_ringbuffer_spans_read(&stream->rx).span_1;
    char tmp[64];
    cansub_result result;

    result = cansub_http_response_header_first(&stream->upgrade_response, data, "Upgrade", tmp, sizeof(tmp));
    if (result < 0)
    {
        return CANSUB_RESULT_CONNECTION_PROTOCOL;
    }
    if (cansub_strcasecmp(tmp, "websocket") != 0)
    {
        return CANSUB_RESULT_CONNECTION_PROTOCOL;
    }

    result = cansub_http_response_header_first(&stream->upgrade_response, data, "Connection", tmp, sizeof(tmp));
    if (result < 0)
    {
        return CANSUB_RESULT_CONNECTION_PROTOCOL;
    }
    if (cansub_strcasecmp(tmp, "upgrade") != 0)
    {
        return CANSUB_RESULT_CONNECTION_PROTOCOL;
    }

    result = cansub_http_response_header_first(&stream->upgrade_response, data, "Sec-WebSocket-Accept", tmp, sizeof(tmp));
    if (result < 0)
    {
        return CANSUB_RESULT_CONNECTION_PROTOCOL;
    }

    char expected_accept[CANSUB_WEBSOCKET_ACCEPT_LENGTH + 1];
    result = cansub_websocket_accept_compute(stream->websocket_key, expected_accept, sizeof(expected_accept));
    if (result != CANSUB_RESULT_OK)
    {
        return result;
    }
    if (strcmp(expected_accept, tmp) != 0)
    {
        return CANSUB_RESULT_CONNECTION_PROTOCOL;
    }

    /* Remove the HTTP upgrade response from the buffer. */
    cansub_ringbuffer_commit_read(&stream->rx, stream->upgrade_response.header_size);
    stream->state = CANSUB_DATA_STREAM_STATE_OPEN;

    return CANSUB_RESULT_OK;
}

static cansub_result cansub_data_process(cansub_stream_data *stream)
{
    struct net_buf_simple *const hdlc_rx = &stream->hdlc_rx;

    while (true)
    {
        /* Decode the complete HDLC frames, as long as the frame queue can hold their worst case. */
        uint16_t span;
        while ((span = cansub_data_hdlc_span(stream)) != 0)
        {
            if (span == 2)
            {
                /* Two adjacent boundary bytes, the second one may start the next frame. */
                (void)net_buf_simple_pull(hdlc_rx, 1);
                continue;
            }

            uint32_t const worst = (span > CANSUB_DATA_HDLC_OVERHEAD) ? (span - CANSUB_DATA_HDLC_OVERHEAD) / CANSUB_DATA_MESSAGE_MIN_SIZE : 0;
            if (cansub_data_frames_space(stream) < worst)
            {
                /* Hold back the received data until frames are read. */
                return CANSUB_RESULT_OK;
            }

            /* The decoder destuffs in place between the boundary bytes, which is fine as that part is consumed afterwards.
             * CRC errors drop the frame. */
            struct net_buf_simple frame;
            net_buf_simple_init_with_data(&frame, hdlc_rx->data, span);
            int const decoded = cansub_protocol_decode(&frame, cansub_data_frame_decoded, stream);

            /* A failed span may be an unterminated frame cut short by the start of the next one. Keep its closing
             * boundary byte (left untouched by the decoder) as a possible start, so the next frame is not lost. */
            (void)net_buf_simple_pull(hdlc_rx, (decoded < 0) ? (size_t)(span - 1) : span);
        }

        /* Move the incomplete frame to the front to make room. */
        if (hdlc_rx->data != stream->hdlc_rx_storage)
        {
            memmove(stream->hdlc_rx_storage, hdlc_rx->data, hdlc_rx->len);
            hdlc_rx->data = stream->hdlc_rx_storage;
        }

        if (net_buf_simple_tailroom(hdlc_rx) == 0)
        {
            /* An HDLC frame larger than the remote may send: drop it like a CRC error. Its remaining bytes hold no
             * boundary byte (byte stuffing), so they are skipped until its end, which then resynchronizes. */
            net_buf_simple_reset(hdlc_rx);
        }

        size_t const tailroom = net_buf_simple_tailroom(hdlc_rx);

        cansub_result const result = cansub_websocket_read(&stream->websocket_rx, &stream->rx, net_buf_simple_tail(hdlc_rx), tailroom);
        if (result == CANSUB_RESULT_CLOSED)
        {
            return cansub_data_remote_close(stream);
        }
        if (result <= 0)
        {
            /* Need more data, or a protocol violation. */
            return result;
        }

        (void)net_buf_simple_add(hdlc_rx, (size_t)result);
    }
}

static cansub_result cansub_data_remote_close(cansub_stream_data *stream)
{
    if (CANSUB_STATE_IN(stream->state, CANSUB_DATA_MASK_CLOSE_SENT))
    {
        /* The close from this end is confirmed. */
        stream->clean_close = true;

        if (stream->close_queued && cansub_ringbuffer_empty(&stream->tx))
        {
            return CANSUB_RESULT_CLOSED;
        }

        /* Both ends closed at the same time, the close frame of this end is still pending. */
    }
    else
    {
        /* Initiated by the remote, answer it. */
        cansub_result const result = cansub_websocket_mask_generate(stream->close_mask);
        if (result != CANSUB_RESULT_OK)
        {
            return result;
        }
        stream->close_queued = false;
    }

    /* CAN frames not yet written are discarded, the remote no longer accepts them. */
    net_buf_simple_reset(&stream->hdlc_tx);
    stream->state = CANSUB_DATA_STREAM_STATE_REMOTE_CLOSE;

    return CANSUB_RESULT_OK;
}

static uint16_t cansub_data_hdlc_span(cansub_stream_data *stream)
{
    struct net_buf_simple *const hdlc_rx = &stream->hdlc_rx;

    uint8_t const *const start = memchr(hdlc_rx->data, HDLC_BOUNDARY_BYTE, hdlc_rx->len);
    if (!start)
    {
        /* No start boundary, these bytes can never be part of a frame. */
        net_buf_simple_reset(hdlc_rx);
        return 0;
    }

    /* Discard any bytes before the start boundary. */
    (void)net_buf_simple_pull(hdlc_rx, (size_t)(start - hdlc_rx->data));

    uint8_t const *const end = memchr(hdlc_rx->data + 1, HDLC_BOUNDARY_BYTE, hdlc_rx->len - 1);
    if (!end)
    {
        return 0;
    }

    return (uint16_t)(end + 1 - hdlc_rx->data);
}

static cansub_result cansub_data_flush(cansub_stream_data *stream)
{
    cansub_result result = CANSUB_RESULT_OK;

    if (stream->hdlc_tx.len > 0)
    {
        result = cansub_websocket_write_frame(&stream->tx, CANSUB_WEBSOCKET_OPCODE_BINARY, stream->hdlc_tx_mask, stream->hdlc_tx.data, stream->hdlc_tx.len);
        if (result == CANSUB_RESULT_OK)
        {
            net_buf_simple_reset(&stream->hdlc_tx);
        }
    }

    return result;
}

static void cansub_data_tx_fill(cansub_stream_data *stream)
{
    if (CANSUB_STATE_IN(stream->state, CANSUB_DATA_MASK_WEBSOCKET))
    {
        (void)cansub_data_flush(stream);
    }

    /* The close frame goes after all pending CAN frames. */
    if (CANSUB_STATE_IN(stream->state, CANSUB_DATA_MASK_CLOSING) && !stream->close_queued && (stream->hdlc_tx.len == 0))
    {
        static uint8_t const payload[] = { CANSUB_DATA_CLOSE_STATUS_HI, CANSUB_DATA_CLOSE_STATUS_LO };

        if (cansub_websocket_write_frame(&stream->tx, CANSUB_WEBSOCKET_OPCODE_CLOSE, stream->close_mask, payload, sizeof(payload)) == CANSUB_RESULT_OK)
        {
            stream->close_queued = true;
        }
    }
}

static void cansub_data_frame_decoded(cansub_frame *frame, void *user_data)
{
    cansub_stream_data *stream = user_data;

    /* Cannot happen, the space for the worst case is checked before decoding. */
    if (cansub_data_frames_space(stream) == 0)
    {
        return;
    }

    if (frame->flags & CANSUB_FRAME_FLAG_ERROR)
    {
        /* The error type is the single data byte, which the decoder stores without setting a DLC. */
        frame->dlc = 1;

        /* Error frames carry no arbitration ID on the wire. Set it here, independent of how the decoder clears the frame. */
        frame->arbitration_id = 0;
    }

    /* The decoder only sets the DLC. For RTR frames, the length is the requested length. */
    frame->length = can_dlc_to_bytes_safe((frame->flags & CANSUB_FRAME_FLAG_FDF) != 0, frame->dlc);

    stream->frames[stream->frames_write & (CANSUB_DATA_FRAME_QUEUE_SIZE - 1)] = *frame;
    stream->frames_write += 1;
}

static uint32_t cansub_data_frames_count(cansub_stream_data const *stream)
{
    return stream->frames_write - stream->frames_read;
}

static uint32_t cansub_data_frames_space(cansub_stream_data const *stream)
{
    return CANSUB_DATA_FRAME_QUEUE_SIZE - cansub_data_frames_count(stream);
}
