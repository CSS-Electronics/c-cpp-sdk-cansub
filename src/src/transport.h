/* SPDX-License-Identifier: MIT */

#ifndef CANSUB_TRANSPORT_H
#define CANSUB_TRANSPORT_H

/***********************************************************************************************************************
* Includes.
***********************************************************************************************************************/
/* Standard includes. */

#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>

/* Project includes. */
#include "common.h"
#include "ringbuffer.h"

/***********************************************************************************************************************
* Public definitions.
***********************************************************************************************************************/

/** Size of each TLS buffer of a transport: Received bytes waiting for TLS, and encrypted bytes waiting to be drained. */
#define CANSUB_TRANSPORT_BUFFER_SIZE (4096)

/***********************************************************************************************************************
* Public type declarations.
***********************************************************************************************************************/

typedef enum
{
    /** The initial idle state. The transport is not connected and has never been connected. */
    CANSUB_TRANSPORT_STATE_IDLE = 0,
    /** The idle state after a close. The transport is not connected but has been connected. */
    CANSUB_TRANSPORT_STATE_ENDED,
    /** The transport is requested to be connected. */
    CANSUB_TRANSPORT_STATE_REQUESTED,
    /** The transport is connected. */
    CANSUB_TRANSPORT_STATE_CONNECTED,
    /** During transport closure, the TLS close_notify is not handed to mbedTLS yet. A stuck application record is
     * flushed first. Draining moves it forward. */
    CANSUB_TRANSPORT_STATE_CLOSE_NOTIFY,
    /** During transport closure, the TLS close_notify is handed to mbedTLS but not fully written. Draining moves it forward. */
    CANSUB_TRANSPORT_STATE_CLOSE_FLUSH,
    /** All close data is in the tx buffer, only draining is left. */
    CANSUB_TRANSPORT_STATE_CLOSE_PENDING,
    CANSUB_TRANSPORT_STATE_COUNT,
} cansub_transport_state;

typedef enum
{
    CANSUB_TLS_INACTIVE = 0,
    CANSUB_TLS_HANDSHAKING,
    CANSUB_TLS_ESTABLISHED,
    CANSUB_TLS_FAILED,
} cansub_tls_status;

typedef struct cansub_transport
{
    cansub_ringbuffer rx;
    cansub_ringbuffer tx;

    mbedtls_ssl_context ssl_context;
    int tls_error;
    size_t tx_pending;

    cansub_transport_state state;

    /** Selects the byte of the ready flags, see cansub_transport_ready(). */
    cansub_stream stream;
} cansub_transport;

/***********************************************************************************************************************
* Public variable declarations.
***********************************************************************************************************************/

/***********************************************************************************************************************
* Public function declarations.
***********************************************************************************************************************/

cansub_result cansub_transport_library_init(void);
void cansub_transport_library_deinit(void);

/**
 * Map the status of an mbedTLS or PSA call that does not do TLS I/O.
 *
 * Only 0 is success, so e.g. the positive "certificates failed" count of mbedtls_x509_crt_parse() is a failure.
 *
 * @param status mbedTLS or PSA status.
 * @param failure Result for any failure other than running out of memory.
 * @return CANSUB_RESULT_OK, CANSUB_RESULT_MEMORY, or failure.
 */
cansub_result cansub_transport_result_from_mbedtls(int status, cansub_result failure);

cansub_result cansub_transport_init(cansub_transport *transport, cansub_stream stream, mbedtls_ssl_config *ssl_config);
void cansub_transport_destroy(cansub_transport *transport);

cansub_result cansub_transport_connect(cansub_transport *transport);

cansub_ready cansub_transport_ready(cansub_transport const *transport);

cansub_tls_status cansub_transport_progress(cansub_transport *transport);

cansub_result cansub_transport_read(cansub_transport *transport, cansub_ringbuffer *ringbuffer);
cansub_result cansub_transport_write(cansub_transport *transport, cansub_ringbuffer *ringbuffer);

cansub_result cansub_transport_connected(cansub_transport *transport, char const *host);

