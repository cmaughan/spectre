# RPC Codec Fuzzing

**Type:** test
**Priority:** 21
**Raised by:** Gemini

## Summary

Set up a libFuzzer or AFL++ fuzz harness for `MpackCodec` that feeds random byte sequences into the decoder and verifies that it never crashes, asserts, or invokes undefined behaviour. This is a self-contained work item well-suited to a dedicated sub-agent.

## Background

The RPC read path accepts raw bytes from neovim's stdout pipe. While a well-behaved neovim will always send valid msgpack, network transports, corrupt processes, or future remote-attach scenarios could deliver malformed data. Fuzzing the codec boundary ensures that no input sequence can crash or corrupt Draxul's process. Fuzzing is the most efficient way to find edge cases in binary protocol parsers.

**Sub-agent note:** This work item is self-contained and ideal for a dedicated sub-agent. The fuzz harness setup (CMake target, seed corpus, LLVM integration) is well-scoped work that does not require context from the rest of the codebase beyond the `MpackCodec` API.

## Implementation Plan

### Files to modify
- `libs/draxul-nvim/fuzz/` — create directory; add `mpack_codec_fuzz.cpp`
- `libs/draxul-nvim/CMakeLists.txt` — add fuzz target gated behind `DRAXUL_ENABLE_FUZZING` CMake option
- `libs/draxul-nvim/fuzz/corpus/` — add seed corpus files (valid msgpack examples: nil, bool, int, float, string, array, map)

### Steps
- [ ] Create `libs/draxul-nvim/fuzz/mpack_codec_fuzz.cpp` with a `LLVMFuzzerTestOneInput` entry point that feeds the fuzz input to `MpackCodec::decode`
- [ ] Add `DRAXUL_ENABLE_FUZZING` CMake option; when ON, compile fuzz target with `-fsanitize=fuzzer,address`
- [ ] Create a seed corpus with at least 10 valid msgpack messages (nil, true, false, small int, large int, float, short string, long string, empty array, nested map)
- [ ] Run the fuzzer locally for at least 30 minutes on the seed corpus; fix any crashes found
- [ ] Document how to run the fuzzer in a comment at the top of the fuzz target file
- [ ] Add a CI job that runs the fuzzer for 60 seconds on each PR touching `libs/draxul-nvim/src/`

## Depends On
- None

## Blocks
- None

## Notes
libFuzzer is available in Clang 6+; it is already present in the macOS toolchain. The fuzzer target should not be built by default — only when `DRAXUL_ENABLE_FUZZING=ON`. Consider also fuzzing the VT parser in `TerminalHostBase` with a second fuzz target (out of scope for this work item but a natural follow-on).

> Work item produced by: claude-sonnet-4-6
