# WI 09 — `int64_t` msgid silently truncated to `uint32_t` without range validation

**Type:** bug  
**Severity:** MEDIUM  
**Source:** review-bugs-latest.claude.md  
**Consensus:** review-consensus.md Phase 4

---

## Problem

`libs/draxul-nvim/src/rpc.cpp` lines 212 and 240:

```cpp
uint32_t msgid = (uint32_t)msg_array[1].as_int();   // line 212
auto req_msgid = (uint32_t)msg_array[1].as_int();   // line 240
```

`as_int()` returns `int64_t`. A negative or `> UINT32_MAX` value (from a corrupted stream or future nvim protocol change) silently wraps. A negative ID could collide with a valid in-flight `msgid`, causing the wrong waiting thread to be unblocked with another request's response — silent correctness failure, potentially manifesting as a frozen UI or wrong command result.

**Files:**
- `libs/draxul-nvim/src/rpc.cpp` (~lines 212, 240)

---

## Implementation Plan

- [ ] At both call sites, validate the raw `int64_t` before casting:
  ```cpp
  int64_t raw_id = msg_array[1].as_int();
  if (raw_id < 0 || raw_id > static_cast<int64_t>(UINT32_MAX)) {
      DRAXUL_LOG_WARN(LogCategory::Rpc, "msgid out of range: {}", raw_id);
      return;
  }
  uint32_t msgid = static_cast<uint32_t>(raw_id);
  ```
- [ ] Apply the same guard to the request-dispatch path if it also uses an unchecked cast.
- [ ] Add a unit test with a fake RPC stream that injects a negative msgid and asserts graceful discard (no response mismatch, no crash).

---

## Interdependencies

- Small, self-contained fix; can land after Phase 1 or independently.
- Related to WI 05 (reader exception handling) — both are in `rpc.cpp`.

---

*Filed by: claude-sonnet-4-6 — 2026-04-08*
