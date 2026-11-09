/* SPDX-License-Identifier: MIT */

/***********************************************************************************************************************
 * Includes.
 **********************************************************************************************************************/
/* Standard includes. */
#include <assert.h>

/* Third-party includes. */
#include <mbedtls/ssl.h>
#include <mbedtls/threading.h>

/* Project includes. */
#include "common.h"
#include "transport.h"

/***********************************************************************************************************************
 * External declarations.
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Public variable definition.
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Private definitions.
 **********************************************************************************************************************/

/* Transport state masks for CANSUB_STATE_IN(). */
#define CANSUB_TRANSPORT_MASK_IDLE          CANSUB_STATE_BIT(CANSUB_TRANSPORT_STATE_IDLE)
#define CANSUB_TRANSPORT_MASK_ENDED         CANSUB_STATE_BIT(CANSUB_TRANSPORT_STATE_ENDED)
#define CANSUB_TRANSPORT_MASK_REQUESTED     CANSUB_STATE_BIT(CANSUB_TRANSPORT_STATE_REQUESTED)
#define CANSUB_TRANSPORT_MASK_CONNECTED     CANSUB_STATE_BIT(CANSUB_TRANSPORT_STATE_CONNECTED)
#define CANSUB_TRANSPORT_MASK_CLOSE_NOTIFY  CANSUB_STATE_BIT(CANSUB_TRANSPORT_STATE_CLOSE_NOTIFY)
#define CANSUB_TRANSPORT_MASK_CLOSE_FLUSH   CANSUB_STATE_BIT(CANSUB_TRANSPORT_STATE_CLOSE_FLUSH)
#define CANSUB_TRANSPORT_MASK_CLOSE_PENDING CANSUB_STATE_BIT(CANSUB_TRANSPORT_STATE_CLOSE_PENDING)
_Static_assert(CANSUB_TRANSPORT_STATE_COUNT < 32, "Too many states");

/** Not requested, connected or closing, whether or not connected before. */
#define CANSUB_TRANSPORT_MASK_UNUSED        (CANSUB_TRANSPORT_MASK_IDLE | CANSUB_TRANSPORT_MASK_ENDED)
/** The close_notify is not completely handed to the tx buffer yet. */
#define CANSUB_TRANSPORT_MASK_CLOSE_WRITING (CANSUB_TRANSPORT_MASK_CLOSE_NOTIFY | CANSUB_TRANSPORT_MASK_CLOSE_FLUSH)
/** Closing, waiting to be drained and ended with EOF. */
#define CANSUB_TRANSPORT_MASK_CLOSING       (CANSUB_TRANSPORT_MASK_CLOSE_WRITING | CANSUB_TRANSPORT_MASK_CLOSE_PENDING)
/** No more data is carried. */
#define CANSUB_TRANSPORT_MASK_DONE          (CANSUB_TRANSPORT_MASK_CLOSE_PENDING | CANSUB_TRANSPORT_MASK_ENDED)
/** May be aborted right away, a requested or closing transport is handled once connected or drained. */
#define CANSUB_TRANSPORT_MASK_ABORTABLE     (CANSUB_TRANSPORT_MASK_IDLE | CANSUB_TRANSPORT_MASK_CONNECTED)

/* Each stream has the same transport flags in its own byte, so the DATA flags shifted by the stream select them. */
_Static_assert((CANSUB_READY_CONTROL_SINK == (CANSUB_READY_DATA_SINK << CANSUB_READY_STREAM_SHIFT(CANSUB_STREAM_CONTROL))) &&
               (CANSUB_READY_CONTROL_DRAIN == (CANSUB_READY_DATA_DRAIN << CANSUB_READY_STREAM_SHIFT(CANSUB_STREAM_CONTROL))) &&
               (CANSUB_READY_CONTROL_TRANSPORT_CLOSE == (CANSUB_READY_DATA_TRANSPORT_CLOSE << CANSUB_READY_STREAM_SHIFT(CANSUB_STREAM_CONTROL))) &&
               (CANSUB_READY_CONTROL_TRANSPORT_OPEN == (CANSUB_READY_DATA_TRANSPORT_OPEN << CANSUB_READY_STREAM_SHIFT(CANSUB_STREAM_CONTROL))) &&
               (CANSUB_READY_STREAM_SHIFT(CANSUB_STREAM_DATA) == 0), "Transport flags must be shifted per stream");
