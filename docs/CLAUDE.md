This folder contains auxiliary documentation for the public interface of the CANsub library in the `include/cansub/cansub_ffi.h` file.

`implementer_notes.md` collects details that are too long for the header, e.g. the state transitions, the ready flags each call can change and how each stream ends. The ready flag tables describe the implementation and serve as a test oracle. They go beyond the public contract.

The header is the source of truth. If the documentation here disagrees with the header, the header is correct and the documentation must be updated.
