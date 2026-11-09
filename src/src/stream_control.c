/* SPDX-License-Identifier: MIT */

/***********************************************************************************************************************
 * Includes.
 **********************************************************************************************************************/
/* Standard includes. */
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

/* Third-party includes. */

/* Project includes. */
#include "common.h"
#include "stream_control.h"

/***********************************************************************************************************************
 * External declarations.
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Public variable definition.
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Private definitions.
 **********************************************************************************************************************/

/* The abort request is a bodyless DELETE. */
#define CANSUB_CONTROL_ABORT_REQUEST_SIZE (1024)

/* The response to the abort is small, its body is not used. */
#define CANSUB_CONTROL_ABORT_RESPONSE_SIZE (4096)

_Static_assert(CANSUB_RINGBUFFER_SIZE_EXACT(CANSUB_CONTROL_HTTP_BUFFER_SIZE), "HTTP buffer size must be a power of two");
_Static_assert(CANSUB_RINGBUFFER_SIZE_EXACT(CANSUB_CONTROL_ABORT_REQUEST_SIZE), "Abort request size must be a power of two");
_Static_assert(CANSUB_RINGBUFFER_SIZE_EXACT(CANSUB_CONTROL_ABORT_RESPONSE_SIZE), "Abort response size must be a power of two");

/* Request state masks for CANSUB_STATE_IN(). */
#define CANSUB_REQUEST_MASK_FREE        CANSUB_STATE_BIT(CANSUB_REQUEST_STATE_FREE)
#define CANSUB_REQUEST_MASK_QUEUED      CANSUB_STATE_BIT(CANSUB_REQUEST_STATE_QUEUED)
#define CANSUB_REQUEST_MASK_SENDING     CANSUB_STATE_BIT(CANSUB_REQUEST_STATE_SENDING)
#define CANSUB_REQUEST_MASK_AWAITING    CANSUB_STATE_BIT(CANSUB_REQUEST_STATE_AWAITING)
#define CANSUB_REQUEST_MASK_COMPLETE    CANSUB_STATE_BIT(CANSUB_REQUEST_STATE_COMPLETE)
#define CANSUB_REQUEST_MASK_FAILED      CANSUB_STATE_BIT(CANSUB_REQUEST_STATE_FAILED)
_Static_assert(CANSUB_REQUEST_STATE_COUNT < 32, "Too many states");

/** Queued or partially written, i.e. it goes out with tx_source. */
#define CANSUB_REQUEST_MASK_PENDING_TX  (CANSUB_REQUEST_MASK_QUEUED | CANSUB_REQUEST_MASK_SENDING)
/** Outstanding, the response has not arrived yet. */
#define CANSUB_REQUEST_MASK_OUTSTANDING (CANSUB_REQUEST_MASK_PENDING_TX | CANSUB_REQUEST_MASK_AWAITING)
/** In flight, bound to the current transport: it fails if the transport ends. */
#define CANSUB_REQUEST_MASK_IN_FLIGHT   (CANSUB_REQUEST_MASK_SENDING | CANSUB_REQUEST_MASK_AWAITING)
/** A response or a failure waits to be read. */
#define CANSUB_REQUEST_MASK_DONE        (CANSUB_REQUEST_MASK_COMPLETE | CANSUB_REQUEST_MASK_FAILED)

/***********************************************************************************************************************
 * Private type definitions.
 **********************************************************************************************************************/

/***********************************************************************************************************************
* Private variable definitions.
***********************************************************************************************************************/

/***********************************************************************************************************************
 * Private function declarations.
 **********************************************************************************************************************/

static cansub_ringbuffer *cansub_control_tx_source(cansub_stream_obj *base);
static cansub_result cansub_control_tx_done(cansub_stream_obj *base);
static cansub_ringbuffer *cansub_control_rx_sink(cansub_stream_obj *base);
static cansub_result cansub_control_rx_done(cansub_stream_obj *base);
static cansub_ready cansub_control_ready(cansub_stream_obj const *base);
static void cansub_control_failed(cansub_stream_obj *base, cansub_result reason);
static void cansub_control_ended(cansub_stream_obj *base, bool opened, bool final);
static bool cansub_control_wants_transport(cansub_stream_obj const *base);

