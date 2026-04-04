---
# WI 74 — Fix Megacity Private Build Include Leakage

**Type:** refactor  
**Priority:** high (blocks safe renderer refactors; weakens test isolation)  
**Raised by:** [P] GPT (HIGH)  
**Created:** 2026-04-03  
**Model:** claude-sonnet-4-6

---

## Problem

`libs/draxul-megacity/CMakeLists.txt` adds private renderer backend include paths directly to the megacity target. This means megacity can reach into `libs/draxul-renderer/src/` (private implementation) even though that directory is not part of the renderer's public API. Any renderer internal refactor is forced to check whether megacity uses the changed private header.

Additionally, `tests/CMakeLists.txt` adds `libs/draxul-megacity/src` to the test include path, making tests part of megacity's implementation surface. Tests that include megacity private headers cannot be safely maintained independently of megacity internals.

Specific file references (per [P]):
- `libs/draxul-megacity/CMakeLists.txt` lines ~80 and ~89
- `tests/CMakeLists.txt` line ~19

---

## Investigation Steps

- [ ] Read `libs/draxul-megacity/CMakeLists.txt` to identify all `target_include_directories` lines that reference renderer private paths
- [ ] Read `tests/CMakeLists.txt` line ~19 to see which megacity private headers are included in tests
- [ ] Identify which specific headers are being reached through these paths
- [ ] Determine which headers can be promoted to megacity's public API vs. which represent true implementation leakage

---

## Implementation

### Step 1: Megacity → Renderer private include removal
- [ ] For each renderer private header megacity reaches: either promote it to the renderer's public include, or refactor megacity to use only the public renderer API (`I3DRenderer`, `IRenderPass`, `IRenderContext`)
- [ ] Remove the offending `target_include_directories` lines from megacity's CMakeLists

### Step 2: Test → Megacity private include removal
- [ ] Identify which tests include megacity private headers and why
- [ ] Either move the tested logic to a megacity public header, or create a megacity test-support header under `libs/draxul-megacity/include/draxul/test_support/`
- [ ] Remove the `libs/draxul-megacity/src` from `tests/CMakeLists.txt`

---

## Acceptance Criteria

- [ ] `libs/draxul-megacity/CMakeLists.txt` has no `target_include_directories` pointing into any `src/` directory of another library
- [ ] `tests/CMakeLists.txt` has no `libs/draxul-megacity/src` include path
- [ ] All tests pass; megacity builds and renders correctly (smoke test)
- [ ] A future change to `libs/draxul-renderer/src/` does not require consulting `libs/draxul-megacity/`

---

## Notes

**Do not** remove megacity or refactor its rendering architecture — that is icebox items 16/17. This item is purely about the CMake build wiring. Focused and low-drama.

A subagent is appropriate — the CMake changes are isolated but require understanding both the renderer and megacity public APIs to determine what to promote vs. remove.
