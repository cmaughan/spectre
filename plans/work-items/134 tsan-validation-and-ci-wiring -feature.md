# WI 134 — TSan Validation and CI Wiring

**Type:** feature
**Priority:** 8 (CI quality — ThreadSanitizer will catch data races before users do)
**Source:** follow-up from WI 112 + WI 123
**Created:** 2026-04-08

---

## Problem

WI 112 landed the `mac-tsan` CMake preset and `DRAXUL_ENABLE_TSAN` option,
and `cmake --preset mac-tsan` configures cleanly. The actual "run the
test suite under TSan, triage findings, suppress false positives, and
wire it into CI" work was deliberately deferred so the preset itself
could land independently of race triage.

That follow-up needs doing now that the preset exists. WI 123's
`UiRequestWorker` overlap tests are particularly relevant — they were
written specifically to be exercised under TSan to validate the
worker's threading model, but their TSan acceptance criterion was
parked pending this WI.

---

## Investigation

- [ ] `cmake --preset mac-tsan && cmake --build build --target draxul-tests`
- [ ] `ctest --test-dir build -R draxul-tests` and capture all TSan output.
- [ ] Categorise findings:
  - **Real races** in our code → fix at the source (likely candidates: WI 89,
    WI 100, WI 101 areas).
  - **Library / SDK noise** (SDL3 event queue, Metal driver internals,
    libsystem) → add to a `tsan.supp` suppression file with comments
    explaining each entry.
- [ ] Run the smoke test under TSan too: `./build/draxul.app/Contents/MacOS/draxul --console --smoke-test`.

---

## Fix Strategy

- [ ] Land any required race fixes as separate small commits, referencing
  the TSan stack trace in each commit message.
- [ ] Add `tsan.supp` at the repo root with a header comment pointing at
  this WI for each suppression's rationale.
- [ ] Wire `mac-tsan` into the CI workflow (`.github/workflows/build.yml`)
  on macOS — likely as a separate job, not gating the main build, until
  it has been stable for at least one cycle.
- [ ] Update WI 123's "ran under TSan" gap and tick the criterion.

---

## Acceptance Criteria

- [ ] `ctest --test-dir build -R draxul-tests` runs cleanly under
  `mac-tsan` (no unsuppressed findings).
- [ ] Smoke test runs cleanly under `mac-tsan`.
- [ ] `tsan.supp` is committed and documented in `CLAUDE.md`.
- [ ] CI runs the TSan job on every PR (or at least nightly) and the
  job is green.

---

## Interdependencies

- **WI 112** (tsan-build-preset) — provides the preset; landed.
- **WI 123** (uirequestworker overlap tests) — these tests' TSan
  acceptance criterion folds into this WI.
- **WI 89, 100, 101** — known threading concerns most likely to surface
  as TSan findings; may need to be tackled here or via separate fix WIs
  spawned from the TSan output.
