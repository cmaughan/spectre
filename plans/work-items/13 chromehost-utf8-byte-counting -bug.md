# WI 13 — `ChromeHost` measures tab/pane labels by byte count, not codepoints

**Type:** bug  
**Severity:** MEDIUM  
**Source:** review-bugs-latest.gpt.md  
**Consensus:** review-consensus.md Phase 3

---

## Problem

Several places in `app/chrome_host.cpp` use raw byte lengths to measure, truncate, and render UTF-8 label text:

| Location | Issue |
|---|---|
| `chrome_host.cpp:288` | Tab width calculation uses byte count |
| `chrome_host.cpp:439` | Tab label truncation uses byte count |
| `chrome_host.cpp:943` | Pane-status truncation uses byte count |
| `chrome_host.cpp:1018` | Glyph warming loop iterates byte-by-byte |

A tab named `"Ångström"` (9 bytes, 8 codepoints) is measured as 9 columns instead of 8. A tab named with CJK characters or emoji is measured even more incorrectly.

Truncation at byte boundaries can split a multibyte sequence, producing an invalid trailing byte sequence that is then passed to the renderer as an invalid one-byte "cluster".

This is especially visible now that pane rename input is UTF-8-aware (the user can type multi-byte characters) while the label layout is not.

**Files:**
- `app/chrome_host.cpp` lines 288, 439, 943, 1018

---

## Implementation Plan

- [ ] Audit all label measurement/truncation sites in `chrome_host.cpp` and replace byte-length operations with UTF-8-aware equivalents.
- [ ] Use the project's existing UTF-8 utilities (`libs/draxul-types` or `libs/draxul-grid` UTF-8 helpers) — do not introduce a new dependency.
- [ ] For glyph warming at line 1018: iterate by codepoint, not byte.
- [ ] For truncation: truncate at a codepoint boundary, never mid-sequence. Optionally append an ellipsis `…` (U+2026) if truncation occurs.
- [ ] Write a unit test (see WI 20) that:
  - Creates a `ChromeHost`-like label measurer with a multi-byte UTF-8 tab name.
  - Asserts column width equals codepoint count (or display-width accounting for wide chars).
  - Asserts truncation produces valid UTF-8.
- [ ] After the fix, run the render-test suite (`py do.py smoke`) to confirm no visual regression in the tab bar.

---

## Interdependencies

- WI 20 (chromehost-utf8-layout test) is the direct companion test for this fix.
- The rename input path (already UTF-8 aware) will look inconsistent until this lands.
- WI 25 (centralised test fixtures) may help if a fake ChromeHost measurement harness is needed.

---

*Filed by: claude-sonnet-4-6 — 2026-04-08*