/* The request at the front of the queue: the one being sent, awaited or waiting to be read. */
static cansub_request *cansub_control_front(cansub_stream_control *stream);
static cansub_request const *cansub_control_front_const(cansub_stream_control const *stream);

static uint8_t const *cansub_control_response_data(cansub_stream_control const *stream);

/* Whether the abort is queued or partially written, i.e. it goes out with tx_source. */
static bool cansub_control_abort_pending_tx(cansub_stream_control const *stream);

/* Prepare the abort to be written again from the start on a new transport. */
static void cansub_control_abort_rewind(cansub_stream_control *stream);

/* Parse the response to the abort, CANSUB_RESULT_OK, CANSUB_RESULT_CLOSED or an error code. */
static cansub_result cansub_control_abort_rx_done(cansub_stream_control *stream);

/* Parse a response on the control transport: AGAIN while incomplete, OK once complete, CLOSED once complete and the
 * remote closes the transport afterwards, or a CONNECTION_* error. */
static cansub_result cansub_control_parse(cansub_http_response *http, cansub_ringbuffer *response);

static cansub_stream_vtable const cansub_stream_control_vtable = {
    .tx_source = cansub_control_tx_source,
    .tx_done = cansub_control_tx_done,
    .rx_sink = cansub_control_rx_sink,
    .rx_done = cansub_control_rx_done,
    .ready = cansub_control_ready,
    .failed = cansub_control_failed,
    .ended = cansub_control_ended,
    .wants_transport = cansub_control_wants_transport,
};

/***********************************************************************************************************************
* Public function definitions.
***********************************************************************************************************************/

cansub_result cansub_stream_control_init(cansub_stream_control *stream)
{
    assert(stream);

    cansub_result result = CANSUB_RESULT_ERROR;

    do
    {
        stream->base.vtable = &cansub_stream_control_vtable;

        result = cansub_ringbuffer_init(&stream->request, CANSUB_CONTROL_HTTP_BUFFER_SIZE);
        if (result != CANSUB_RESULT_OK)
        {
            break;
        }
        result = cansub_ringbuffer_init(&stream->response, CANSUB_CONTROL_HTTP_BUFFER_SIZE);
        if (result != CANSUB_RESULT_OK)
        {
            break;
        }

        result = cansub_ringbuffer_init(&stream->abort_slot.request, CANSUB_CONTROL_ABORT_REQUEST_SIZE);
        if (result != CANSUB_RESULT_OK)
        {
            break;
        }
        result = cansub_ringbuffer_init(&stream->abort_slot.response, CANSUB_CONTROL_ABORT_RESPONSE_SIZE);
        if (result != CANSUB_RESULT_OK)
        {
            break;
        }

        result = CANSUB_RESULT_OK;
    } while (0);

    if (result != CANSUB_RESULT_OK)
    {
        cansub_stream_control_destroy(stream);
    }

    return result;
}

void cansub_stream_control_destroy(cansub_stream_control *stream)
{
    assert(stream);

    cansub_ringbuffer_destroy(&stream->request);
    cansub_ringbuffer_destroy(&stream->response);
    cansub_ringbuffer_destroy(&stream->abort_slot.request);
    cansub_ringbuffer_destroy(&stream->abort_slot.response);
}

