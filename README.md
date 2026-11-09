# C/C++ SDK for CANsub

This repository contains the common core code for CANsub projects. The core library is written in C and exposes a C API for other languages to consume via their respective FFI-bindings.

## Layout

```
├── docs                # Auxiliary documentation for the core library.
├── include             # Global header files for the core library.
└── src                 # Source code for the core library.
```

## Specification

The spec is the C API defined in `include/cansub/cansub_ffi.h`.
Auxiliary documentation can be found in the `docs` directory.

## Building

The build uses CMake, and all dependencies are included in `src/thirdparty`:

```sh
cmake -S . -B build
cmake --build build -j
```

This builds a static library that bundles the third-party code (`libcansub.a`, `cansub_static.lib` with MSVC) and a shared library (`libcansub.so`, `cansub.dll` on Windows).

## License

The C/C++ SDK for CANsub is licensed under the MIT License, see `LICENSE`.

The third-party code under `src/thirdparty` keeps its own licenses. Their notices are collected in `THIRD_PARTY_NOTICES.md`.