_Static_assert(CANSUB_RINGBUFFER_SIZE_EXACT(CANSUB_TRANSPORT_BUFFER_SIZE), "Transport buffer size must be a power of two");
_Static_assert(((CANSUB_READY_DATA_SINK | CANSUB_READY_DATA_DRAIN | CANSUB_READY_DATA_TRANSPORT_CLOSE | CANSUB_READY_DATA_TRANSPORT_OPEN) &
                ~CANSUB_READY_STREAM_MASK) == 0, "Transport flags must fit into one byte");

/***********************************************************************************************************************
 * Private type definitions.
 **********************************************************************************************************************/

/***********************************************************************************************************************
* Private variable definitions.
***********************************************************************************************************************/

/***********************************************************************************************************************
 * Private function declarations.
 **********************************************************************************************************************/

static cansub_result cansub_transport_map_error(cansub_transport *transport, int mbed_status);

/**
 * Move the TLS close_notify of a closing transport forward, as far as tx has room.
 *
 * Moves CLOSE_NOTIFY or CLOSE_FLUSH to CLOSE_PENDING once everything is written.
 *
 * @param transport Transport.
 */
static void cansub_transport_close_notify(cansub_transport *transport);

static int cansub_tls_recv(void *p, unsigned char *buf, size_t len);
static int cansub_tls_send(void *p, unsigned char const *buf, size_t len);

/***********************************************************************************************************************
* Public function definitions.
***********************************************************************************************************************/

cansub_result cansub_transport_library_init(void)
{
#if defined(MBEDTLS_THREADING_ALT)
    /* Before any other mbedTLS call, this also initializes the global mutexes of mbedTLS. */
    cansub_threading_alt_set();
#endif

    cansub_result const result = cansub_transport_result_from_mbedtls(psa_crypto_init(), CANSUB_RESULT_ERROR);

#if defined(MBEDTLS_THREADING_ALT)
    if (result != CANSUB_RESULT_OK)
    {
        /* No deinit follows a failed init, and the next init initializes the global mutexes again. */
        mbedtls_threading_free_alt();
    }
#endif

    return result;
}

void cansub_transport_library_deinit(void)
{
    mbedtls_psa_crypto_free();

#if defined(MBEDTLS_THREADING_ALT)
    /* After the last mbedTLS call. */
    mbedtls_threading_free_alt();
#endif
}

cansub_result cansub_transport_result_from_mbedtls(int status, cansub_result failure)
{
    cansub_result result = failure;

    if (status == 0)
    {
        result = CANSUB_RESULT_OK;
    }
    else if (status == PSA_ERROR_INSUFFICIENT_MEMORY)
    {
        /* Every mbedTLS allocation failure maps to this PSA code. */
        result = CANSUB_RESULT_MEMORY;
    }

    return result;
}

cansub_result cansub_transport_init(cansub_transport *transport, cansub_stream stream, mbedtls_ssl_config *ssl_config)
{
    assert(transport);

    cansub_result result = CANSUB_RESULT_ERROR;

    do
    {
        /* Need this up here to ensure that the release is idempotent. */
        int const mbed_status = mbedtls_ssl_setup(&transport->ssl_context, ssl_config);
        result = cansub_transport_result_from_mbedtls(mbed_status, CANSUB_RESULT_ERROR);
        if (result != CANSUB_RESULT_OK)
        {
            break;
        }

        result = cansub_ringbuffer_init(&transport->rx, CANSUB_TRANSPORT_BUFFER_SIZE);
        if (result != CANSUB_RESULT_OK)
        {
            break;
        }

        result = cansub_ringbuffer_init(&transport->tx, CANSUB_TRANSPORT_BUFFER_SIZE);
        if (result != CANSUB_RESULT_OK)
        {
            break;
        }

        transport->state = CANSUB_TRANSPORT_STATE_IDLE;
        transport->stream = stream;
    } while (0);

    return result;
}