cansub_result cansub_stream_control_add_request(cansub_stream_control *stream, uint64_t tag, cansub_http_request const *config)
{
    assert(stream);
    assert(config);

    cansub_result result = CANSUB_RESULT_ERROR;

    do
    {
        if (!cansub_http_valid_request(config))
        {
            result = CANSUB_RESULT_INVALID_ARG;
            break;
        }

        cansub_request *const request = cansub_control_front(stream);

        if (!CANSUB_STATE_IN(request->state, CANSUB_REQUEST_MASK_FREE))
        {
            result = CANSUB_RESULT_AGAIN;
            break;
        }

        /* Reset buffers. */
        cansub_ringbuffer_reset(&stream->request);
        cansub_ringbuffer_reset(&stream->response);

        /* Serialize request. */
        result = cansub_http_write_request(config, &stream->request);
        if (result < 0)
        {
            break;
        }

        memset(request, 0x00, sizeof(*request));

        request->state = CANSUB_REQUEST_STATE_QUEUED;
        request->tag = tag;

        result = CANSUB_RESULT_OK;
    } while (0);

    return result;
}

cansub_result cansub_stream_control_peek_request(cansub_stream_control const *stream, uint64_t *tag, uint32_t *http_status)
{
    cansub_result result = CANSUB_RESULT_AGAIN;
    cansub_request const *const request = cansub_control_front_const(stream);

    if (CANSUB_STATE_IN(request->state, CANSUB_REQUEST_MASK_DONE))
    {
        if (tag)
        {
            *tag = request->tag;
        }
        if (http_status)
        {
            *http_status = request->http_status;
        }

        /* Body size for a response, the CONNECTION_* code for a failed request. */
        result = CANSUB_STATE_IN(request->state, CANSUB_REQUEST_MASK_COMPLETE) ? (cansub_result)request->http.body_size : request->result;
    }

    return result;
}

cansub_result cansub_stream_control_read_request(cansub_stream_control *stream, uint64_t *tag, uint32_t *http_status, void *buffer, size_t buffer_size)
{
    cansub_request *const request = cansub_control_front(stream);
    cansub_result const result = cansub_stream_control_peek_request(stream, tag, http_status);

    if (CANSUB_STATE_IN(request->state, CANSUB_REQUEST_MASK_DONE))
    {
        if (CANSUB_STATE_IN(request->state, CANSUB_REQUEST_MASK_COMPLETE) && buffer)
        {
            /* Truncate to the buffer, the full body size is returned. */
            size_t const copy = MIN(buffer_size, (size_t)request->http.body_size);
            memcpy(buffer, cansub_control_response_data(stream) + request->http.header_size, copy);
        }

        /* The response is consumed, the slot is free for the next request. */
        request->state = CANSUB_REQUEST_STATE_FREE;
    }

    return result;
}

cansub_result cansub_stream_control_read_header(cansub_stream_control const *stream, char const *name, char *buffer, size_t buffer_size)
{
    cansub_result result = CANSUB_RESULT_AGAIN;
    cansub_request const *const request = cansub_control_front_const(stream);

    if (CANSUB_STATE_IN(request->state, CANSUB_REQUEST_MASK_COMPLETE))
    {
        result = cansub_http_response_header(&request->http, cansub_control_response_data(stream), name, buffer, buffer_size);
    }
    else if (CANSUB_STATE_IN(request->state, CANSUB_REQUEST_MASK_FAILED))
    {
        /* A failed request has no headers. */
        result = CANSUB_RESULT_NOT_FOUND;
    }

    return result;
}

bool cansub_stream_control_requests_outstanding(cansub_stream_control const *stream)
{
    return CANSUB_STATE_IN(cansub_control_front_const(stream)->state, CANSUB_REQUEST_MASK_OUTSTANDING);
}

cansub_result cansub_stream_control_in_flight(cansub_stream_control const *stream, uint64_t *tag)
{
    cansub_request const *const request = cansub_control_front_const(stream);

    if (!CANSUB_STATE_IN(request->state, CANSUB_REQUEST_MASK_IN_FLIGHT))
    {
        return CANSUB_RESULT_NOT_FOUND;
    }

    if (tag)
    {
        *tag = request->tag;
    }

    return CANSUB_RESULT_OK;
}

