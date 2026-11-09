This folder contains the public interface of the CANsub library in the `cansub/cansub_ffi.h` file.

## Rules

* To make machine translation easier, the FFI should contain as few regular C defines as possible while still maintaining an ergonomic interface. Binding generators read enums together with their type, but they often skip macros or lose their type. Bindings that load the library at runtime cannot read macros at all.
* The transport flags for each stream (`cansub_ready_code`) have an identical layout within their byte. Macros would make this layout easier to work with in the public API, but that clashes with the rule above. `cansub_ready_stream()` moves the flags of a stream to the data stream positions instead.
* The last bit in the `cansub_frame_flags` enum is reserved for future expansion.
* All changes to the FFI must keep `docs/implementer_notes.md` consistent with the header.
* `src/CMakeLists.txt` reads the library version from the header with a regular expression that expects `#define CANSUB_VERSION 0x<hex digits>` on one line. Keep `CANSUB_VERSION` in that form, or update the CMake file in the same change.

## Corrections

* Do not flag the use of a Doxygen `@page` inside a `@defgroup` as an issue.
