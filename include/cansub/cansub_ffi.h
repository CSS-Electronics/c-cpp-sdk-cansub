/* SPDX-License-Identifier: MIT */

#ifndef CANSUB_LIBRARY_CANSUB_FFI_H
#define CANSUB_LIBRARY_CANSUB_FFI_H

#include <stddef.h>
#include <stdint.h>

#ifndef CANSUB_API
    #if defined(_WIN32) && defined(CANSUB_BUILDING_SHARED)
        #define CANSUB_API __declspec(dllexport)
    #elif defined(_WIN32) && !defined(CANSUB_STATIC)
        #define CANSUB_API __declspec(dllimport)
    #elif defined(__GNUC__)
        #define CANSUB_API __attribute__((visibility("default")))
    #else
        #define CANSUB_API
    #endif
#endif

#ifdef __cplusplus
extern "C" {
#else
#include <stdalign.h>
#endif

#ifndef CANSUB_STATIC_ASSERT
#ifdef __cplusplus
#define CANSUB_STATIC_ASSERT(x, m)      static_assert((x), m)
#else
#define CANSUB_STATIC_ASSERT(x, m)      _Static_assert((x), m)
#endif
#endif

/**
 * @file cansub_ffi.h
 * @brief C API of the CANsub library.
 */

/** Library version as CalVer: The year in bits 31-16, the month in bits 15-8 and the day in bits 7-0.
 *
 * E.g. 0x07EA0B09 is 2026-11-09.
 */
#define CANSUB_VERSION 0x07EA0B09
#define CANSUB_VERSION_HEX_STR "07EA0B09"

/**
 * @defgroup cansub_api C API
 * @{
 */

/**
 * @page cansub_conventions API conventions
 *
 * - Glue: The caller's code that owns the sockets and timers, and moves data between them and the library.
 * - Pointers are borrowed for the duration of the call. The library copies anything it keeps.
 * - The library has no threads or timers. State only changes on API calls, e.g. sinking data or sending CAN frames,
 *   and the glue handles all timeouts.
 * - String inputs are NUL-terminated ASCII, unless the function takes their size, e.g.
 *   @ref cansub_api_version_check().
 * - Data from the remote is copied unvalidated. Response header values and response bodies may contain bytes above
 *   0x7F.
 * - Functions can be called in any state unless stated otherwise.
 * - All parameters must be valid, even those marked as ignored. An invalid ignored parameter gives
 *   @ref CANSUB_RESULT_INVALID_ARG.
 * - String outputs are NUL terminated and only copied if they fit into the buffer. Otherwise nothing is copied and the
 *   required size is returned. Call with buffer_size 0 to query the size. @ref cansub_error() is the exception.
 *   The response body is not a string output: @ref cansub_response_recv() truncates it and always consumes the
 *   response, so query its size with @ref cansub_response_peek().
 * - Contexts may only be created and used while the library is initialized, see @ref cansub_library_init().
 *   Hold your own successful @ref cansub_library_init() while calling @ref cansub_context_options_validate() or
 *   @ref cansub_context_create(), and until every context you created is destroyed. Do not rely on an initialization
 *   held elsewhere in the process, as it may be released at any time.
 *   Functions without a context can be called at any time, except @ref cansub_context_options_validate(), which
 *   requires the global resources.
 * - Threading: @ref cansub_library_init(), @ref cansub_library_deinit() and the functions without a context are
 *   thread-safe. All calls on one context, on either stream and including the functions taking a const context, must be
 *   serialized by the caller. A context may be used from different threads over time if the caller orders the calls,
 *   e.g. with a mutex. The library keeps no thread-local state and never calls back into the glue.
 * - Check the remote API version before any other request to the remote:
 *   Send GET /api/version as the first request with @ref cansub_request_send(), and pass the response body to
 *   @ref cansub_api_version_check(). Do not continue with an unsupported remote.
 *
 * @section cansub_conventions_building Building
 *
 * When building statically for Windows, build the library and all consumers with CANSUB_STATIC defined.
 * The shared library itself is built with CANSUB_BUILDING_SHARED defined, which the library's build system sets.
 * Consumers never define it.
 *
 * All functions use the platform's default C calling convention, e.g. cdecl on 32-bit Windows. Bindings that default
 * to another convention, e.g. C# P/Invoke on 32-bit Windows, must select cdecl explicitly.
 *
 * @section cansub_conventions_streams Streams
 *
 * A context uses up to two transports (TCP connections) at the same time, one per stream (@ref cansub_stream_code):
 * - @ref CANSUB_STREAM_DATA carries the WebSocket with the CAN frames.
 * - @ref CANSUB_STREAM_CONTROL carries REST requests from @ref cansub_request_send(), and the abort of
 *   @ref cansub_close_force().
 *
 * @section cansub_conventions_loop Driving a context
 *
 * The ready flags (@ref cansub_ready_code) tell the glue what to do next. They are level-triggered, and any call taking
 * a non-const @ref cansub_ctx may set or clear any flag.
 *
 * 1. Read the flags with @ref cansub_get_ready().
 * 2. Act on every set flag as its description says.
 * 3. Repeat until a pass makes no progress. Then wait for one of the following, and start over:
 *    - Readable for streams with SINK set.
 *    - Writable while the glue holds drained bytes not yet written.
 *    - A connect finishes.
 *    - A glue timer expires.
 *
 * Start one transport per TRANSPORT_OPEN. The flag stays set until answered, and is never withdrawn.
 * Answer it exactly once:
 * - With @ref cansub_data_connected() for normal operation.
 * - With @ref cansub_data_eof() if the transport could not be opened.
 * Every connected transport ends with @ref cansub_data_eof().
 *
 * When the remote closes a transport, keep calling @ref cansub_data_sink() and reading frames until every received byte
 * has been accepted and CAN_RX is clear, then call @ref cansub_data_eof(). Calling it earlier discards the unprocessed
 * data, which may hold the remote's close.
 * No call to @ref cansub_data_sink() with buffer_size 0 is needed, as every frame read continues processing.
 * On the control stream, unread responses need not be read first. The next request is only sent once the previous
 * response has been read, so nothing but the remote's close can be held back. Responses can still be read after
 * @ref cansub_data_eof().
 *
 * The control stream may request a new transport after its previous one ended. The data stream never will.
 * Once @ref CANSUB_STATE_CLOSED is reached, the context can be destroyed without aborting a transport.
 *
 * @section cansub_conventions_timeouts Timeouts
 *
 * The glue bounds every wait itself. To end a transport early, close the socket and call @ref cansub_data_eof()
 * without draining.
 * - Opening the data stream: From DATA_TRANSPORT_OPEN until @ref CANSUB_STATE_OPEN. This covers the connect, the TLS
 *   handshake and the HTTP upgrade.
 *   Ending the transport finishes the data stream and records @ref CANSUB_RESULT_CONNECTION_LOST.
 * - Opening the control stream: From CONTROL_TRANSPORT_OPEN until @ref cansub_request_in_flight() reports a request,
 *   which happens once the TLS handshake has completed.
 *   Ending the transport before then fails all queued requests, see @ref cansub_data_eof().
 *   A control transport opened while no requests are outstanding only carries the abort of @ref cansub_close_force(),
 *   which is never reported as in flight. Do not start this timer then. The closing timeout bounds the abort.
 *   A request is outstanding from a successful @ref cansub_request_send() until @ref cansub_response_peek() stops
 *   returning @ref CANSUB_RESULT_AGAIN for it, so the glue knows this from its own count of sent requests and received
 *   responses.
 * - Requests: From when @ref cansub_request_in_flight() reports a new tag until its response arrives.
 *   Ending the control transport fails that request with @ref CANSUB_RESULT_CONNECTION_LOST, see @ref cansub_data_eof()
 *   for the queued requests.
 * - Closing: If @ref CANSUB_STATE_CLOSED is not reached in time after @ref cansub_close(), call
 *   @ref cansub_close_force().
 *   If the abort does not complete in time either, end the control transport (close the socket and call
 *   @ref cansub_data_eof()), which fails the abort. Then call @ref cansub_close_force() again to start a new abort, or
 *   destroy the context with @ref cansub_context_destroy(). Calling @ref cansub_close_force() while the abort is still
 *   in progress returns @ref CANSUB_RESULT_AGAIN and starts nothing.
 *   Keep timing requests while closing, as the abort is only sent once no request is outstanding.
 */

/**
 * @defgroup cansub_datatypes Datatypes
 * @brief Custom datatypes used in the CANsub library.
 * @{
 */

/**
 * Opaque handle to the CANsub context.
 *
 * Separate contexts may be used from different threads. All calls on one context, on either stream and including the
 * functions taking a const context, must be serialized, see @ref cansub_conventions.
 *
 * With a NULL context, functions returning @ref cansub_result return @ref CANSUB_RESULT_INVALID_ARG, functions with
 * other return types return 0 and void functions do nothing.
 */
typedef struct cansub_ctx cansub_ctx;

/**
 * CAN frame representation.
 *
 * A frame to send is malformed if it violates any rule of @ref cansub_frame_fault_code.
 */
typedef struct cansub_frame {
    /** Timestamp in microseconds since the UNIX epoch. Ignored when sending. */
    uint64_t timestamp_us;
    /** Arbitration ID. Always 0 for error frames (@ref CANSUB_FRAME_FLAG_ERROR). */
    uint32_t arbitration_id;
    /** Data length in bytes (not the DLC). For RTR frames, this is the requested length. */
    uint8_t length;
    /** Data length code. For RTR frames, this is the requested DLC. Non-FD frames support DLC 9-15 when received.
     * When transmitting, the remote truncates DLC 9-15 of a non-FD frame to DLC 8, on the bus and in the transmit
     * acknowledgement. */
    uint8_t dlc;
    /** Flags, see @ref cansub_frame_flags. */
    uint8_t flags;
    /** Reserved. Must be 0 when sending. Always 0 when received. */
    uint8_t reserved[1];
    /** Data. Bytes beyond length, and all bytes of RTR frames, are 0 when received and ignored when sending. */
    uint8_t data[64];
} cansub_frame;

/* Size and alignment rules. */
CANSUB_STATIC_ASSERT(sizeof(cansub_frame) == 80, "cansub_frame must be 80 bytes");
CANSUB_STATIC_ASSERT(alignof(cansub_frame) == alignof(uint64_t), "cansub_frame must be uint64_t aligned");

/** Flag values for flags in @ref cansub_frame. */
typedef enum cansub_frame_flags
{
    /** Remote request frame. */
    CANSUB_FRAME_FLAG_RTR = 0x01,
    /** Extended arbitration ID. */
    CANSUB_FRAME_FLAG_IDE = 0x02,
    /** Error frame. The arbitration ID is always 0x000, and the single data byte holds the error type, see
     * @ref cansub_error_frame_type. */
    CANSUB_FRAME_FLAG_ERROR = 0x04,
    /** CAN FD frame. */
    CANSUB_FRAME_FLAG_FDF = 0x08,
    /** Bit rate switch. */
    CANSUB_FRAME_FLAG_BRS = 0x10,
    /** Error state indicator. */
    CANSUB_FRAME_FLAG_ESI = 0x20,
    /** Transmit acknowledgement: The remote transmitted this frame on the bus, either a frame this client sent or one
     * sent autonomously by the remote (requires remote configuration). Only set on received frames.
     * There is no NACK. Detect lost frames by timing out on the expected acknowledgement. */
    CANSUB_FRAME_FLAG_TX = 0x40,
    /** Reserved. Must be 0 when sending. Always 0 when received. */
    CANSUB_FRAME_FLAG_RESERVED = 0x80,
} cansub_frame_flags;

/**
 * Error type in the first data byte of an error frame (@ref CANSUB_FRAME_FLAG_ERROR). Error frames have a single data
 * byte, so their length and DLC are both 1.
 */
typedef enum cansub_error_frame_type
{
    /** Unmapped error type. */
    CANSUB_ERROR_FRAME_TYPE_OTHER_ERROR = 0xFF,
    /** No error. */
    CANSUB_ERROR_FRAME_TYPE_NO_ERROR = 0,
    /** A transmitted bit was read back with the opposite level. */
    CANSUB_ERROR_FRAME_TYPE_BIT_ERROR = 1,
    /** A frame was not acknowledged by any node. */
    CANSUB_ERROR_FRAME_TYPE_ACK_ERROR = 2,
    /** Invalid fixed-form field. */
    CANSUB_ERROR_FRAME_TYPE_FORM_ERROR = 3,
    /** Invalid bit stuffing. */
    CANSUB_ERROR_FRAME_TYPE_STUFF_ERROR = 4,
    /** Invalid CRC. */
    CANSUB_ERROR_FRAME_TYPE_CRC_ERROR = 5,
} cansub_error_frame_type;

/**
 * Bit set of @ref cansub_frame_fault_code values, see @ref cansub_frame_validate().
 */
typedef uint32_t cansub_frame_faults;

/**
 * Fault codes of a malformed CAN frame. Each code is a bit, and a frame reports every rule it violates.
 */
typedef enum cansub_frame_fault_code
{
    /** @ref CANSUB_FRAME_FLAG_RESERVED is set, or the reserved byte is not 0. */
    CANSUB_FRAME_FAULT_RESERVED = 0x01,
    /** @ref CANSUB_FRAME_FLAG_ERROR or @ref CANSUB_FRAME_FLAG_TX is set. These flags only occur on received frames. */
    CANSUB_FRAME_FAULT_RX_ONLY = 0x02,
    /** @ref CANSUB_FRAME_FLAG_BRS or @ref CANSUB_FRAME_FLAG_ESI is set without @ref CANSUB_FRAME_FLAG_FDF. */
    CANSUB_FRAME_FAULT_FD_ONLY = 0x04,
    /** @ref CANSUB_FRAME_FLAG_RTR is set together with @ref CANSUB_FRAME_FLAG_FDF. */
    CANSUB_FRAME_FAULT_RTR_FD = 0x08,
    /** The arbitration ID exceeds 11 bits (29 bits with @ref CANSUB_FRAME_FLAG_IDE). */
    CANSUB_FRAME_FAULT_ID = 0x10,
    /** The DLC is outside the valid range (0-15). */
    CANSUB_FRAME_FAULT_DLC = 0x20,
    /** The length is not exactly the number of bytes the DLC represents, see @ref cansub_frame_dlc_to_nob().
     * Only checked if the DLC is in the valid range. */
    CANSUB_FRAME_FAULT_LENGTH = 0x40,
} cansub_frame_fault_code;

/**
 * Result of a CANsub library call.
 * 0 is success, positive values are counts and negative values are the statuses and errors of @ref cansub_result_code.
 */
typedef int32_t cansub_result;

/**
 * Result codes.
 *
 * Every function returning @ref cansub_result reports whether the call itself did its job:
 * - 0 or a positive count - done.
 * - @ref CANSUB_RESULT_NOT_FOUND - done, but the requested item does not exist.
 * - @ref CANSUB_RESULT_NOT_SUPPORTED - done, but this library does not support the checked item.
 * - @ref CANSUB_RESULT_AGAIN - not now. Call again later. For calls with a matching ready flag, call once that flag is
 *   set.
 * - @ref CANSUB_RESULT_CLOSED - finished. This call makes no further progress on this context (for the transport
 *   functions, on the current transport).
 * - @ref CANSUB_RESULT_INVALID_STATE - a bug in the caller. A correct glue never sees it.
 * - @ref CANSUB_RESULT_INVALID_ARG - invalid data passed to the library.
 * - @ref CANSUB_RESULT_MEMORY - a resource failure which can be retried.
 * - @ref CANSUB_RESULT_ERROR - an internal library error. For a call on a context, the context is unusable: Destroy and
 *   re-create it.
 *
 * Any function returning @ref cansub_result may return @ref CANSUB_RESULT_INVALID_ARG or @ref CANSUB_RESULT_ERROR.
 *
 * The CONNECTION_* codes describe how the data transport or a request ended. @ref cansub_error() returns them for the
 * data stream. @ref cansub_response_recv() and @ref cansub_response_peek() return them for a failed request: The call
 * itself succeeded, the tag is set and @ref cansub_response_recv() consumed the response.
 */
typedef enum cansub_result_code {
    /** Success. */
    CANSUB_RESULT_OK = 0,
    /** Not ready, call again later. */
    CANSUB_RESULT_AGAIN = -1,
    /** Memory allocation failure. */
    CANSUB_RESULT_MEMORY = -2,
    /** Invalid argument, e.g. a NULL buffer with a non-zero size. */
    CANSUB_RESULT_INVALID_ARG = -3,
    /** Context or transport is closed. No more data passes through this call. */
    CANSUB_RESULT_CLOSED = -4,
    /** Invalid state for this call. */
    CANSUB_RESULT_INVALID_STATE = -5,
    /** Connection was rejected. */
    CANSUB_RESULT_CONNECTION_REJECTED = -6,
    /** TLS error. */
    CANSUB_RESULT_CONNECTION_TLS = -7,
    /** Unrecoverable protocol violation. */
    CANSUB_RESULT_CONNECTION_PROTOCOL = -8,
    /** Connection was lost. */
    CANSUB_RESULT_CONNECTION_LOST = -9,
    /** Requested item does not exist, e.g. a response header. */
    CANSUB_RESULT_NOT_FOUND = -10,
    /** This library does not support the checked item, e.g. the remote API version. */
    CANSUB_RESULT_NOT_SUPPORTED = -11,
    /** Internal library error. Fatal for the context, see the result code overview. Corresponds to INT32_MIN. Written
     * as -2147483647 - 1, as the literal 2147483648 does not fit an int, and negating it as an unsigned long draws
     * warning C4146 from MSVC. */
    CANSUB_RESULT_ERROR = (-2147483647 - 1),
} cansub_result_code;

/**
 * Holds a @ref cansub_state_code.
 */
typedef uint32_t cansub_state;

/**
 * States of a CANsub context.
 */
typedef enum cansub_state_code {
    /** Not a valid state. Returned for a NULL context. */
    CANSUB_STATE_INVALID = 0,
    /** Context is initialized and ready to start opening. Control requests can already be sent. */
    CANSUB_STATE_INITIALIZED = 1,
    /** Context is opening. Configure the remote before entering this state. */
    CANSUB_STATE_OPENING = 2,
    /** Context is open, CAN frames can flow. */
    CANSUB_STATE_OPEN = 3,
    /** Entered by @ref cansub_close() or @ref cansub_close_force(), a fatal error that ends the data transport (see
     * @ref cansub_error()), a remote close of the data transport or a data EOF while the control transport is up.
     * Left only via @ref cansub_data_eof(). */
    CANSUB_STATE_CLOSING = 4,
    /** Terminal state, reached only via @ref cansub_data_eof(). Both streams have ended and no transport is requested.
     * The context can be destroyed. Remaining received frames and responses can still be read. */
    CANSUB_STATE_CLOSED = 5,
} cansub_state_code;

/**
 * Bit set of @ref cansub_ready_code values.
 */
typedef uint32_t cansub_ready;

/**
 * Ready flags of a @ref cansub_ctx. See @ref cansub_conventions_loop for how to drive the context.
 *
 * Each stream has the same four transport flags (SINK, DRAIN, TRANSPORT_CLOSE, TRANSPORT_OPEN) in its own byte:
 * the data stream in bits 0-7 and the control stream in bits 8-15. Payload flags start at bit 24.
 */
typedef enum cansub_ready_code {
    /** @ref cansub_data_sink() accepts at least 1 byte for the data stream. */
    CANSUB_READY_DATA_SINK = 0x00000001,
    /** Call @ref cansub_data_drain() (or @ref cansub_data_drain_peek() and @ref cansub_data_drain_consume()) and write
     * the bytes to the data stream transport. */
    CANSUB_READY_DATA_DRAIN = 0x00000002,
    /** Drain until @ref cansub_data_drain() (or @ref cansub_data_drain_peek()) returns @ref CANSUB_RESULT_CLOSED,
     * close the data stream transport, then call @ref cansub_data_eof(). For a transport that was never opened,
     * only call @ref cansub_data_eof(). Set whenever the library is done with the transport. */
    CANSUB_READY_DATA_TRANSPORT_CLOSE = 0x00000004,
    /** Open a transport for the data stream, then call @ref cansub_data_connected(), or @ref cansub_data_eof() if it
     * could not be opened. Set by @ref cansub_open(). Stays set until answered. */
    CANSUB_READY_DATA_TRANSPORT_OPEN = 0x00000008,

    /** @ref cansub_data_sink() accepts at least 1 byte for the control stream. */
    CANSUB_READY_CONTROL_SINK = 0x00000100,
    /** Call @ref cansub_data_drain() (or @ref cansub_data_drain_peek() and @ref cansub_data_drain_consume()) and write
     * the bytes to the control stream transport. */
    CANSUB_READY_CONTROL_DRAIN = 0x00000200,
    /** Drain until @ref cansub_data_drain() (or @ref cansub_data_drain_peek()) returns @ref CANSUB_RESULT_CLOSED,
     * close the control stream transport, then call @ref cansub_data_eof(). For a transport that was never opened,
     * only call @ref cansub_data_eof(). Set whenever the library is done with the transport. */
    CANSUB_READY_CONTROL_TRANSPORT_CLOSE = 0x00000400,
    /** Open a transport for the control stream, then call @ref cansub_data_connected(), or @ref cansub_data_eof() if
     * it could not be opened. Set when control requests are queued or an abort of @ref cansub_close_force() waits,
     * and no control transport is connected. Stays set until answered. */
    CANSUB_READY_CONTROL_TRANSPORT_OPEN = 0x00000800,

    /** Call @ref cansub_frame_recv() until it returns @ref CANSUB_RESULT_AGAIN or @ref CANSUB_RESULT_CLOSED.
     * Received data waits while frames are unread. */
    CANSUB_READY_CAN_RX = 0x01000000,
    /** @ref cansub_frame_send() accepts at least one CAN frame of any size. */
    CANSUB_READY_CAN_TX = 0x02000000,
    /** Call @ref cansub_response_recv() to read the oldest response. */
    CANSUB_READY_CONTROL_RESPONSE = 0x04000000,
    /** @ref cansub_request_send() accepts a request. */
    CANSUB_READY_CONTROL_REQUEST = 0x08000000,
} cansub_ready_code;

/**
 * Holds a @ref cansub_stream_code.
 */
typedef uint32_t cansub_stream;

/**
 * CANsub stream type codes.
 *
 * A context interleaves two streams, each with its own transport. Calls on both streams are serialized like all calls
 * on the context, see @ref cansub_ctx. See @ref cansub_conventions_streams.
 */
typedef enum cansub_stream_code {
    /** WebSocket with the CAN frames. */
    CANSUB_STREAM_DATA = 0,
    /** REST requests. */
    CANSUB_STREAM_CONTROL = 1,
} cansub_stream_code;

/**
 * CANsub context options.
 *
 * New fields are only appended, so the size only grows.
 */
typedef struct cansub_context_options {
    /** Size of this struct as known by the caller: sizeof(cansub_context_options). */
    size_t struct_size;
    /** Host the glue connects to: A hostname or an IPv4 address. Always required. At most 255 characters, each a
     * letter, a digit, '-', '.' or '_'.
     * If neither device_id nor accept_any_device is set, it must match the device certificate,
     * see @ref cansub_context_create(). */
    char const *host;
    /** Device ID (8 hex digits, case-insensitive). If set, the device certificate is verified against it instead of
     * the host. Needed when the host is not in the device certificate, e.g. an IPv4 address or a custom DNS name. */
    char const *device_id;
    /** CAN channel on the device, 1-based. */
    uint32_t channel;
    /** Non-zero to verify only the root certificate. Ignored if device_id is set. Prefer device_id whenever the
     * device ID is known, e.g. for an IPv4 address or a custom DNS name as host. */
    uint32_t accept_any_device;
    /** Client certificate for mutual TLS, in DER or PEM format. */
    void const *client_certificate;
    /** Size of client_certificate. For PEM, include the NUL terminator. */
    size_t client_certificate_size;
    /** Unencrypted client key for mutual TLS, in DER or PEM format. */
    void const *client_key;
    /** Size of client_key. For PEM, include the NUL terminator. */
    size_t client_key_size;
} cansub_context_options;

/**
 * Bit set of @ref cansub_options_fault_code values, see @ref cansub_context_options_validate().
 */
typedef uint32_t cansub_options_faults;

/**
 * Fault codes of invalid context options. Each code is a bit, and the options report every rule they violate.
 */
typedef enum cansub_options_fault_code
{
    /** struct_size does not match the size used by the library. No other field is checked. */
    CANSUB_OPTIONS_FAULT_STRUCT_SIZE = 0x00000001,
    /** host is NULL. */
    CANSUB_OPTIONS_FAULT_HOST_MISSING = 0x00000002,
    /** device_id is not 8 hex digits. */
    CANSUB_OPTIONS_FAULT_DEVICE_ID = 0x00000004,
    /** host is empty, longer than 255 characters or contains a character other than a letter, a digit, '-', '.' or
     * '_'. */
    CANSUB_OPTIONS_FAULT_HOST = 0x00000008,
    /** channel is 0. */
    CANSUB_OPTIONS_FAULT_CHANNEL = 0x00000010,
    /** Only one of client_certificate and client_key is non-NULL, or a NULL buffer has a non-zero size. */
    CANSUB_OPTIONS_FAULT_MTLS_PAIR = 0x00000020,
    /** client_certificate cannot be parsed, e.g. PEM without the NUL terminator.
     * Only checked if the mTLS pair is valid. Running out of memory while parsing can also report this fault, so retry
     * before treating the certificate as invalid. */
    CANSUB_OPTIONS_FAULT_MTLS_CERTIFICATE = 0x00000040,
    /** client_key cannot be parsed, e.g. PEM without the NUL terminator. Only checked if the mTLS pair is valid.
     * Running out of memory while parsing can also report this fault, so retry before treating the key as invalid. */
    CANSUB_OPTIONS_FAULT_MTLS_KEY = 0x00000080,
    /** client_key does not belong to client_certificate. Only checked if both can be parsed. */
    CANSUB_OPTIONS_FAULT_MTLS_MISMATCH = 0x00000100,
} cansub_options_fault_code;

/**
 * Bit timing of one CAN phase, in time quanta of the prescaled clock.
 */
typedef struct cansub_bittiming_segment
{
    /** Clock prescaler. */
    uint32_t brp;
    /** Time quanta before the sample point, excluding the sync segment. A bit is 1 + seg1 + seg2 time quanta. */
    uint32_t seg1;
    /** Time quanta after the sample point. */
    uint32_t seg2;
    /** Synchronization jump width in time quanta. */
    uint32_t sjw;
} cansub_bittiming_segment;

/**
 * Nominal and data phase bit timing.
 */
typedef struct cansub_bittiming
{
    /** Nominal phase. */
    cansub_bittiming_segment nominal;
    /** Data phase. Always required, as the device always operates in FD mode.
     * Only used by frames with @ref CANSUB_FRAME_FLAG_BRS. */
    cansub_bittiming_segment data;
} cansub_bittiming;

/** @} */

/**
 * @defgroup cansub_library Library information
 * @brief Library-wide functions.
 * @{
 */

/**
 * Get the version of the CANsub library.
 *
 * No ABI compatibility is guaranteed across versions. See the library's changelog for compatibility notes.
 *
 * @return Library version, see @ref CANSUB_VERSION.
 */
CANSUB_API uint32_t cansub_version(void);

/**
 * Initialize the global resources of the CANsub library.
 *
 * Must succeed before a context can be created or options validated, see @ref cansub_conventions.
 * Balance every successful call with @ref cansub_library_deinit(). Do not balance a failed call.
 *
 * Thread-safe and reference counted: It can be called concurrently with any library function, including
 * @ref cansub_library_deinit(). The first successful call sets up the global resources. After the last balancing
 * deinit, the library can be initialized again.
 *
 * @return Result.
 * @retval CANSUB_RESULT_OK Library initialized, or already initialized.
 * @retval CANSUB_RESULT_MEMORY Allocation failure.
 * @retval CANSUB_RESULT_ERROR Initialization failed, e.g. no entropy source is available.
 */
CANSUB_API cansub_result cansub_library_init(void);

/**
 * Release the global resources of the CANsub library.
 *
 * Balances one successful call to @ref cansub_library_init(). Only the call balancing the last initialization releases
 * the global resources, and all contexts must have been destroyed before it. Calls while the library is not
 * initialized are ignored.
 *
 * Thread-safe like @ref cansub_library_init().
 */
CANSUB_API void cansub_library_deinit(void);

/**
 * Check whether this library supports a remote API version.
 *
 * The version has the format MAJOR.MINOR with two digits each. Two forms are accepted:
 * - Quoted, 7 bytes: "05.00" including the double quotes, as returned in the body of GET /api/version.
 * - Unquoted, 5 bytes: 05.00, e.g. from the `api` TXT record of the device discovery.
 *
 * version_size must cover exactly the version. Surrounding whitespace, line endings or other data are rejected with
 * @ref CANSUB_RESULT_INVALID_ARG. NUL termination is not required.
 *
 * Supported means both MAJOR and MINOR match a version known to this library.
 * See @ref cansub_conventions for when to call it.
 *
 * @param[in] version Version, e.g. the body from @ref cansub_response_recv().
 * @param version_size Size of the version: 7 for the quoted form, 5 for the unquoted form.
 * @param[out] remote Parsed version, MAJOR in the upper 16 bits and MINOR in the lower 16 bits. Ignored if NULL.
 *                    Written whenever the version can be parsed.
 * @return Result.
 * @retval CANSUB_RESULT_OK The version is supported.
 * @retval CANSUB_RESULT_NOT_SUPPORTED The version was parsed, but this library does not support it.
 * @retval CANSUB_RESULT_INVALID_ARG The version is in neither form, or version is NULL.
 */
CANSUB_API cansub_result cansub_api_version_check(char const *version, size_t version_size, uint32_t *remote);

/** @} */

/**
 * @defgroup cansub_lifecycle Context lifecycle
 * @brief Creating, opening, closing and destroying a context.
 * @{
 */

/**
 * Check context options without creating a context.
 *
 * Applies the same rules as @ref cansub_context_create(), including parsing the mTLS certificate and key.
 * Every violated rule is reported, except:
 * - A struct_size mismatch is reported alone, as no other field can be read safely.
 * - The certificate and key are only parsed if they form a valid pair, and only checked against each other if both
 *   can be parsed.
 *
 * @param[in] options Options to validate.
 * @param[out] faults Every rule the options violate, see @ref cansub_options_fault_code. Set to 0 for valid options.
 *                    Ignored if NULL. Only written for @ref CANSUB_RESULT_OK, and for @ref CANSUB_RESULT_INVALID_ARG
 *                    with options specified.
 * @note Allocates temporary memory.
 * @return Result.
 * @retval CANSUB_RESULT_OK Options are valid.
 * @retval CANSUB_RESULT_MEMORY Allocation failure while parsing the mTLS certificate or key.
 * @retval CANSUB_RESULT_INVALID_ARG Options are invalid, or options is NULL.
 * @retval CANSUB_RESULT_INVALID_STATE The library is not initialized, see @ref cansub_library_init().
 */
CANSUB_API cansub_result cansub_context_options_validate(cansub_context_options const *options, cansub_options_faults *faults);

/**
 * Create a CANsub context to connect to a remote host.
 *
 * Zero-initialize the options struct, then set `struct_size` to `sizeof(cansub_context_options)`.
 *
 * The host is always required, either a hostname or an IPv4 address. device_id and accept_any_device determine what
 * is verified:
 *
 * | device_id | accept_any_device | Verified                                 |
 * |-----------|-------------------|------------------------------------------|
 * | Set       | Ignored           | Device ID                                |
 * | NULL      | Set (Not 0)       | Only root certificate                    |
 * | NULL      | Cleared (0)       | Host against the certificate's hostnames |
 *
 * The device certificate only contains the hostnames "<device ID>-usb.local" and "<device ID>-eth.local".
 * Any other host, e.g. an IPv4 address or a custom DNS name, passes context creation, but the TLS handshake fails with
 * @ref CANSUB_RESULT_CONNECTION_TLS unless device_id or accept_any_device is set.
 *
 * Set both client_certificate and client_key for mTLS, or neither. Setting only one fails with
 * @ref CANSUB_RESULT_INVALID_ARG. The certificate is only presented if the remote requests it.
 *
 * @param[out] ctx Allocated context handle. Set to NULL on error.
 * @param[in] options Options for the context.
 * @note The library owns no sockets. The glue resolves the host and connects its sockets to it.
 * @return Result.
 * @retval CANSUB_RESULT_OK Context created.
 * @retval CANSUB_RESULT_MEMORY Allocation failure.
 * @retval CANSUB_RESULT_INVALID_ARG An options field is invalid or missing, or struct_size does not match.
 *         @ref cansub_context_options_validate() reports which rules the options violate.
 * @retval CANSUB_RESULT_INVALID_STATE The library is not initialized, see @ref cansub_library_init().
 */
CANSUB_API cansub_result cansub_context_create(cansub_ctx **ctx, cansub_context_options const *options);

/**
 * Destroy a CANsub context and release its memory.
 *
 * Any connected or requested transport is abandoned. No more data is drained or sunk, and the glue must close its
 * sockets immediately.
 *
 * @param ctx Context to destroy.
 */
CANSUB_API void cansub_context_destroy(cansub_ctx *ctx);

/**
 * Start opening the CANsub context.
 *
 * Non-blocking: Requests the data stream transport (@ref CANSUB_READY_DATA_TRANSPORT_OPEN). Once the glue has
 * connected it and called @ref cansub_data_connected(), transfer data with @ref cansub_data_sink() and
 * @ref cansub_data_drain().
 *
 * Does nothing on an opening or open context. A closing or closed context cannot be reopened.
 *
 * Check the remote API version before calling this, see @ref cansub_conventions.
 *
 * @param ctx CANsub context.
 * @return Result.
 * @retval CANSUB_RESULT_OK Opening started, or nothing changed.
 * @retval CANSUB_RESULT_CLOSED The context is closing or closed.
 * @retval CANSUB_RESULT_MEMORY Allocation failure. Nothing changed, the call can be repeated.
 */
CANSUB_API cansub_result cansub_open(cansub_ctx *ctx);

/**
 * Start closing the CANsub context.
 *
 * Non-blocking: Transfer data with @ref cansub_data_sink() and @ref cansub_data_drain() until the close completes.
 * Does nothing on a closing or closed context.
 *
 * Data stream:
 * - From @ref CANSUB_STATE_OPEN, starts the closing handshake. All pending frames are sent first.
 * - From @ref CANSUB_STATE_INITIALIZED or @ref CANSUB_STATE_OPENING, pending data for drain and sink is discarded.
 *   TRANSPORT_CLOSE is set for the data stream immediately or by @ref cansub_data_connected() while the transport is
 *   still being opened.
 *
 * Control stream:
 * - No new requests are accepted. Queued requests are still completed.
 * - The control transport is shut down once the data stream has finished, no requests are outstanding and no written
 *   abort of @ref cansub_close_force() awaits its response.
 * - A request awaiting its response delays @ref CANSUB_STATE_CLOSED. To bound the close, see
 *   @ref cansub_conventions_timeouts.
 *
 * @param ctx CANsub context.
 * @return Result.
 * @retval CANSUB_RESULT_OK Closing started, or nothing changed.
 * @retval CANSUB_RESULT_MEMORY Allocation failure. Nothing changed, the call can be repeated.
 */
CANSUB_API cansub_result cansub_close(cansub_ctx *ctx);

/**
 * Abort the queued CAN transmissions on the remote to complete a pending close.
 *
 * The remote only confirms a close once all its queued CAN frames are transmitted on the bus, without a timeout.
 * Call this when the confirmation has not arrived in time after @ref cansub_close(), see
 * @ref cansub_conventions_timeouts. The remote then discards its queued CAN frames without transmitting them and
 * confirms the close on the data stream.
 *
 * The abort is sent on the control stream, so a control transport may be requested
 * (@ref CANSUB_READY_CONTROL_TRANSPORT_OPEN). It bypasses the request queue, returns no response through
 * @ref cansub_response_recv() and is never reported by @ref cansub_request_in_flight(). Outstanding control requests
 * are answered before the abort is sent.
 *
 * - From @ref CANSUB_STATE_INITIALIZED or @ref CANSUB_STATE_OPENING, acts as @ref cansub_close(). The remote has no
 *   queued CAN frames yet.
 * - From @ref CANSUB_STATE_OPEN, acts as @ref cansub_close() and starts the abort at once, without waiting for a
 *   close timeout.
 * - From @ref CANSUB_STATE_CLOSING, returns @ref CANSUB_RESULT_AGAIN if an abort is in progress, i.e. queued, being
 *   written or awaiting its response. Otherwise, starts the abort and returns @ref CANSUB_RESULT_OK if the remote has
 *   yet to confirm the close, and returns @ref CANSUB_RESULT_OK without starting anything if not, e.g. once the data
 *   stream has finished. An abort not yet written when the data stream finishes is dropped.
 * - From @ref CANSUB_STATE_CLOSED, does nothing.
 *
 * If the control transport ends before the abort is written, the abort stays in progress and waits for the next
 * control transport. The abort fails if that transport cannot be opened, if the control transport ends after the abort
 * was written but before it was answered, if its response is invalid or on a TLS error. A failed abort leaves the
 * context in @ref CANSUB_STATE_CLOSING with no abort in progress, so calling this again starts a new one.
 *
 * @param ctx CANsub context.
 * @return Result.
 * @retval CANSUB_RESULT_OK A close or abort was started, or there is nothing left to abort.
 * @retval CANSUB_RESULT_AGAIN An abort is already in progress, nothing new was started.
 * @retval CANSUB_RESULT_MEMORY Allocation failure while starting the close. Nothing changed, the call can be repeated.
 */
CANSUB_API cansub_result cansub_close_force(cansub_ctx *ctx);

/** @} */

/**
 * @defgroup cansub_data Transport data
 * @brief Connecting, draining and sinking raw data from the transports.
 *
 * The drain, consume and sink functions process at most INT32_MAX bytes per call. Larger sizes are clamped.
 * @{
 */

/**
 * Notify the library that the transport of a stream has been opened.
 *
 * Only valid while TRANSPORT_OPEN is set for the stream. Starts the TLS handshake, so data is available to drain
 * afterwards. If the transport could not be opened, call @ref cansub_data_eof() instead. If the library no longer
 * needs the transport (e.g. @ref cansub_close() was called while connecting), this sets TRANSPORT_CLOSE instead of
 * starting the handshake, and still succeeds.
 *
 * @param ctx CANsub context.
 * @param stream Stream type, see @ref cansub_stream_code.
 * @return Result.
 * @retval CANSUB_RESULT_OK Transport accepted.
 * @retval CANSUB_RESULT_MEMORY Allocation failure. TRANSPORT_OPEN stays set. Call this again for the same transport, or
 *         close it and call @ref cansub_data_eof().
 * @retval CANSUB_RESULT_INVALID_STATE No transport is requested for the stream.
 * @retval CANSUB_RESULT_INVALID_ARG ctx is NULL or the stream is unknown.
 */
CANSUB_API cansub_result cansub_data_connected(cansub_ctx *ctx, cansub_stream stream);

/**
 * Drain the pending bytes of a stream, to be written to its transport.
 *
 * Drained bytes are removed. Bytes the glue cannot write now must be written later, in order. Otherwise use
 * @ref cansub_data_drain_peek() and @ref cansub_data_drain_consume().
 *
 * @param ctx CANsub context.
 * @param stream Stream type, see @ref cansub_stream_code.
 * @param[out] buffer Buffer for the drained bytes. Can be NULL if buffer_size is 0.
 * @param buffer_size Size of the buffer.
 * @return Result. Positive values are the number of bytes written to the buffer. 0 if buffer_size is 0 and data is
 *         pending.
 * @retval CANSUB_RESULT_AGAIN No data is pending.
 * @retval CANSUB_RESULT_CLOSED The current transport carries no more data (TRANSPORT_CLOSE is set or EOF was
 *         signaled) and no bytes are pending.
 * @retval CANSUB_RESULT_INVALID_ARG ctx is NULL or the stream is unknown.
 */
CANSUB_API cansub_result cansub_data_drain(cansub_ctx *ctx, cansub_stream stream, void *buffer, size_t buffer_size);

/**
 * Copy the pending bytes of a stream without removing them.
 *
 * Use with @ref cansub_data_drain_consume() when the glue does not keep bytes it could not write, e.g. with
 * asynchronous writes: Peek, write the bytes to the transport, then consume the number of bytes written.
 * Until consumed, the next peek returns the same bytes. While the transport is closing, this continues a pending TLS
 * close_notify, as @ref cansub_data_drain() does. New data is only appended, so peeked bytes never change.
 *
 * Peeked bytes belong to the current transport. They are discarded with its pending data (see @ref cansub_close() and
 * @ref cansub_data_eof()) and must not be consumed afterwards.
 *
 * @param ctx CANsub context.
 * @param stream Stream type, see @ref cansub_stream_code.
 * @param[out] buffer Buffer for the pending bytes. Can be NULL if buffer_size is 0.
 * @param buffer_size Size of the buffer.
 * @return Result. Positive values are the number of bytes copied to the buffer. 0 if buffer_size is 0 and data is
 *         pending.
 * @retval CANSUB_RESULT_AGAIN No data is pending.
 * @retval CANSUB_RESULT_CLOSED The current transport carries no more data (TRANSPORT_CLOSE is set or EOF was
 *         signaled) and no bytes are pending.
 * @retval CANSUB_RESULT_INVALID_ARG ctx is NULL or the stream is unknown.
 */
CANSUB_API cansub_result cansub_data_drain_peek(cansub_ctx *ctx, cansub_stream stream, void *buffer, size_t buffer_size);

/**
 * Remove bytes returned by @ref cansub_data_drain_peek() once they are written to the transport.
 *
 * size must not exceed the number of bytes returned by the last @ref cansub_data_drain_peek(). The library cannot
 * detect a larger size: Bytes appended after the peek are then removed without ever being written, which corrupts the
 * transport.
 * Like @ref cansub_data_drain(), this may move more data into the internal buffer and change any flag.
 *
 * @param ctx CANsub context.
 * @param stream Stream type, see @ref cansub_stream_code.
 * @param size Number of bytes written to the transport.
 * @return Result. Positive values are the number of bytes removed. 0 if size is 0 and data is pending.
 * @retval CANSUB_RESULT_AGAIN No data is pending.
 * @retval CANSUB_RESULT_CLOSED The current transport carries no more data (TRANSPORT_CLOSE is set or EOF was
 *         signaled) and no bytes are pending.
 * @retval CANSUB_RESULT_INVALID_ARG ctx is NULL or the stream is unknown.
 */
CANSUB_API cansub_result cansub_data_drain_consume(cansub_ctx *ctx, cansub_stream stream, size_t size);

/**
 * Pass data received from the transport of a stream to the library.
 *
 * Only valid between @ref cansub_data_connected() and @ref cansub_data_eof() for the stream.
 * Once TRANSPORT_CLOSE is set for the stream, data is discarded and @ref CANSUB_RESULT_CLOSED is returned.
 *
 * Calling with buffer_size 0 continues processing the data already received for the stream.
 * This is not required, as data held back while frames were not read is processed as they are read.
 *
 * @param ctx CANsub context.
 * @param stream Stream type, see @ref cansub_stream_code.
 * @param[in] buffer Received data. Can be NULL if buffer_size is 0.
 * @param buffer_size Size of the buffer.
 * @return Result. Positive values are the number of bytes accepted.
 * @retval CANSUB_RESULT_OK buffer_size was 0, the data already received was processed.
 * @retval CANSUB_RESULT_AGAIN No space, nothing was accepted.
 * @retval CANSUB_RESULT_CLOSED TRANSPORT_CLOSE is set for the stream, the data was discarded.
 * @retval CANSUB_RESULT_INVALID_STATE The transport is not connected and TRANSPORT_CLOSE is not set.
 * @retval CANSUB_RESULT_INVALID_ARG ctx is NULL or the stream is unknown.
 */
CANSUB_API cansub_result cansub_data_sink(cansub_ctx *ctx, cansub_stream stream, void const *buffer, size_t buffer_size);

/**
 * Get the number of bytes @ref cansub_data_sink() currently accepts for a stream.
 *
 * Only valid between @ref cansub_data_connected() and @ref cansub_data_eof() for the stream.
 * Optional, e.g. to limit how much the glue reads from the transport. Changes no state or flag.
 * While the transport is connected, the space is non-zero exactly when SINK is set for the stream.
 *
 * @param ctx CANsub context.
 * @param stream Stream type, see @ref cansub_stream_code.
 * @return Result. Positive values are the number of bytes @ref cansub_data_sink() accepts, at most INT32_MAX.
 * @retval CANSUB_RESULT_OK No space, @ref cansub_data_sink() currently returns @ref CANSUB_RESULT_AGAIN.
 * @retval CANSUB_RESULT_CLOSED TRANSPORT_CLOSE is set for the stream, @ref cansub_data_sink() discards all data.
 * @retval CANSUB_RESULT_INVALID_STATE The transport is not connected and TRANSPORT_CLOSE is not set.
 * @retval CANSUB_RESULT_INVALID_ARG ctx is NULL or the stream is unknown.
 */
CANSUB_API cansub_result cansub_data_sink_space(cansub_ctx const *ctx, cansub_stream stream);

/**
 * Signal the transport EOF of a stream.
 *
 * Valid while the stream has a transport connected or requested, or TRANSPORT_CLOSE set.
 * Data sunk but not yet processed is discarded. When the remote closed the transport, process the received data first,
 * see @ref cansub_conventions_loop. Responses can still be read after this call.
 *
 * Data stream:
 * - The data stream is finished.
 * - If the close has not completed, @ref CANSUB_RESULT_CONNECTION_LOST is recorded.
 * - Received frames can still be read with @ref cansub_frame_recv().
 *
 * Control stream:
 * - The request in progress fails with @ref CANSUB_RESULT_CONNECTION_LOST, returned by @ref cansub_response_recv().
 *   A response without Content-Length or chunked encoding ends with the transport, so it is completed instead.
 * - If the transport could not be opened, or the data stream has finished, all queued requests fail the same way.
 * - Otherwise, a new transport is requested if requests are still queued.
 *
 * A transport that ends before its TLS handshake has completed counts as not opened, e.g. when the remote closes it to
 * reject the client certificate.
 *
 * @param ctx CANsub context.
 * @param stream Stream type, see @ref cansub_stream_code.
 * @return Result.
 * @retval CANSUB_RESULT_OK The EOF was processed.
 * @retval CANSUB_RESULT_INVALID_ARG ctx is NULL or the stream is unknown.
 * @retval CANSUB_RESULT_INVALID_STATE No transport is connected or requested for the stream, and TRANSPORT_CLOSE is
 *         not set.
 */
CANSUB_API cansub_result cansub_data_eof(cansub_ctx *ctx, cansub_stream stream);

/** @} */

/**
 * @defgroup cansub_frames CAN frames
 * @brief Reading and sending CAN frames.
 * @{
 */

/**
 * Read the next received CAN frame.
 *
 * Only returns frames from data already passed to @ref cansub_data_sink(). None are available before
 * @ref CANSUB_STATE_OPEN. Frames are returned in bus order (FIFO), not by arbitration ID priority.
 * While frames are unread, the library holds back received data and processes it as they are read.
 * Reading frames therefore advances the context, e.g. answering the remote's close.
 *
 * @param ctx CANsub context.
 * @param[out] frame Storage for the frame.
 * @return Result.
 * @retval CANSUB_RESULT_OK Frame was read.
 * @retval CANSUB_RESULT_AGAIN No frame is available.
 * @retval CANSUB_RESULT_CLOSED No frames remain and the data stream has ended.
 * @retval CANSUB_RESULT_INVALID_ARG ctx or frame is NULL.
 */
CANSUB_API cansub_result cansub_frame_recv(cansub_ctx *ctx, cansub_frame *frame);

/**
 * Send a CAN frame to the remote CANsub.
 *
 * The frame is encoded immediately and reaches the transport when drained.
 *
 * - Frames are accepted in @ref CANSUB_STATE_OPEN. While the context is opening, @ref CANSUB_RESULT_AGAIN is returned.
 * - Frames are sent in the order they are queued (FIFO), not by arbitration ID priority.
 * - Malformed frames (see @ref cansub_frame_validate()) are rejected with @ref CANSUB_RESULT_INVALID_ARG in every
 *   state and are not queued.
 *
 * @param ctx CANsub context.
 * @param[in] frame CAN frame to send.
 * @return Result.
 * @retval CANSUB_RESULT_OK Frame was queued.
 * @retval CANSUB_RESULT_AGAIN No space for the frame, or the context is still opening.
 * @retval CANSUB_RESULT_MEMORY Allocation failure. The frame was not queued, the call can be repeated.
 * @retval CANSUB_RESULT_CLOSED Context is closing or closed.
 * @retval CANSUB_RESULT_INVALID_STATE @ref cansub_open() has not been called.
 * @retval CANSUB_RESULT_INVALID_ARG ctx or frame is NULL, or the frame is malformed.
 */
CANSUB_API cansub_result cansub_frame_send(cansub_ctx *ctx, cansub_frame const *frame);

/**
 * Translate a DLC to the number of data bytes.
 *
 * For classic CAN frames, DLC 9-15 represent 8 bytes. A DLC above 15 is treated as 15.
 *
 * @param dlc DLC from the CAN frame.
 * @param fd Non-zero for a CAN FD frame.
 * @return Number of data bytes.
 */
CANSUB_API uint32_t cansub_frame_dlc_to_nob(uint32_t dlc, uint32_t fd);

/**
 * Translate a number of data bytes to a DLC.
 *
 * Lengths that are not valid CAN lengths round up, e.g. 10 gives DLC 9 (12 bytes). Above 64 bytes, 15 is returned.
 *
 * @param nob Number of data bytes.
 * @return The smallest DLC covering nob bytes. Values above 8 require a CAN FD frame.
 */
CANSUB_API uint32_t cansub_frame_nob_to_dlc(uint32_t nob);

/**
 * Check whether a frame can be sent, without sending it.
 *
 * Applies the rules of @ref cansub_frame_fault_code, as @ref cansub_frame_send() does.
 *
 * @param[in] frame Frame to validate.
 * @param[out] faults Every rule the frame violates, see @ref cansub_frame_fault_code. Set to 0 for a valid frame.
 *             Ignored if NULL, and not written if frame is NULL.
 * @return Result.
 * @retval CANSUB_RESULT_OK Frame is valid.
 * @retval CANSUB_RESULT_INVALID_ARG Frame is malformed, or frame is NULL.
 */
CANSUB_API cansub_result cansub_frame_validate(cansub_frame const *frame, cansub_frame_faults *faults);

/**
 * Calculate the best fitting bit timing for the given bit rates and sample points.
 *
 * The device always operates in FD mode, so the data phase is always calculated.
 *
 * @param[out] bittiming Calculated bit timing. Unspecified unless @ref CANSUB_RESULT_OK is returned.
 * @param bitrate_nominal Nominal bit rate in bit/s.
 * @param sample_point_nominal Nominal sample point in 0.01 %, e.g. 8000 = 80 %.
 * @param bitrate_data Data bit rate in bit/s. Must be at least bitrate_nominal.
 * @param sample_point_data Data sample point in 0.01 %, e.g. 8000 = 80 %.
 * @return Result.
 * @retval CANSUB_RESULT_OK bittiming holds a valid bit timing.
 * @retval CANSUB_RESULT_NOT_FOUND No bit timing is within 0.1 % of the target bit rates.
 * @retval CANSUB_RESULT_INVALID_ARG bittiming is NULL, a bit rate is 0, a sample point is above 10000 or bitrate_data
 *         is below bitrate_nominal.
 */
CANSUB_API cansub_result cansub_calculate_bittiming(cansub_bittiming *bittiming, uint32_t bitrate_nominal, uint32_t sample_point_nominal, uint32_t bitrate_data, uint32_t sample_point_data);

/** @} */

/**
 * @defgroup cansub_diagnostics Context diagnostics
 * @brief Getting the state of the CANsub context, error handling and readiness.
 * @{
 */

/**
 * Get the current state of the CANsub context.
 *
 * @param ctx CANsub context.
 * @return The current state.
 */
CANSUB_API cansub_state cansub_get_state(cansub_ctx const *ctx);

/**
 * Get the current ready flags of the CANsub context.
 *
 * @param ctx CANsub context.
 * @return The current ready flags.
 */
CANSUB_API cansub_ready cansub_get_ready(cansub_ctx const *ctx);

/**
 * Get the transport flags of one stream, moved to the positions of the data stream flags.
 *
 * Lets one handler serve both streams by testing the result against the CANSUB_READY_DATA_* values.
 * Only the stream's byte is returned. Check payload flags (e.g. @ref CANSUB_READY_CAN_RX) on the value from
 * @ref cansub_get_ready().
 *
 * @param ready Ready flags from @ref cansub_get_ready().
 * @param stream Stream type, see @ref cansub_stream_code.
 * @return The stream's transport flags in the data stream positions. 0 for an unknown stream.
 */
CANSUB_API cansub_ready cansub_ready_stream(cansub_ready ready, cansub_stream stream);

/**
 * Get the error recorded for the data stream.
 *
 * Only reports the data stream. Control request errors are returned per request by @ref cansub_response_recv().
 *
 * Unlike the other string outputs, the message is set to an empty string if it does not fit and buffer_size is
 * non-zero. Call with buffer_size 0 to query the size and the error code.
 *
 * The first recorded error is kept: It is never replaced, and reading it does not clear it.
 * A fatal error is available once @ref CANSUB_STATE_CLOSING is entered.
 * A remote close and a lost transport are recorded by @ref cansub_data_eof(), so the final result is only known in
 * @ref CANSUB_STATE_CLOSED.
 *
 * @param ctx CANsub context.
 * @param[out] buffer Buffer for the error message. Can be NULL if buffer_size is 0.
 * @param buffer_size Size of the buffer.
 * @param[out] message_size Required size for the message including the NUL terminator. Ignored if NULL. Set for every
 *             result except @ref CANSUB_RESULT_INVALID_ARG.
 * @return The recorded error of the data stream.
 * @retval CANSUB_RESULT_OK No error. In @ref CANSUB_STATE_CLOSED, the close was clean.
 * @retval CANSUB_RESULT_ERROR An internal error ended the data transport.
 * @retval CANSUB_RESULT_MEMORY An allocation failure ended the data transport, e.g. while checking the upgrade
 *         response or answering the remote's close. Create a new context to connect again.
 * @retval CANSUB_RESULT_CONNECTION_REJECTED The HTTP upgrade was rejected.
 * @retval CANSUB_RESULT_CONNECTION_TLS TLS error, e.g. the device certificate does not match the device ID or the host
 *         (see @ref cansub_context_create()), or the remote rejected the client certificate with a TLS alert.
 * @retval CANSUB_RESULT_CONNECTION_PROTOCOL Invalid upgrade response, or a WebSocket protocol violation by the remote.
 * @retval CANSUB_RESULT_CONNECTION_LOST The remote closed the WebSocket, or the transport was lost, closed or could
 *         not be opened before the close completed. A remote that closes the transport during the TLS handshake
 *         (e.g. rejecting a missing client certificate without an alert) also gives this code.
 * @retval CANSUB_RESULT_INVALID_ARG ctx is NULL, or buffer is NULL with a non-zero buffer_size. Not a recorded error.
 */
CANSUB_API cansub_result cansub_error(cansub_ctx const *ctx, char *buffer, size_t buffer_size, size_t *message_size);

/**
 * Get the name of a result code, e.g. "CANSUB_RESULT_AGAIN".
 *
 * The name follows the string output rules of @ref cansub_conventions.
 *
 * @param code Result code, see @ref cansub_result_code.
 * @param[out] buffer Buffer for the name. Can be NULL if buffer_size is 0.
 * @param buffer_size Size of the buffer.
 * @return Result. Positive values are the size of the name including the NUL terminator. A value larger than
 *         buffer_size means nothing was copied.
 * @retval CANSUB_RESULT_NOT_FOUND Unknown result code.
 * @retval CANSUB_RESULT_INVALID_ARG buffer is NULL with a non-zero buffer_size.
 */
CANSUB_API cansub_result cansub_result_name(cansub_result code, char *buffer, size_t buffer_size);

/**
 * Get the name of a state code, e.g. "CANSUB_STATE_OPEN".
 *
 * The name follows the string output rules of @ref cansub_conventions.
 *
 * @param state State code, see @ref cansub_state_code.
 * @param[out] buffer Buffer for the name. Can be NULL if buffer_size is 0.
 * @param buffer_size Size of the buffer.
 * @return Result. Positive values are the size of the name including the NUL terminator. A value larger than
 *         buffer_size means nothing was copied.
 * @retval CANSUB_RESULT_NOT_FOUND Unknown state code.
 * @retval CANSUB_RESULT_INVALID_ARG buffer is NULL with a non-zero buffer_size.
 */
CANSUB_API cansub_result cansub_state_name(cansub_state state, char *buffer, size_t buffer_size);

/** @} */

/**
 * @defgroup cansub_control Control requests
 * @brief Low-level REST requests and responses over the control stream.
 * @{
 */

/**
 * Queue a request on the control stream.
 *
 * Requests are sent one at a time in queue order (FIFO). The next request is only sent once the previous response has
 * been read. Responses are returned by @ref cansub_response_recv() in the same order, with the tag of their request.
 * A control transport is requested (@ref CANSUB_READY_CONTROL_TRANSPORT_OPEN) if none is connected.
 * A request occupies its queue slot until its response has been read.
 *
 * A TLS error fails the request in progress and all queued ones. For failures when the transport ends, see
 * @ref cansub_data_eof().
 *
 * Accepted in @ref CANSUB_STATE_INITIALIZED, @ref CANSUB_STATE_OPENING and @ref CANSUB_STATE_OPEN.
 * Ignored parameters are still validated, e.g. a content_type containing "\r" fails the request even if body is NULL
 * and body_size is 0.
 *
 * @param ctx CANsub context.
 * @param tag Caller-defined value returned with the response, e.g. to find the caller's record of the request.
 *            Not interpreted by the library.
 * @param[in] method HTTP method, e.g. "GET". Must not contain control characters or spaces.
 * @param[in] path Absolute path on the remote, e.g. "/api/info". Must not contain control characters or spaces.
 * @param[in] content_type Content type of the body, e.g. "application/json". Can be NULL if body is NULL and body_size
 *            is 0. Must not contain control characters.
 * @param[in] body Request body. Can be NULL if body_size is 0.
 * @param body_size Size of the body.
 * @return Result.
 * @retval CANSUB_RESULT_OK Request queued.
 * @retval CANSUB_RESULT_AGAIN The request queue is full. Read pending responses to free slots.
 * @retval CANSUB_RESULT_CLOSED Context is closing or closed.
 * @retval CANSUB_RESULT_INVALID_ARG ctx is NULL, the request exceeds the internal size limit or one of method, path,
 *         content_type and body is invalid.
 */
CANSUB_API cansub_result cansub_request_send(cansub_ctx *ctx, uint64_t tag, char const *method, char const *path, char const *content_type, void const *body, size_t body_size);

/**
 * Read and consume the oldest response from the control stream.
 *
 * This always consumes the response, also with buffer_size 0. To size the buffer first, use
 * @ref cansub_response_peek(). Read headers with @ref cansub_response_header() before consuming. Consuming a response
 * frees its slot in the request queue. A body longer than the buffer is truncated, and the full size is always
 * returned.
 *
 * tag and http_status are set for every response, including failed requests, and left unchanged for
 * @ref CANSUB_RESULT_AGAIN, @ref CANSUB_RESULT_CLOSED and @ref CANSUB_RESULT_INVALID_ARG.
 *
 * @param ctx CANsub context.
 * @param[out] tag Tag of the request this response belongs to, see @ref cansub_request_send(). Ignored if NULL.
 * @param[out] http_status HTTP status code of the response. Set to 0 for a failed request (CONNECTION_* result).
 *             Ignored if NULL.
 * @param[out] buffer Buffer for the response body after removing any chunked transfer encoding, not NUL terminated.
 *             Can be NULL if buffer_size is 0.
 * @param buffer_size Size of the buffer.
 * @return Result. Zero or positive values are the body size before truncation.
 * @retval CANSUB_RESULT_AGAIN No response is available.
 * @retval CANSUB_RESULT_CLOSED No responses remain and no more will arrive.
 * @retval CANSUB_RESULT_CONNECTION_TLS The request failed with a TLS error. The response is consumed.
 * @retval CANSUB_RESULT_CONNECTION_PROTOCOL The response was invalid or exceeded the internal size limit.
 *         The response is consumed.
 * @retval CANSUB_RESULT_CONNECTION_LOST The control transport ended before the response arrived.
 *         The response is consumed.
 */
CANSUB_API cansub_result cansub_response_recv(cansub_ctx *ctx, uint64_t *tag, uint32_t *http_status, void *buffer, size_t buffer_size);

/**
 * Peek at the oldest response from the control stream without consuming it.
 *
 * tag and http_status are set for every response, including failed requests, and left unchanged for
 * @ref CANSUB_RESULT_AGAIN, @ref CANSUB_RESULT_CLOSED and @ref CANSUB_RESULT_INVALID_ARG.
 *
 * @param ctx CANsub context.
 * @param[out] tag Tag of the request this response belongs to, see @ref cansub_request_send(). Ignored if NULL.
 * @param[out] http_status HTTP status code of the response. Set to 0 for a failed request (CONNECTION_* result).
 *             Ignored if NULL.
 * @return Result. Zero or positive values are the body size.
 * @retval CANSUB_RESULT_AGAIN No response is available.
 * @retval CANSUB_RESULT_CLOSED No responses remain and no more will arrive.
 * @retval CANSUB_RESULT_CONNECTION_TLS The request failed with a TLS error.
 * @retval CANSUB_RESULT_CONNECTION_PROTOCOL The response was invalid or exceeded the internal size limit.
 * @retval CANSUB_RESULT_CONNECTION_LOST The control transport ended before the response arrived.
 */
CANSUB_API cansub_result cansub_response_peek(cansub_ctx const *ctx, uint64_t *tag, uint32_t *http_status);

/**
 * Read a header of the oldest response from the control stream, without consuming the response.
 *
 * The name is matched case-insensitively. Repeated headers are joined with ", ".
 * The value is copied byte for byte without validation. It follows the string output rules of
 * @ref cansub_conventions and may contain bytes above 0x7F, so do not decode it as strict ASCII.
 *
 * @param ctx CANsub context.
 * @param[in] name Header name, e.g. "Location".
 * @param[out] buffer Buffer for the header value. Can be NULL if buffer_size is 0.
 * @param buffer_size Size of the buffer.
 * @return Result. Positive values are the size of the value including the NUL terminator. A value larger than
 *         buffer_size means nothing was copied.
 * @retval CANSUB_RESULT_AGAIN No response is available.
 * @retval CANSUB_RESULT_CLOSED No responses remain and no more will arrive.
 * @retval CANSUB_RESULT_NOT_FOUND The response has no such header, or the oldest response is a failed request.
 * @retval CANSUB_RESULT_INVALID_ARG ctx or name is NULL.
 */
CANSUB_API cansub_result cansub_response_header(cansub_ctx const *ctx, char const *name, char *buffer, size_t buffer_size);

/**
 * Get the tag of the request in flight on the control transport.
 *
 * A request is in flight from when its first bytes are queued for @ref cansub_data_drain() until its response arrives
 * or it fails. This is after the TLS handshake and before the glue writes the bytes, so the connect and the handshake
 * are not covered. It is the request that fails with @ref CANSUB_RESULT_CONNECTION_LOST if the control transport ends,
 * see @ref cansub_data_eof().
 * A queued request waiting for a transport is not in flight. The abort of @ref cansub_close_force() is never reported.
 *
 * Use it to time requests and the opening of the control transport, see @ref cansub_conventions_timeouts.
 *
 * @warning Tags are opaque. Reusing a tag for consecutive requests makes a new request indistinguishable from the
 *          previous one, so a running timer is not restarted. Use unique tags when timing requests.
 *
 * @param ctx CANsub context.
 * @param[out] tag Tag of the request in flight, see @ref cansub_request_send(). Ignored if NULL. Only written on
 *             success.
 * @return Result.
 * @retval CANSUB_RESULT_OK A request is in flight, the tag is set.
 * @retval CANSUB_RESULT_NOT_FOUND No request is in flight.
 * @retval CANSUB_RESULT_INVALID_ARG ctx is NULL.
 */
CANSUB_API cansub_result cansub_request_in_flight(cansub_ctx const *ctx, uint64_t *tag);

/** @} */

/** @} */

#ifdef __cplusplus
}
#endif

#endif // CANSUB_LIBRARY_CANSUB_FFI_H