cansub_result cansub_stream_control_abort_start(cansub_stream_control *stream, uint32_t channel, char const *host)
{
    cansub_result result = CANSUB_RESULT_ERROR;
    cansub_control_abort *const slot = &stream->abort_slot;

    do
    {
        if (!CANSUB_STATE_IN(slot->state, CANSUB_REQUEST_MASK_FREE))
        {
            result = CANSUB_RESULT_AGAIN;
            break;
        }

        /* Room for the largest channel, UINT32_MAX. */
        char path[sizeof("/api/can/4294967295/ws")];
        snprintf(path, sizeof(path), "/api/can/%" PRIu32 "/ws", channel);

        cansub_http_request const request = {
            .method = "DELETE",
            .path = path,
            .host = host,
        };

        /* Written from the start of the buffer, see cansub_control_abort_rewind(). */
        cansub_ringbuffer_reset(&slot->request);
        cansub_ringbuffer_reset(&slot->response);
        memset(&slot->http, 0x00, sizeof(slot->http));

        result = cansub_http_write_request(&request, &slot->request);
        if (result < 0)
        {
            break;
        }

        slot->state = CANSUB_REQUEST_STATE_QUEUED;
        result = CANSUB_RESULT_OK;
    } while (0);

    return result;
}

bool cansub_stream_control_abort_in_progress(cansub_stream_control const *stream)
{
    return CANSUB_STATE_IN(stream->abort_slot.state, CANSUB_REQUEST_MASK_OUTSTANDING);
}

void cansub_stream_control_abort_cancel(cansub_stream_control *stream)
{
    if (CANSUB_STATE_IN(stream->abort_slot.state, CANSUB_REQUEST_MASK_QUEUED))
    {
        stream->abort_slot.state = CANSUB_REQUEST_STATE_FREE;
    }
}

/***********************************************************************************************************************
* Private function definitions.
***********************************************************************************************************************/

static cansub_request *cansub_control_front(cansub_stream_control *stream)
{
    return &stream->requests[0];
}

static cansub_request const *cansub_control_front_const(cansub_stream_control const *stream)
{
    return &stream->requests[0];
}

static cansub_ringbuffer *cansub_control_tx_source(cansub_stream_obj *base)
{
    cansub_stream_control *stream = CONTAINER_OF(base, cansub_stream_control, base);
    cansub_ringbuffer *result = NULL;

    if (CANSUB_STATE_IN(cansub_control_front(stream)->state, CANSUB_REQUEST_MASK_PENDING_TX))
    {
        result = &stream->request;
    }
    else if (!cansub_stream_control_requests_outstanding(stream) && cansub_control_abort_pending_tx(stream))
    {
        /* The abort goes out once no request is outstanding. */
        result = &stream->abort_slot.request;
    }

    return result;
}

static cansub_result cansub_control_tx_done(cansub_stream_obj *base)
{
    cansub_stream_control *stream = CONTAINER_OF(base, cansub_stream_control, base);
    cansub_request *const request = cansub_control_front(stream);
    cansub_control_abort *const slot = &stream->abort_slot;

    /* Same order as cansub_control_tx_source(). */
    if (CANSUB_STATE_IN(request->state, CANSUB_REQUEST_MASK_PENDING_TX))
    {
        if (cansub_ringbuffer_empty(&stream->request))
        {
            request->state = CANSUB_REQUEST_STATE_AWAITING;
        }
        else
        {
            request->state = CANSUB_REQUEST_STATE_SENDING;
        }
    }
    else if (!cansub_stream_control_requests_outstanding(stream) && cansub_control_abort_pending_tx(stream))
    {
        slot->state = cansub_ringbuffer_empty(&slot->request) ? CANSUB_REQUEST_STATE_AWAITING : CANSUB_REQUEST_STATE_SENDING;
    }

    return CANSUB_RESULT_OK;
}

static cansub_ringbuffer *cansub_control_rx_sink(cansub_stream_obj *base)
{
    cansub_stream_control *stream = CONTAINER_OF(base, cansub_stream_control, base);

    cansub_ringbuffer *result = NULL;

    if (CANSUB_STATE_IN(cansub_control_front(stream)->state, CANSUB_REQUEST_MASK_AWAITING))
    {
        result = &stream->response;
    }
    else if (CANSUB_STATE_IN(stream->abort_slot.state, CANSUB_REQUEST_MASK_AWAITING))
    {
        result = &stream->abort_slot.response;
    }

    return result;
}

