# Implementer notes

## Context options

The host is always required. It is what the glue connects to, and it is sent in the HTTP Host header. What is verified follows this precedence:

| Condition                                   | Verified                                         |
|---------------------------------------------|--------------------------------------------------|
| device_id set                               | The device ID. The host is not checked.          |
| device_id NULL, accept_any_device set       | Only the root certificate.                       |
| device_id NULL, accept_any_device cleared   | The host against the certificate's hostnames.    |

- The library does not distinguish hostnames from IPv4 addresses. The host is always used as the TLS server name, and only checked in the last row.
- The device certificate lists `<device ID>-usb.local` and `<device ID>-eth.local`. Any other host, e.g. an IPv4 address or a custom DNS name, cannot be
  detected at context creation. In the last row, it fails the TLS handshake with CANSUB_RESULT_CONNECTION_TLS.
- Validation faults: A missing host is CANSUB_OPTIONS_FAULT_HOST_MISSING. accept_any_device has no validation fault.
- With accept_any_device, any CANsub device is accepted as the remote, and so is anyone holding the private key of any CANsub device. Prefer
  device_id whenever the device ID is known.
- The host may only contain letters, digits, '-', '.' and '_', i.e. the characters of a DNS name or an IPv4 address. Anything else, e.g. a
  space, '/', '@', ':' or an IPv6 literal, is CANSUB_OPTIONS_FAULT_HOST.
- Running out of memory while parsing the mTLS certificate or key can be reported as CANSUB_OPTIONS_FAULT_MTLS_CERTIFICATE or
  CANSUB_OPTIONS_FAULT_MTLS_KEY. Retry before treating the input as invalid.

## Threading

- cansub_library_init() and cansub_library_deinit() are thread-safe and reference counted. They may run concurrently with any library function.
- Functions without a context are thread-safe.
- Every call on a context, const or not and on either stream, is serialized by the caller. A context may move between threads if the caller
  orders the calls, e.g. with a mutex.
- Each caller holds its own successful cansub_library_init() while calling cansub_context_options_validate() or cansub_context_create(), and
  until every context it created is destroyed. An initialization held elsewhere in the process may be released at any time.
- The library keeps no thread-local state and never calls back into the glue.
- The two streams of a context share state (the context state, the ready flags and the close logic), so they cannot be driven from two threads at
  once.

## States

| From                     | Trigger                                           | To                   |
|--------------------------|---------------------------------------------------|----------------------|
| CANSUB_STATE_INITIALIZED | cansub_open()                                     | CANSUB_STATE_OPENING |
| CANSUB_STATE_INITIALIZED | cansub_close(), cansub_close_force()              | CANSUB_STATE_CLOSING |
| CANSUB_STATE_OPENING     | WebSocket upgrade accepted                        | CANSUB_STATE_OPEN    |
| CANSUB_STATE_OPENING     | cansub_close(), cansub_close_force(), fatal error | CANSUB_STATE_CLOSING |
| CANSUB_STATE_OPENING     | close from the remote                             | CANSUB_STATE_CLOSING |
| CANSUB_STATE_OPEN        | cansub_close(), cansub_close_force(), fatal error | CANSUB_STATE_CLOSING |
| CANSUB_STATE_OPEN        | close from the remote                             | CANSUB_STATE_CLOSING |
| any but CLOSING, CLOSED  | cansub_data_eof() (DATA), control transport up    | CANSUB_STATE_CLOSING |
| any but CLOSED           | cansub_data_eof() (DATA), control transport down  | CANSUB_STATE_CLOSED  |
| CANSUB_STATE_CLOSING     | cansub_data_eof() (CONTROL), data stream finished | CANSUB_STATE_CLOSED  |