void cansub_transport_destroy(cansub_transport *transport)
{
    assert(transport);

    mbedtls_ssl_free(&transport->ssl_context);

    cansub_ringbuffer_destroy(&transport->rx);
    cansub_ringbuffer_destroy(&transport->tx);
}

cansub_ready cansub_transport_ready(cansub_transport const *transport)
{
    assert(transport);

    /* Built with the DATA flags, then moved to the byte of the stream. */
    cansub_ready result = 0;

    if (CANSUB_STATE_IN(transport->state, CANSUB_TRANSPORT_MASK_REQUESTED))
    {
        result = CANSUB_READY_DATA_TRANSPORT_OPEN;
    }
    else if (cansub_transport_is_closing(transport))
    {
        result |= CANSUB_READY_DATA_TRANSPORT_CLOSE;

        /* While the close_notify is incomplete, draining makes room for the rest of it. */
        if (!cansub_ringbuffer_empty(&transport->tx) || CANSUB_STATE_IN(transport->state, CANSUB_TRANSPORT_MASK_CLOSE_WRITING))
        {
            result |= CANSUB_READY_DATA_DRAIN;
        }
    }
    else if (CANSUB_STATE_IN(transport->state, CANSUB_TRANSPORT_MASK_CONNECTED))
    {
        if (!cansub_ringbuffer_empty(&transport->tx))
        {
            result |= CANSUB_READY_DATA_DRAIN;
        }
        if (!cansub_ringbuffer_full(&transport->rx))
        {
            result |= CANSUB_READY_DATA_SINK;
        }
    }

    result <<= CANSUB_READY_STREAM_SHIFT(transport->stream);

    return result;
}

cansub_tls_status cansub_transport_progress(cansub_transport *transport)
{
    cansub_tls_status result = CANSUB_TLS_INACTIVE;

    do
    {
        if (!CANSUB_STATE_IN(transport->state, CANSUB_TRANSPORT_MASK_CONNECTED))
        {
            break;
        }

        if (mbedtls_ssl_is_handshake_over(&transport->ssl_context))
        {
            result = CANSUB_TLS_ESTABLISHED;
            break;
        }

        int const mbed_status = mbedtls_ssl_handshake(&transport->ssl_context);
        if (mbed_status == 0)
        {
            result = CANSUB_TLS_ESTABLISHED;
        }
        else if (mbed_status == MBEDTLS_ERR_SSL_WANT_READ || mbed_status == MBEDTLS_ERR_SSL_WANT_WRITE)
        {
            result = CANSUB_TLS_HANDSHAKING;
        }
        else
        {
            transport->tls_error = mbed_status;
            transport->state = CANSUB_TRANSPORT_STATE_CLOSE_PENDING;
            result = CANSUB_TLS_FAILED;
        }
    } while (0);

    return result;
}

cansub_result cansub_transport_read(cansub_transport *transport, cansub_ringbuffer *ringbuffer)
{
    cansub_result result = CANSUB_RESULT_CONNECTION_TLS;

    /* The mbedTLS read calls works on buffer spans. One span per call, the caller loops for the second one.
     * An error or close behind a full first span is then returned by the next call, not lost. */
    cansub_ringbuffer_spans const spans = cansub_ringbuffer_spans_write(ringbuffer);

    if (spans.span_1_size == 0)
    {
        /* No space, mbedtls_ssl_read() with length 0 would return 0. */
        result = CANSUB_RESULT_AGAIN;
    }
    else
    {
        int const bytes_read = mbedtls_ssl_read(&transport->ssl_context, spans.span_1, spans.span_1_size);
        if (bytes_read < 0)
        {
            result = cansub_transport_map_error(transport, bytes_read);
        }
        else
        {
            cansub_ringbuffer_commit_write(ringbuffer, (size_t)bytes_read);
            result = bytes_read;
        }
    }

    return result;
}