static cansub_result cansub_control_rx_done(cansub_stream_obj *base)
{
    cansub_stream_control *stream = CONTAINER_OF(base, cansub_stream_control, base);
    cansub_request *const request = cansub_control_front(stream);

    if (!CANSUB_STATE_IN(request->state, CANSUB_REQUEST_MASK_AWAITING))
    {
        /* Not a request, so it is the response to the abort, see cansub_control_rx_sink(). */
        return CANSUB_STATE_IN(stream->abort_slot.state, CANSUB_REQUEST_MASK_AWAITING) ? cansub_control_abort_rx_done(stream) : CANSUB_RESULT_OK;
    }

    cansub_result result = cansub_control_parse(&request->http, &stream->response);
    if ((result == CANSUB_RESULT_OK) || (result == CANSUB_RESULT_CLOSED))
    {
        request->http_status = request->http.status;
        request->state = CANSUB_REQUEST_STATE_COMPLETE;
    }
    else if (result == CANSUB_RESULT_AGAIN)
    {
        /* Incomplete, wait for more data. */
        result = CANSUB_RESULT_OK;
    }

    /* Errors fail the request via cansub_control_failed(), it is still awaiting its response. */
    return result;
}

static cansub_ready cansub_control_ready(cansub_stream_obj const *base)
{
    cansub_stream_control const *stream = CONTAINER_OF_CONST(base, cansub_stream_control, base);
    cansub_request const *const request = cansub_control_front_const(stream);
    cansub_ready result = 0;

    if (CANSUB_STATE_IN(request->state, CANSUB_REQUEST_MASK_DONE))
    {
        result |= CANSUB_READY_CONTROL_RESPONSE;
    }
    else if (CANSUB_STATE_IN(request->state, CANSUB_REQUEST_MASK_FREE))
    {
        result |= CANSUB_READY_CONTROL_REQUEST;
    }

    return result;
}

static void cansub_control_failed(cansub_stream_obj *base, cansub_result reason)
{
    cansub_stream_control *stream = CONTAINER_OF(base, cansub_stream_control, base);
    cansub_request *const request = cansub_control_front(stream);

    /* Only fail a request in flight. A transport error while idle, or after the response, affects no request. */
    if (CANSUB_STATE_IN(request->state, CANSUB_REQUEST_MASK_OUTSTANDING))
    {
        request->state = CANSUB_REQUEST_STATE_FAILED;
        request->result = reason;
        request->http_status = 0;
    }

    /* The abort fails once written, or on a TLS error. Otherwise a queued abort waits for the next transport. */
    cansub_control_abort *const slot = &stream->abort_slot;
    if (CANSUB_STATE_IN(slot->state, CANSUB_REQUEST_MASK_IN_FLIGHT) ||
        (CANSUB_STATE_IN(slot->state, CANSUB_REQUEST_MASK_QUEUED) && (reason == CANSUB_RESULT_CONNECTION_TLS)))
    {
        slot->state = CANSUB_REQUEST_STATE_FREE;
    }
}

