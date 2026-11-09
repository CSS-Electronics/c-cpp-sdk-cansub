/* SPDX-License-Identifier: MIT */

#ifndef CANSUB_STREAM_CONTROL_H
#define CANSUB_STREAM_CONTROL_H

/***********************************************************************************************************************
* Includes.
***********************************************************************************************************************/
/* Standard includes. */

/* Project includes. */
#include "common.h"
#include "http.h"
#include "stream.h"

/***********************************************************************************************************************
* Public definitions.
***********************************************************************************************************************/

/** Size of the request buffer and of the response buffer, each including its HTTP headers. */
#define CANSUB_CONTROL_HTTP_BUFFER_SIZE (4096 * 4)

/***********************************************************************************************************************
* Public type declarations.
***********************************************************************************************************************/

typedef enum
{
    CANSUB_REQUEST_STATE_FREE = 0,
    CANSUB_REQUEST_STATE_QUEUED,
    CANSUB_REQUEST_STATE_SENDING,
    CANSUB_REQUEST_STATE_AWAITING,
    CANSUB_REQUEST_STATE_COMPLETE,
    CANSUB_REQUEST_STATE_FAILED,
    CANSUB_REQUEST_STATE_COUNT,
} cansub_request_state;

typedef struct cansub_request
{
    cansub_request_state state;
    uint64_t tag;
    uint32_t http_status;
    /* CONNECTION_* code of a failed request. */
    cansub_result result;
    /* Parse state and result of the response, offsets into the response buffer. */
    cansub_http_response http;
} cansub_request;

typedef struct cansub_control_abort
{
    /* The abort is a request without a response for the caller, so it shares the request states. It only uses FREE (no
     * abort in progress), QUEUED, SENDING and AWAITING, and goes back to FREE once answered or failed. */
    cansub_request_state state;
    /* Serialized request, written once from the start of the buffer so it can be rewound for a new transport. */
    cansub_ringbuffer request;
    cansub_ringbuffer response;
    cansub_http_response http;
} cansub_control_abort;

typedef struct cansub_stream_control
{
    cansub_stream_obj base;

    cansub_ringbuffer response;
    cansub_ringbuffer request;

    cansub_request requests[1];

    cansub_control_abort abort_slot;
} cansub_stream_control;

/***********************************************************************************************************************
* Public variable declarations.
***********************************************************************************************************************/

/***********************************************************************************************************************
* Public function declarations.
***********************************************************************************************************************/

cansub_result cansub_stream_control_init(cansub_stream_control *stream);
void cansub_stream_control_destroy(cansub_stream_control *stream);

cansub_result cansub_stream_control_add_request(cansub_stream_control *stream, uint64_t tag, cansub_http_request const *config);
cansub_result cansub_stream_control_read_request(cansub_stream_control *stream, uint64_t *tag, uint32_t *http_status, void *buffer, size_t buffer_size);
cansub_result cansub_stream_control_peek_request(cansub_stream_control const *stream, uint64_t *tag, uint32_t *http_status);
cansub_result cansub_stream_control_read_header(cansub_stream_control const *stream, char const *name, char *buffer, size_t buffer_size);

bool cansub_stream_control_requests_outstanding(cansub_stream_control const *stream);
cansub_result cansub_stream_control_in_flight(cansub_stream_control const *stream, uint64_t *tag);
cansub_result cansub_stream_control_abort_start(cansub_stream_control *stream, uint32_t channel, char const *host);
bool cansub_stream_control_abort_in_progress(cansub_stream_control const *stream);
void cansub_stream_control_abort_cancel(cansub_stream_control *stream);

#endif