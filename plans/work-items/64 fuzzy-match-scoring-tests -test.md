---
# WI 64 — CommandPalette Fuzzy Match Scoring Correctness Tests

**Type:** test  
**Priority:** high (untested scoring logic is a palette usability risk)  
**Raised by:** [C] Claude  
**Created:** 2026-04-03  
**Model:** claude-sonnet-4-6

---

## Problem

`fuzzy_match.cpp` has no dedicated test file. The scoring and match-position logic underpins the command palette's result ordering, but its correctness (exact match > substring > subsequence, shorter target wins ties, match positions correct for highlight rendering) is untested.

---

## Investigation Steps

- [ ] Read `app/fuzzy_match.h` and `app/fuzzy_match.cpp` to understand the scoring API
- [ ] Identify the public surface: likely `fuzzy_match(std::string_view pattern, std::string_view target) -> std::optional<MatchResult>` or similar
- [ ] Identify the `MatchResult` fields: score and match positions

---

## Test Cases to Cover

- [ ] Exact match scores higher than substring match (`"split" vs "split_vertical"`)
- [ ] Shorter target wins score tie when pattern matches both fully
- [ ] Subsequence match (`"sv"` matches `"split_vertical"`) returns correct positions
- [ ] Non-matching pattern returns no result (`nullopt` or score below threshold)
- [ ] Case-insensitive matching (if supported)
- [ ] Empty pattern — define and test expected behaviour
- [ ] Match positions array covers exactly the matched indices, used for highlight rendering
- [ ] Pattern longer than target — returns no match
- [ ] Single-character pattern matching first vs. non-first position

---

## Implementation

- [ ] Create `tests/fuzzy_match_test.cpp`
- [ ] Add it to `tests/CMakeLists.txt`
- [ ] Write Catch2 `TEST_CASE` / `SECTION` blocks covering all cases above

---

## Acceptance Criteria

- [ ] All test cases pass
- [ ] Exact match ordering is verified with at least 5 pairs
- [ ] Match positions are verified to be non-overlapping and in-bounds
- [ ] No changes to `fuzzy_match.cpp` itself unless a bug is uncovered (document any bugs found as separate WIs)
