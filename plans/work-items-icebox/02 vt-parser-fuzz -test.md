# 02 vt-parser-fuzz — Test

## Summary

`vt_parser.cpp` implements a VT state machine that processes terminal escape sequences from child processes (nvim, shell). The input is untrusted: any program running in the terminal can emit arbitrary byte sequences. State machines of this type are a common source of security vulnerabilities and crashes when given malformed input. There are currently no fuzz tests for the VT parser. A libFuzzer corpus would exercise all state transitions automatically and catch hang/crash paths that no hand-written unit test covers.

**Raised by:** Claude (review-latest.claude.md), corroborated by GPT

## Steps

- [ ] 1. Read `libs/draxul-host/src/vt_parser.cpp` — understand the state machine structure, entry points, and callback interface.
- [ ] 2. Read `libs/draxul-host/include/draxul/vt_parser.h` (public) and `libs/draxul-host/src/vt_parser.h` (internal) — understand the API surface to call from the fuzz harness.
- [ ] 3. Read `tests/CMakeLists.txt` — understand how existing tests are registered, and whether there is an existing libFuzzer/sanitizer build path.
- [ ] 4. Read `CMakeLists.txt` and `cmake/` — check for ASan/libFuzzer preset infrastructure (the `mac-asan` preset already enables ASan/LSan).
- [ ] 5. Write a fuzz harness `tests/fuzz/vt_parser_fuzz.cpp`:
   ```cpp
   #include "draxul/vt_parser.h"
   #include <cstdint>
   #include <cstddef>

   extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
       // construct a VtParser with a no-op callback set
       // feed data as a byte span
       // return 0 (never crash = success)
       return 0;
   }
   ```
- [ ] 6. Wire the fuzz target into CMakeLists.txt behind a `DRAXUL_ENABLE_FUZZING` option so it does not affect normal test builds. Only link it when `-fsanitize=fuzzer` is available (Clang).
- [ ] 7. Create an initial seed corpus `tests/fuzz/corpus/vt_parser/` with a handful of real VT sequences (e.g. ESC[2J, ESC[1;32m, ESC]8;;http://example.com ST, ESC[?1049h).
- [ ] 8. Run the fuzzer locally for at least 60 seconds: `./build/vt_parser_fuzz -max_total_time=60 tests/fuzz/corpus/vt_parser/`. Confirm no crashes or hangs.
- [ ] 9. If crashes are found, fix them in `vt_parser.cpp` before marking this item complete.
- [ ] 10. Add a comment in `tests/CMakeLists.txt` documenting how to run the fuzzer manually.
- [ ] 11. Mark complete and move to `plans/work-items-complete/`.

## Acceptance Criteria

- A libFuzzer harness for `VtParser` exists under `tests/fuzz/`.
- The harness builds cleanly with `-DDRAXUL_ENABLE_FUZZING=ON`.
- A seed corpus of at least 5 representative VT sequences is committed.
- The fuzzer runs for 60 seconds without crashes or hangs on the seed corpus.
- Any crashes found during development are fixed before this item closes.

## Interdependencies

- Benefits from `07 terminal-host-base-further-split -refactor.md` but is not blocked by it.
- Fuzzing is most valuable when the parser is a small, well-isolated unit — the split helps but is not required.
- A sub-agent running the fuzzer in isolation is appropriate here.

*Authored by: claude-sonnet-4-6*