- Data stream finished: cansub_data_eof() (DATA) has been called.
- Control transport up: A control transport is connected or requested (CANSUB_READY_CONTROL_TRANSPORT_OPEN).
- Fatal error: An error that ends the data transport (TLS, upgrade rejected, protocol violation, allocation failure or internal error while processing received data). Errors on the control stream fail the affected requests, but never change the state.
- Close from the remote: A WebSocket close, or a TLS close_notify on the data transport. While OPENING, only the latter.
- The control transport is shut down with CANSUB_READY_CONTROL_TRANSPORT_CLOSE once the data stream has finished, no control requests are
  outstanding and no written abort of cansub_close_force() awaits its response. A pending CANSUB_READY_CONTROL_TRANSPORT_OPEN must still be
  answered. cansub_data_connected() then sets CANSUB_READY_CONTROL_TRANSPORT_CLOSE instead of starting the handshake.
  A control EOF after the data stream has finished fails the remaining requests instead of requesting a new transport.

## Ready flags

The tables list which calls can set and clear each flag, without the `CANSUB_READY_` prefix. They describe the implementation and serve as a
test oracle. The public contract is only "any non-const call may change any flag".
In the first table, a flag without a stream name belongs to the stream in the "Stream" column. Functions not listed do not change any flags.

After `cansub_context_create()`, only `CONTROL_REQUEST` is set.

| Function                    | Stream  | May set                                  | May clear                                                                           |
|-----------------------------|---------|------------------------------------------|-------------------------------------------------------------------------------------|
| cansub_data_connected()     | DATA    | DRAIN, SINK, TRANSPORT_CLOSE             | TRANSPORT_OPEN                                                                      |
| cansub_data_connected()     | CONTROL | DRAIN, SINK, TRANSPORT_CLOSE             | TRANSPORT_OPEN                                                                      |
| cansub_data_drain()         | DATA    | SINK, TRANSPORT_CLOSE, CAN_TX, CAN_RX    | DRAIN, SINK                                                                         |
| cansub_data_drain()         | CONTROL | SINK, TRANSPORT_CLOSE, CONTROL_RESPONSE  | DRAIN, SINK                                                                         |
| cansub_data_drain_peek()    | DATA    |                                          | DRAIN                                                                               |
| cansub_data_drain_peek()    | CONTROL |                                          | DRAIN                                                                               |
| cansub_data_drain_consume() | DATA    | SINK, TRANSPORT_CLOSE, CAN_TX, CAN_RX    | DRAIN, SINK                                                                         |
| cansub_data_drain_consume() | CONTROL | SINK, TRANSPORT_CLOSE, CONTROL_RESPONSE  | DRAIN, SINK                                                                         |
| cansub_data_sink()          | DATA    | DRAIN, TRANSPORT_CLOSE, CAN_RX, CAN_TX   | SINK, CAN_TX, CONTROL_REQUEST                                                       |
| cansub_data_sink()          | CONTROL | DRAIN, TRANSPORT_CLOSE, CONTROL_RESPONSE | SINK                                                                                |
| cansub_data_eof()           | DATA    | CONTROL_TRANSPORT_CLOSE, CONTROL_DRAIN   | SINK, DRAIN, CAN_TX, TRANSPORT_OPEN, TRANSPORT_CLOSE, CONTROL_REQUEST, CONTROL_SINK |
| cansub_data_eof()           | CONTROL | TRANSPORT_OPEN, CONTROL_RESPONSE         | SINK, DRAIN, TRANSPORT_CLOSE, TRANSPORT_OPEN                                        |

cansub_data_sink_space() takes a const context and never changes any flag.
cansub_data_drain_peek() only continues a pending TLS close_notify, as cansub_data_drain() does before reading. This appends to the pending bytes and
never removes any, so peeked bytes stay valid. It clears DRAIN only if the close_notify fails while no bytes are pending.
cansub_data_drain_consume() does the same follow-up work as cansub_data_drain() after removing bytes: It continues a pending close_notify and moves
more stream data in.

