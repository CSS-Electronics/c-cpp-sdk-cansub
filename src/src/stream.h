/* SPDX-License-Identifier: MIT */

#ifndef CANSUB_STREAM_H
#define CANSUB_STREAM_H

/***********************************************************************************************************************
* Includes.
***********************************************************************************************************************/
/* Standard includes. */

/* Project includes. */
#include "common.h"

/***********************************************************************************************************************
* Public definitions.
***********************************************************************************************************************/

/***********************************************************************************************************************
* Public type declarations.
***********************************************************************************************************************/

typedef struct cansub_stream_obj cansub_stream_obj;

typedef struct cansub_stream_vtable
{
    /** Plaintext waiting to be sent, NULL if nothing. */
    cansub_ringbuffer *(*tx_source)(cansub_stream_obj *base);
    /** Some of tx_source was handed to TLS (QUEUED -> SENDING, SENDING -> AWAITING when empty).
     * CANSUB_RESULT_OK, CANSUB_RESULT_CLOSED when done with the transport, or a CONNECTION_* code. */
    cansub_result (*tx_done)(cansub_stream_obj *base);
    /** Where received plaintext goes, NULL if not accepting. */
    cansub_ringbuffer *(*rx_sink)(cansub_stream_obj *base);
    /** New bytes in rx_sink: parse. CANSUB_RESULT_OK or a CONNECTION_* code. */
    cansub_result (*rx_done)(cansub_stream_obj *base);
    /** Get the current ready state of the stream. */
    cansub_ready (*ready)(cansub_stream_obj const *base);
    /** Fatal error on the current transport. */
    void (*failed)(cansub_stream_obj *base, cansub_result reason);
    /** The current transport ended. opened is false if it never connected. */
    void (*ended)(cansub_stream_obj *base, bool opened, bool final);
    /** Whether the stream needs a transport right now. */
    bool (*wants_transport)(cansub_stream_obj const *base);
} cansub_stream_vtable;

struct cansub_stream_obj
{
    cansub_stream_vtable const *vtable;
};

/***********************************************************************************************************************
* Public variable declarations.
***********************************************************************************************************************/

/***********************************************************************************************************************
* Public function declarations.
***********************************************************************************************************************/

static inline cansub_ringbuffer *cansub_stream_tx_source(cansub_stream_obj *base)
{
    return base->vtable->tx_source(base);
}

static inline cansub_result cansub_stream_tx_done(cansub_stream_obj *base)
{
    return base->vtable->tx_done(base);
}

static inline cansub_ringbuffer *cansub_stream_rx_sink(cansub_stream_obj *base)
{
    return base->vtable->rx_sink(base);
}

static inline cansub_result cansub_stream_rx_done(cansub_stream_obj *base)
{
    return base->vtable->rx_done(base);
}

static inline cansub_ready cansub_stream_ready(cansub_stream_obj const *base)
{
    return base->vtable->ready(base);
}

static inline void cansub_stream_failed(cansub_stream_obj *base, cansub_result reason)
{
    base->vtable->failed(base, reason);
}
static inline void cansub_stream_ended(cansub_stream_obj *base, bool opened, bool final)
{
    base->vtable->ended(base, opened, final);
}
static inline bool cansub_stream_wants_transport(cansub_stream_obj const *base)
{
    return base->vtable->wants_transport(base);
}

#endif