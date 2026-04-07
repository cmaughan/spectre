# WI 115 — NvimHost Duplicate `open_file_at_type:` Handler

**Type:** Bug  
**Severity:** High (maintenance bug magnet, divergence risk)  
**Source:** Gemini review, GPT review  
**Authored by:** claude-sonnet-4-6

---

## Problem

`libs/draxul-host/src/nvim_host.cpp` contains the `open_file_at_type:` dispatch handler **twice** — at approximately L216 and L248 — each with independently copy-pasted embedded Lua blocks. Any fix or enhancement to this feature must be applied to both copies; if only one is updated, the two code paths silently diverge, producing inconsistent behaviour depending on which dispatch route is taken.

Gemini specifically named this as a "concrete maintenance bug magnet." GPT flagged `NvimHost::dispatch_action()` as containing duplicated Lua at those two locations.

---

## Investigation Steps

- [ ] Open `libs/draxul-host/src/nvim_host.cpp`
- [ ] Search for `open_file_at_type` — confirm both handlers exist and are structurally identical or nearly so
- [ ] Determine which dispatch path invokes each copy (string-match switch? separate if-chain?)
- [ ] Check if the two Lua blocks differ in any way (edge case handling, return value, error paths)

---

## Fix Strategy

1. Extract the common logic into a single `nvim_open_file_at_type(path, type)` free function or private method that builds and sends the Lua RPC call.
2. Both dispatch paths call this single function.
3. Write a regression test (see WI 119 for a related `open_file_at_type` RPC dedup test).

---

## Acceptance Criteria

- [ ] `grep -n "open_file_at_type"` in `nvim_host.cpp` shows exactly one implementation site.
- [ ] The embedded Lua block exists in exactly one place.
- [ ] No behaviour change for the normal open-file workflow.
- [ ] CI green.

---

## Interdependencies

- Prerequisite for **WI 126** (Lua extraction refactor — easier to extract after dedup).
- Prerequisite for **WI 127** (NvimHost handler consolidation).
