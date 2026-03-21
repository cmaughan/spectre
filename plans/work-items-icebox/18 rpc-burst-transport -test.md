# Large RPC Burst Transport Test

**Type:** test
**Priority:** 18
**Raised by:** GPT, Gemini

## Summary

Add a test that feeds a large burst of msgpack-encoded RPC messages through the `NvimRpc` decode path and verifies that all messages are decoded correctly, in order, with no data loss and no quadratic-copy performance regression.

## Background

The RPC read path accumulates bytes in a buffer and decodes complete messages. Work item `24` fixes a quadratic copy in this path. This test verifies both the correctness of the decode (all messages reconstructed correctly) and the performance (a large burst completes in a time bounded enough to fail CI if the O(n²) regression returns). The test also covers the split-message case where a message boundary falls across a read buffer boundary.

## Implementation Plan

### Files to modify
- `libs/draxul-nvim/tests/` — add `rpc_burst_test.cpp`
- `libs/draxul-nvim/CMakeLists.txt` — register with ctest

### Steps
- [ ] Write a helper that generates N msgpack-encoded `notify` messages with sequential IDs
- [ ] Write test: feed all N messages as a single contiguous byte blob; verify N messages decoded, correct IDs, correct order
- [ ] Write test: feed messages split at arbitrary byte positions (simulate partial reads); verify same result
- [ ] Write test: feed 1,000 messages of 1 KB each; verify total decode time is sub-second (or any reasonable bound that rules out O(n²) behaviour)
- [ ] Write test: feed a message that is split exactly at its length prefix boundary
- [ ] Write test: feed a message split mid-payload
- [ ] Register with ctest

## Depends On
- `24 rpc-buffer-linear-copy -refactor.md` — the performance test is most meaningful after the fix

## Blocks
- None

## Notes
The performance bound in the timing test should be generous enough not to be flaky on slow CI machines but tight enough to catch a genuine O(n²) regression. A 1,000-message × 1 KB burst should decode in under 100ms on any modern machine; an O(n²) implementation would take several seconds.

> Work item produced by: claude-sonnet-4-6