cansub_result cansub_transport_write(cansub_transport *transport, cansub_ringbuffer *ringbuffer)
{
    cansub_result result = CANSUB_RESULT_CONNECTION_TLS;

    /* The mbedTLS write calls works on buffer spans. One span per call, the caller loops for the second one. */
    cansub_ringbuffer_spans const spans = cansub_ringbuffer_spans_read(ringbuffer);
    size_t length = spans.span_1_size;

    if (transport->tx_pending)
    {
        /* After WANT_WRITE, mbedTLS has encrypted the data already and only flushes it, returning the length passed.
         * The data is still uncommitted at the read position and the ringbuffer only appends, so the first span is
         * never shorter. MIN only guards the memory access. */
        length = MIN(transport->tx_pending, spans.span_1_size);
    }

    int const bytes_written = mbedtls_ssl_write(&transport->ssl_context, spans.span_1, length);
    if (bytes_written < 0)
    {
        result = cansub_transport_map_error(transport, bytes_written);
        if (result == CANSUB_RESULT_AGAIN)
        {
            transport->tx_pending = length;
        }
    }
    else
    {
        transport->tx_pending = 0;
        result = bytes_written;
    }

    if (result > 0)
    {
        cansub_ringbuffer_commit_read(ringbuffer, result);
    }

    return result;
}

cansub_result cansub_transport_connect(cansub_transport *transport)
{
    assert(transport);

    cansub_result result = CANSUB_RESULT_ERROR;

    do
    {
        if (!cansub_transport_is_idle(transport))
        {
            result = CANSUB_RESULT_INVALID_STATE;
            break;
        }

        transport->state = CANSUB_TRANSPORT_STATE_REQUESTED;

        result = CANSUB_RESULT_OK;
    } while (0);

    return result;
}

cansub_result cansub_transport_connected(cansub_transport *transport, char const *host)
{
    assert(transport);

    cansub_result result = CANSUB_RESULT_ERROR;
    int mbedtls_status;

    do
    {
        if (!CANSUB_STATE_IN(transport->state, CANSUB_TRANSPORT_MASK_REQUESTED))
        {
            result = CANSUB_RESULT_INVALID_STATE;
            break;
        }

        /* Ensure we start a clean TLS session. */
        mbedtls_status = mbedtls_ssl_session_reset(&transport->ssl_context);
        result = cansub_transport_result_from_mbedtls(mbedtls_status, CANSUB_RESULT_ERROR);
        if (result != CANSUB_RESULT_OK)
        {
            break;
        }

        /* The TLS server name, and the name the device certificate is checked against. The verify callback of the
         * TLS configuration may replace that check. A name longer than mbedTLS accepts is invalid input. */
        mbedtls_status = mbedtls_ssl_set_hostname(&transport->ssl_context, host);
        result = cansub_transport_result_from_mbedtls(mbedtls_status, CANSUB_RESULT_INVALID_ARG);
        if (result != CANSUB_RESULT_OK)
        {
            break;
        }

        mbedtls_ssl_set_bio(&transport->ssl_context, transport, cansub_tls_send, cansub_tls_recv, NULL);

        transport->state = CANSUB_TRANSPORT_STATE_CONNECTED;
        transport->tls_error = 0;
        transport->tx_pending = 0;

        result = CANSUB_RESULT_OK;
    } while (0);

    return result;
}

cansub_result cansub_transport_close(cansub_transport *transport)
{
    assert(transport);

    cansub_result result = CANSUB_RESULT_ERROR;

    do
    {
        if (!CANSUB_STATE_IN(transport->state, CANSUB_TRANSPORT_MASK_CONNECTED))
        {
            result = CANSUB_RESULT_INVALID_STATE;
            break;
        }

        transport->state = CANSUB_TRANSPORT_STATE_CLOSE_NOTIFY;

        /* Usually written right away, otherwise cansub_transport_drain() continues it. */
        cansub_transport_close_notify(transport);

        result = CANSUB_RESULT_OK;
    } while (0);

    return result;
}

