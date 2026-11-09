Main implementation of the CANsub library, which implements the C FFI in `include/cansub/cansub_ffi.h`.

All code in here must be MIT licensed.

The code is split into several files to ease maintenance and readability.

```
├── include
│   └── cansub
│       └── cansub_ffi.h                    # The C-API for the CANsub library
├── src                                     # Source code for the core library
│   ├── src                                 # This folder
│   │    ├── cansub.c                       # Binding, exposing the public API to the internal files
│   │    ├── cansub_crypto_config.h         # TF-PSA-Crypto configuration (PSA algorithms, key store, entropy)
│   │    ├── cansub_lock.h                  # Locking semantics
│   │    ├── cansub_mbedtls_config.h        # mbedTLS configuration (TLS 1.2 and X.509)
│   │    ├── cansub_root_cert.crt           # Trusted root certificate, embedded by the build (CANSUB_ROOT_CERT)
│   │    ├── common.c/.h                    # Common internal functions and state masks
│   │    ├── http.c/.h                      # HTTP parser helper
│   │    ├── ringbuffer.h                   # Ringbuffer implementation
│   │    ├── stream.h                       # Common stream interface
│   │    ├── stream_control.c/.h            # Remote configuration via request/response queue and force close
│   │    ├── stream_data.c/.h               # Upgrade, WebSocket and CAN frames in HDLC transport
│   │    ├── transport.c/.h                 # Transport handling (TLS, buffers and life-cycle)
│   │    ├── websocket.c/.h                 # WebSocket framing
│   │    ├── mbedtls_alt                    # Alternative mbedTLS implementations
│   │    │    └── threading_alt.c/.h        # Native Windows threading (MBEDTLS_THREADING_ALT)
│   │    └── cansub_protocol                # CAN protocol and HDLC framing
│   │         ├── cansub_protocol.h         # Protocol interface and constants
│   │         ├── cansub_protocol_common.c  # DLC and length conversion
│   │         ├── cansub_protocol_decode.c  # Decoder
│   │         └── cansub_protocol_encode.c  # Encoder
│   └── thirdparty                          # Third-party code
│        ├── mbedtls-4.2.0                  # TLS handling via mbedTLS
│        ├── picohttpparser-1.5             # HTTP parsing via picohttpparser
│        └── zephyr-4.5.0                   # Zephyr convenience functions
```

## Encapsulation

* Only the file that implements a struct reads or writes its members directly. All other files go through its functions, e.g. `cansub_ringbuffer` members are only touched in `ringbuffer.h`, and other files call `cansub_ringbuffer_empty()` and the like.
* This is similar to the PIMPL pattern in C++, except that the struct definitions stay in the headers. Other structs can then embed them without a separate allocation.
* Exception: Structs whose only job is passing values in or out of a function may be read and written directly, e.g. `cansub_http_request` and `cansub_http_header` built with designated initializers, or `cansub_ringbuffer_spans` returned by value.