| Function               | May set                                                                                                            | May clear                                                    |
|------------------------|--------------------------------------------------------------------------------------------------------------------|--------------------------------------------------------------|
| cansub_open()          | DATA_TRANSPORT_OPEN                                                                                                |                                                              |
| cansub_close()         | DATA_DRAIN, DATA_TRANSPORT_CLOSE                                                                                   | DATA_DRAIN, DATA_SINK, CAN_TX, CONTROL_REQUEST               |
| cansub_close_force()   | DATA_DRAIN, DATA_TRANSPORT_CLOSE, CONTROL_TRANSPORT_CLOSE, CONTROL_TRANSPORT_OPEN, CONTROL_DRAIN, CONTROL_RESPONSE | DATA_DRAIN, DATA_SINK, CAN_TX, CONTROL_SINK, CONTROL_REQUEST |
| cansub_frame_recv()    | DATA_SINK, DATA_DRAIN, DATA_TRANSPORT_CLOSE, CAN_TX                                                                | DATA_SINK, CAN_RX, CAN_TX, CONTROL_REQUEST                   |
| cansub_frame_send()    | DATA_DRAIN                                                                                                         | CAN_TX                                                       |
| cansub_request_send()  | CONTROL_TRANSPORT_OPEN, CONTROL_DRAIN, CONTROL_TRANSPORT_CLOSE, CONTROL_RESPONSE                                   | CONTROL_SINK, CONTROL_REQUEST                                |
| cansub_response_recv() | CONTROL_REQUEST                                                                                                    | CONTROL_RESPONSE                                             |

The rows cover the regular paths, including a close from the remote. On top of that, a fatal TLS error can surface in every call that moves data
through a transport: cansub_data_connected(), cansub_data_drain(), cansub_data_drain_consume(), cansub_data_sink(), cansub_frame_recv(), cansub_frame_send(), cansub_close(),
cansub_close_force() and cansub_request_send(). Such a call may then also set TRANSPORT_CLOSE and DRAIN, and clear SINK, of the failing stream.
On the data stream it also clears CAN_TX and CONTROL_REQUEST (the state moves to CLOSING). On the control stream it also sets CONTROL_RESPONSE
(the affected requests fail).

## Stream closure

The "Ended by" column names what ends the transport:

- A TRANSPORT_CLOSE flag: The library is done with the transport, see its description in cansub_ffi.h.
- Glue: The glue detects or causes the end, e.g. a closed socket, a failed connect or a timeout, and calls cansub_data_eof().
- Either: Both can happen, depending on how the remote ends the transport, e.g. with or without a TLS close_notify.

### Data stream

| Path                                                                           | Ended by                                                                           | Recorded error (cansub_error())   |
|--------------------------------------------------------------------------------|------------------------------------------------------------------------------------|-----------------------------------|
| cansub_close() in OPEN, remote confirms                                        | DATA_TRANSPORT_CLOSE                                                               | CANSUB_RESULT_OK                  |
| cansub_close() in INITIALIZED, OPENING                                         | DATA_TRANSPORT_CLOSE (immediately, or by cansub_data_connected() while connecting) | CANSUB_RESULT_OK                  |
| cansub_close_force() in CLOSING                                                | DATA_TRANSPORT_CLOSE, once the remote confirms                                     | CANSUB_RESULT_OK                  |
| Remote closes the WebSocket                                                    | DATA_TRANSPORT_CLOSE, after answering it                                           | CANSUB_RESULT_CONNECTION_LOST     |
| Remote ends the TLS session (close_notify) without a WebSocket close           | DATA_TRANSPORT_CLOSE                                                               | CANSUB_RESULT_CONNECTION_LOST     |
| TLS error, e.g. the device certificate does not match the device ID or host    | DATA_TRANSPORT_CLOSE                                                               | CANSUB_RESULT_CONNECTION_TLS      |
| mTLS: The remote rejects the client certificate with a TLS alert               | DATA_TRANSPORT_CLOSE                                                               | CANSUB_RESULT_CONNECTION_TLS      |
| mTLS: The remote closes during the handshake                                   | Glue                                                                               | CANSUB_RESULT_CONNECTION_LOST     |
| HTTP upgrade rejected                                                          | DATA_TRANSPORT_CLOSE                                                               | CANSUB_RESULT_CONNECTION_REJECTED |
| Invalid HTTP upgrade response, or a WebSocket protocol violation by the remote | DATA_TRANSPORT_CLOSE                                                               | CANSUB_RESULT_CONNECTION_PROTOCOL |
| Allocation failure while processing received data                              | DATA_TRANSPORT_CLOSE                                                               | CANSUB_RESULT_MEMORY              |
| Internal error while processing received data                                  | DATA_TRANSPORT_CLOSE                                                               | CANSUB_RESULT_ERROR               |
| Remote closes or loses the transport before the close completed                | Glue                                                                               | CANSUB_RESULT_CONNECTION_LOST     |
| Transport could not be opened                                                  | Glue                                                                               | CANSUB_RESULT_CONNECTION_LOST     |
| Glue timeout while opening (before CANSUB_STATE_OPEN)                          | Glue                                                                               | CANSUB_RESULT_CONNECTION_LOST     |