cansub_result cansub_transport_sink(cansub_transport *transport, void const *buffer, size_t buffer_size)
{
    assert(transport);
    assert(buffer || !buffer_size);

    cansub_result result = buffer ? (cansub_result)cansub_ringbuffer_write(&transport->rx, buffer, buffer_size) : 0;
    if ((result == 0) && (buffer_size != 0))
    {
        /* rx is full, the stream has to consume it first. */
        result = CANSUB_RESULT_AGAIN;
    }

    return result;
}

cansub_result cansub_transport_sink_space(cansub_transport const *transport)
{
    assert(transport);

    return (cansub_result)cansub_ringbuffer_space(&transport->rx);
}

cansub_result cansub_transport_drain(cansub_transport *transport, void *buffer, size_t buffer_size)
{
    /* A drain is a peek whose bytes are removed at once. */
    cansub_result const result = cansub_transport_drain_peek(transport, buffer, buffer_size);
    if (result > 0)
    {
        (void)cansub_ringbuffer_commit_read(&transport->tx, (size_t)result);
    }

    return result;
}

cansub_result cansub_transport_drain_peek(cansub_transport *transport, void *buffer, size_t buffer_size)
{
    assert(transport);
    assert(buffer || !buffer_size);

    cansub_result result = CANSUB_RESULT_ERROR;

    do
    {
        /* Fill tx with the rest of the close_notify before handing it out. */
        cansub_transport_close_notify(transport);

        uint32_t const copied = buffer ? cansub_ringbuffer_peek(&transport->tx, buffer, buffer_size) : 0;
        if ((copied != 0) || !cansub_ringbuffer_empty(&transport->tx))
        {
            /* Bytes drained, or none requested while data is pending. */
            result = (cansub_result)copied;
            break;
        }

        /* Nothing left. CLOSED only once the close_notify is written completely, or after EOF. */
        result = cansub_transport_has_ended(transport) ? CANSUB_RESULT_CLOSED : CANSUB_RESULT_AGAIN;
    } while (0);

    return result;
}

cansub_result cansub_transport_drain_consume(cansub_transport *transport, size_t size)
{
    assert(transport);

    if (cansub_ringbuffer_empty(&transport->tx))
    {
        /* Same as cansub_transport_drain(): CLOSED only once the close_notify is written completely, or after EOF. */
        return cansub_transport_has_ended(transport) ? CANSUB_RESULT_CLOSED : CANSUB_RESULT_AGAIN;
    }

    cansub_result const removed = (cansub_result)cansub_ringbuffer_commit_read(&transport->tx, size);

    /* Only after removing, so no byte the glue has not peeked is removed. */
    cansub_transport_close_notify(transport);

    return removed;
}

cansub_result cansub_transport_abort(cansub_transport *transport)
{
    assert(transport);

    if (CANSUB_STATE_IN(transport->state, CANSUB_TRANSPORT_MASK_ABORTABLE))
    {
        /* Pending data in both directions is discarded. */
        transport->state = CANSUB_TRANSPORT_STATE_CLOSE_PENDING;

        cansub_ringbuffer_reset(&transport->rx);
        cansub_ringbuffer_reset(&transport->tx);
    }

    /* A requested transport is closed once connected, a closing transport is already closing. */
    return CANSUB_RESULT_OK;
}

cansub_result cansub_transport_eof(cansub_transport *transport)
{
    assert(transport);

    cansub_result result = CANSUB_RESULT_ERROR;

    do
    {
        if (cansub_transport_is_idle(transport))
        {
            result = CANSUB_RESULT_INVALID_STATE;
            break;
        }

        transport->state = CANSUB_TRANSPORT_STATE_ENDED;
        transport->tx_pending = 0;

        cansub_ringbuffer_reset(&transport->rx);
        cansub_ringbuffer_reset(&transport->tx);

        result = CANSUB_RESULT_OK;
    } while (0);

    return result;
}

bool cansub_transport_is_idle(cansub_transport const *transport)
{
    return CANSUB_STATE_IN(transport->state, CANSUB_TRANSPORT_MASK_UNUSED);
}

