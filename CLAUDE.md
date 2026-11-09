This is the root folder for the C/C++ SDK for CANsub. The primary goal of the library is to ease the workflow for integrating the CANsub hardware CAN interface into third-party projects or applications, independent of their programming language.

An LLM-friendly overview of the CANsub hardware can be found at:

* [CANsub 2](https://canlogger.csselectronics.com/cansub-docs/cansub2/llms.txt)
* [CANsub 4](https://canlogger.csselectronics.com/cansub-docs/cansub4/llms.txt)

If unable to access the above links, ask the user to provide the all-in-one LLM-friendly offline file instead (`CANsub.2 AI Docs` or `CANsub.4 AI Docs`).

The overview may be out of date. When the overview and the behavior of a device disagree, the device is correct.

## Project layout

```
├── docs                    # Auxiliary documentation.
├── include
│   └── cansub
│       └── cansub_ffi.h    # The C-API for the CANsub library.
└── src                     # Source code for the core library.
```

## Building

The build uses CMake, and all dependencies are included in `src/thirdparty`:

```sh
cmake -S . -B build
cmake --build build -j
```

* This builds a static library that bundles the third-party code (`libcansub.a`, `cansub_static.lib` with MSVC) and a shared library (`libcansub.so`, `cansub.dll` on Windows).
* Use a build directory of your own. `cmake-build-debug` and `cmake-build-release` belong to the CLion IDE.

## Licensing

* The library is MIT licensed, see `LICENSE`. Every first-party source file, including generated ones, starts with an `SPDX-License-Identifier: MIT` comment in the comment syntax of its language.
* Third-party code under `src/thirdparty` keeps its own licenses, and its notices are collected in `THIRD_PARTY_NOTICES.md`. See `src/thirdparty/CLAUDE.md`.

## Library interface

* The library only trusts a single TLS certificate, which is embedded at build time from the file named by the CMake cache variable `CANSUB_ROOT_CERT`. It does not use the system trust store. There should never be a need to change this certificate, as all CANsub devices use the same root certificate.
* The library is built as sans-IO. It handles TLS, HTTP, WebSocket and HDLC framing and CAN frame serialization, but it never touches a socket. The caller or glue code is responsible for the rest, including:

  * Opening the connections and moving bytes between them and the library. The device only supports IPv4, so the connections must use an IPv4 address.
  * Determining if the channels are still open, e.g. via TCP keep-alive.
  * Ensuring the correct port is used.
  * Building and parsing the JSON bodies of REST requests and responses.

* The library uses up to two transport channels at the same time, one per stream, each a TCP connection. This applies over both USB and Ethernet, as the device exposes a network interface over USB using CDC-NCM:

  * The data stream carries the CAN frames over WebSockets.
  * The control stream carries out-of-band control data over an HTTP REST API. It may request a new connection after its previous one ended.