/**
 * Close a connected transport (TRANSPORT_CLOSE).
 *
 * @param transport Transport.
 * @return CANSUB_RESULT_OK, or CANSUB_RESULT_INVALID_STATE if not connected (e.g. after a fatal TLS error).
 */
cansub_result cansub_transport_close(cansub_transport *transport);

cansub_result cansub_transport_abort(cansub_transport *transport);

/**
 * Sink encrypted data received from the transport.
 *
 * @param transport Transport.
 * @param buffer Source, may be NULL if buffer_size is 0.
 * @param buffer_size Size of the source.
 * @return Number of bytes sunk, which may be less than buffer_size, 0 if buffer_size is 0, or CANSUB_RESULT_AGAIN
 *         while the rx buffer is full.
 */
cansub_result cansub_transport_sink(cansub_transport *transport, void const *buffer, size_t buffer_size);

/**
 * Determine how much space is available in the sink buffer.
 *
 * @param transport Transport.
 * @return Space available in the sink buffer.
 */
cansub_result cansub_transport_sink_space(cansub_transport const *transport);

/**
 * Drain the encrypted data to forward to the transport.
 *
 * @param transport Transport.
 * @param[out] buffer Destination, may be NULL if buffer_size is 0.
 * @param buffer_size Size of the destination. At most the transport buffer size.
 * @return Number of bytes drained, CANSUB_RESULT_CLOSED once the transport has ended and everything is drained,
 *         or CANSUB_RESULT_AGAIN.
 */
cansub_result cansub_transport_drain(cansub_transport *transport, void *buffer, size_t buffer_size);

/**
 * Peek the encrypted data to forward to the transport. Does not consume/drain the data.
 *
 * @param transport Transport.
 * @param[out] buffer Destination, may be NULL if buffer_size is 0.
 * @param buffer_size Size of the destination. At most the transport buffer size.
 * @return Result. Number of bytes peeked for 0 or positive values.
 * @retval CANSUB_RESULT_AGAIN Nothing pending.
 * @retval CANSUB_RESULT_CLOSED once the transport has ended and everything is drained.
 */
cansub_result cansub_transport_drain_peek(cansub_transport *transport, void *buffer, size_t buffer_size);

/**
 * Consume previously peeked data.
 *
 * @param transport Transport.
 * @param size Bytes to consume as drained. At most the transport buffer size.
 * @return Result, bytes consumed or an error code.
 */
cansub_result cansub_transport_drain_consume(cansub_transport *transport, size_t size);

cansub_result cansub_transport_eof(cansub_transport *transport);

/**
 * Whether the current transport is in the idle state.
 *
 * @param transport Transport.
 * @return true if no transport is requested, connected or closing, whether or not one was connected before.
 */
bool cansub_transport_is_idle(cansub_transport const *transport);

/**
 * Whether the transport is connected and not closing.
 *
 * @param transport Transport.
 * @return true if the transport is connected.
 */
bool cansub_transport_is_connected(cansub_transport const *transport);

/**
 * Whether the transport is closing, waiting to be drained and ended with EOF.
 *
 * @param transport Transport.
 * @return true if the transport is closing.
 */
bool cansub_transport_is_closing(cansub_transport const *transport);

/**
 * Whether the current transport will carry no more data, or EOF was signalled and no new transport is requested.
 * A transport that was never connected has not ended. See the @ref cansub_transport_is_idle() if the targeted
 * functionality is the connected property.
 *
 * @param transport Transport.
 * @return true if the current transport has ended.
 */
bool cansub_transport_has_ended(cansub_transport const *transport);

/**
 * Whether the current transport was opened: connected, and its TLS handshake has completed.
 *
 * A transport which ends before its handshake has completed counts as never opened. A requested transport is not
 * checked against TLS, as its context still holds the previous session until cansub_transport_connected().
 *
 * @param transport Transport, connected or closing.
 * @return true if the transport was opened.
 */
bool cansub_transport_is_established(cansub_transport *transport);

#endif