bool cansub_transport_is_connected(cansub_transport const *transport)
{
    return CANSUB_STATE_IN(transport->state, CANSUB_TRANSPORT_MASK_CONNECTED);
}

bool cansub_transport_is_closing(cansub_transport const *transport)
{
    return CANSUB_STATE_IN(transport->state, CANSUB_TRANSPORT_MASK_CLOSING);
}

bool cansub_transport_has_ended(cansub_transport const *transport)
{
    return CANSUB_STATE_IN(transport->state, CANSUB_TRANSPORT_MASK_DONE);
}

bool cansub_transport_is_established(cansub_transport *transport)
{
    return (cansub_transport_is_connected(transport) || cansub_transport_is_closing(transport)) &&
           mbedtls_ssl_is_handshake_over(&transport->ssl_context);
}

/***********************************************************************************************************************
* Private function definitions.
***********************************************************************************************************************/

static cansub_result cansub_transport_map_error(cansub_transport *transport, int mbed_status)
{
    cansub_result result = CANSUB_RESULT_CONNECTION_TLS;

    if (mbed_status == MBEDTLS_ERR_SSL_WANT_READ || mbed_status == MBEDTLS_ERR_SSL_WANT_WRITE)
    {
        result = CANSUB_RESULT_AGAIN;
    }
    else if (mbed_status == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY)
    {
        result = CANSUB_RESULT_CLOSED;
    }
    else
    {
        transport->tls_error = mbed_status;
        transport->state = CANSUB_TRANSPORT_STATE_CLOSE_PENDING;
    }

    return result;
}

static void cansub_transport_close_notify(cansub_transport *transport)
{
    /* mbedtls_ssl_close_notify() first flushes a record still in progress:
     * - An application write which returned WANT_WRITE (tx_pending)
     * - The partially written alert (CLOSE_FLUSH).
     * Only without one does it queue the alert. */
    while (CANSUB_STATE_IN(transport->state, CANSUB_TRANSPORT_MASK_CLOSE_WRITING))
    {
        bool const queues_alert = CANSUB_STATE_IN(transport->state, CANSUB_TRANSPORT_MASK_CLOSE_NOTIFY) && (transport->tx_pending == 0);

        int const ret = mbedtls_ssl_close_notify(&transport->ssl_context);
        if (ret == MBEDTLS_ERR_SSL_WANT_WRITE)
        {
            /* tx is full, continued by the next drain. A queued alert is now partially written. */
            if (queues_alert)
            {
                transport->state = CANSUB_TRANSPORT_STATE_CLOSE_FLUSH;
            }
            break;
        }

        if ((ret != 0) || queues_alert || CANSUB_STATE_IN(transport->state, CANSUB_TRANSPORT_MASK_CLOSE_FLUSH))
        {
            /* The alert is written (or none is sent before the handshake is over), or it failed: the close_notify is
             * best effort, the transport ends either way. */
            transport->state = CANSUB_TRANSPORT_STATE_CLOSE_PENDING;
            break;
        }

        /* Only the stuck application record was flushed, its plaintext is abandoned. Queue the alert next. */
        transport->tx_pending = 0;
    }
}


/***********************************************************************************************************************
* Private function definitions - mbedTLS sans-IO/BIO layer.
***********************************************************************************************************************/

static int cansub_tls_send(void *p, unsigned char const *buf, size_t len)
{
    cansub_transport *const transport = p;

    size_t sent = cansub_ringbuffer_write(&transport->tx, buf, len);

    /* Partial sends are allowed, mbedTLS retries the rest. */
    return sent ? (int) sent : MBEDTLS_ERR_SSL_WANT_WRITE;
}

static int cansub_tls_recv(void *p, unsigned char *buf, size_t len)
{
    cansub_transport *const transport = p;

    size_t received = cansub_ringbuffer_read(&transport->rx, buf, len);

    /* Never return 0 for "no data": 0 means the connection was closed. */
    return received ? (int) received : MBEDTLS_ERR_SSL_WANT_READ;
}