static void cansub_control_ended(cansub_stream_obj *base, bool opened, bool final)
{
    cansub_stream_control *stream = CONTAINER_OF(base, cansub_stream_control, base);
    cansub_request *const request = cansub_control_front(stream);

    if (CANSUB_STATE_IN(request->state, CANSUB_REQUEST_MASK_AWAITING))
    {
        if ((request->http.header_size != 0) && (request->http.framing == CANSUB_HTTP_BODY_UNTIL_CLOSE))
        {
            /* The body of a response without length ends with the transport. */
            request->http_status = request->http.status;
            request->state = CANSUB_REQUEST_STATE_COMPLETE;
        }
        else
        {
            /* Content-Length and chunked responses mark their own end, this one was cut off. */
            cansub_control_failed(base, CANSUB_RESULT_CONNECTION_LOST);
        }
    }
    else if (CANSUB_STATE_IN(request->state, CANSUB_REQUEST_MASK_SENDING))
    {
        cansub_control_failed(base, CANSUB_RESULT_CONNECTION_LOST);
    }
    else if (CANSUB_STATE_IN(request->state, CANSUB_REQUEST_MASK_QUEUED) && (!opened || final))
    {
        /* Could not be opened, or no new transport follows. Otherwise the request waits for a new one. */
        cansub_control_failed(base, CANSUB_RESULT_CONNECTION_LOST);
    }

    cansub_control_abort *const slot = &stream->abort_slot;
    if (CANSUB_STATE_IN(slot->state, CANSUB_REQUEST_MASK_AWAITING))
    {
        /* Written but not answered: failed. A response without length is complete with the transport, the abort is done either way. */
        slot->state = CANSUB_REQUEST_STATE_FREE;
    }
    else if (cansub_control_abort_pending_tx(stream))
    {
        if (!opened || final)
        {
            /* Could not be opened, or no new transport follows. */
            slot->state = CANSUB_REQUEST_STATE_FREE;
        }
        else
        {
            /* Not (completely) written, it is sent again in full on the next transport. */
            cansub_control_abort_rewind(stream);
        }
    }
}

static bool cansub_control_wants_transport(cansub_stream_obj const *base)
{
    cansub_stream_control const *stream = CONTAINER_OF_CONST(base, cansub_stream_control, base);

    return cansub_stream_control_requests_outstanding(stream) || cansub_stream_control_abort_in_progress(stream);
}

static uint8_t const *cansub_control_response_data(cansub_stream_control const *stream)
{
    /* The parser keeps the response linear, starting at the read position. */
    return stream->response.buffer + cansub_ringbuffer_mask(&stream->response, stream->response.read_index);
}

static bool cansub_control_abort_pending_tx(cansub_stream_control const *stream)
{
    return CANSUB_STATE_IN(stream->abort_slot.state, CANSUB_REQUEST_MASK_PENDING_TX);
}

static void cansub_control_abort_rewind(cansub_stream_control *stream)
{
    cansub_control_abort *const slot = &stream->abort_slot;

    /* The request was written once from index 0 and the buffer is never written again, so the bytes are still there. */
    slot->request.read_index = 0;

    cansub_ringbuffer_reset(&slot->response);
    memset(&slot->http, 0x00, sizeof(slot->http));

    slot->state = CANSUB_REQUEST_STATE_QUEUED;
}

static cansub_result cansub_control_abort_rx_done(cansub_stream_control *stream)
{
    cansub_control_abort *const slot = &stream->abort_slot;

    cansub_result result = cansub_control_parse(&slot->http, &slot->response);
    if ((result == CANSUB_RESULT_OK) || (result == CANSUB_RESULT_CLOSED))
    {
        /* Answered. The status is not used: the remote confirms the close on the data stream. */
        slot->state = CANSUB_REQUEST_STATE_FREE;
    }
    else if (result == CANSUB_RESULT_AGAIN)
    {
        /* Incomplete, wait for more data. */
        result = CANSUB_RESULT_OK;
    }

    /* Errors fail the abort via cansub_control_failed(). */
    return result;
}

static cansub_result cansub_control_parse(cansub_http_response *http, cansub_ringbuffer *response)
{
    cansub_result result = cansub_http_parse_response(http, response);

    if (result == CANSUB_RESULT_OK)
    {
        if ((http->status >= 100) && (http->status < 200))
        {
            /* Nothing asks for an interim response on this transport: no Expect: 100-continue and no upgrade.
             * RFC 9110 lets a client skip unexpected 1xx responses; the device never sends them, so reject instead. */
            result = CANSUB_RESULT_CONNECTION_PROTOCOL;
        }
        else if (http->close)
        {
            /* The remote closes the transport after this response, close it from this end as well. */
            result = CANSUB_RESULT_CLOSED;
        }
    }

    return result;
}
