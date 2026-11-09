Core implementation of the CANsub library, which implements the C FFI in `include/cansub/cansub_ffi.h`.

Third-party code is placed under the `src/thirdparty` folder. The main implementation is placed under `src/src`.

## Coding rules

* Always use the `CANSUB_STATE_IN` macro for state checks, even when checking a single state. Do not compare states with `==`.
* Only input from the public FFI is checked for NULL at runtime. Internal code relies on `assert()`.
* Public FFI functions use copy-out buffers and never return borrowed pointers.
* Avoid hidden allocations.