### Control stream

Errors on the control stream never change the state or the recorded error. The control stream only changes the state when its transport ends after the data stream has finished, which moves the context from CANSUB_STATE_CLOSING to CANSUB_STATE_CLOSED (see States).

| Path                                                  | Ended by                | Requests                                                                                          |
|-------------------------------------------------------|-------------------------|---------------------------------------------------------------------------------------------------|
| Data stream finished, no requests outstanding         | CONTROL_TRANSPORT_CLOSE | None affected                                                                                     |
| Remote closes an idle transport                       | Either                  | None affected, a new transport is requested for the next request                                  |
| Invalid or too large response, TLS error              | CONTROL_TRANSPORT_CLOSE | The request in progress fails (TLS: Also the queued ones)                                         |
| Remote closes or loses the transport during a request | Glue                    | The request in progress fails with CANSUB_RESULT_CONNECTION_LOST, queued ones get a new transport |
| Glue request timeout (no response)                    | Glue                    | As above                                                                                          |
| Transport could not be opened                         | Glue                    | All queued requests fail with CANSUB_RESULT_CONNECTION_LOST                                       |
| Remote closes during the TLS handshake                | Glue                    | All queued requests fail with CANSUB_RESULT_CONNECTION_LOST, no new transport                     |
| Glue open timeout (TLS handshake not completed)       | Glue                    | As above                                                                                          |
| Any end after the data stream finished                | Either                  | All remaining requests fail, no new transport is requested                                        |

- A transport which ends before its TLS handshake has completed counts as "could not be opened", e.g. when the remote rejects the client certificate (mTLS) by closing it. Requesting a new transport would fail the same way again.
- A request is outstanding from cansub_request_send() until its response has arrived or it failed. Received responses which have not been read do not keep the control transport up, they can still be read in CLOSED.
  While a response is unread, the control stream reads nothing further from the transport. This holds back nothing that is needed: Requests are not pipelined, so the next request is only sent once the response has been read, and only the remote's close can follow it.
- A request is in flight (cansub_request_in_flight()) while it is outstanding and bound to the current transport: From the moment its first bytes have moved into the transport queue inside the library until its response has arrived or it failed. It is the request that fails if the transport ends. A queued request waiting for a transport is outstanding, but not in flight.
  A request only comes into flight after the TLS handshake of the transport has completed, so the connect and the handshake are not covered. The glue times them with a separate open timeout (see the Timeouts section of the API conventions in cansub_ffi.h).
- CANSUB_STATE_CLOSED is reached once the data stream has finished and the control transport has ended. A request in progress delays it. To bound the close, the glue ends the control transport after a request timeout (see the Timeouts section of the API conventions in cansub_ffi.h).
- The abort of cansub_close_force() has its own buffer, so a full request queue never blocks it. It is written once no request is outstanding. Requests are not accepted while closing, so it only waits for the requests queued before the close. See cansub_close_force() for its behavior when the control transport ends.
  It is never reported by cansub_request_in_flight(). A control transport opened while no requests are outstanding only carries the abort, so the glue cannot stop an open timeout for it. The closing timeout bounds it instead.
  The glue knows whether requests are outstanding from its own count of successful cansub_request_send() calls and of responses that
  cansub_response_peek() reports.
- An abort in progress makes cansub_close_force() return CANSUB_RESULT_AGAIN without starting anything. If the abort does not complete in time, the
  glue ends the control transport (close the socket, cansub_data_eof()), which fails the abort, and then calls cansub_close_force() again.
- A response without Content-Length or chunked encoding ends with the transport, so a control EOF completes it instead of failing it.
