All 21 work items and the consensus file are written. Here's a summary of what was produced:

---

## `plans/reviews/review-consensus.md` — Full Replacement

9-section synthesis covering bugs, smells, tests, features, genuine disagreements (especially Megacity: remove vs. fix vs. ignore), strengths to protect, recommended fix order, and an interdependency table. Every finding attributed to `[C]`, `[G]`, or `[P]`.

---

## 21 New Work Items (WI 61–81)

### Bugs (3)
| # | File | Summary |
|---|------|---------|
| 61 | `61 types-h-duplicate-include -bug` | Duplicate `#include <glm/glm.hpp>` in types.h |
| 62 | `62 selection-limit-no-user-feedback -bug` | 8192-cell selection silently truncates with no warning — **all 3 agents** |
| 63 | `63 imgui-font-formula-magic-number -bug` | Font formula duplicated 3× with undocumented magic `2` |

### Tests (7)
| # | File | Summary |
|---|------|---------|
| 64 | `64 fuzzy-match-scoring-tests -test` | CommandPalette fuzzy match correctness and position accuracy |
| 65 | `65 hostmanager-zoom-close-tests -test` | Pane zoom + close interaction |
| 66 | `66 config-reload-multi-pane -test` | Font reload propagates to all panes; rollback coherence |
| 67 | `67 gui-action-registry-parity -test` | **Prerequisite for WI 71** — enforces 3-registry parity |
| 68 | `68 split-tree-zero-dimension -test` | SplitTree handles `recompute(0,0)` safely |
| 69 | `69 concurrent-host-shutdown -test` | Host exits between pump and close; no dangling pointer |
| 70 | `70 command-palette-host-lifecycle -test` | Atlas state clean across open/close/reopen cycles |

### Refactors (6)
| # | File | Summary |
|---|------|---------|
| 71 | `71 gui-action-single-source-of-truth -refactor` | **HIGH** — centralise 3 action registries into one table |
| 72 | `72 input-dispatcher-mouse-helper -refactor` | 3 identical 7-step mouse handler patterns → private helper |
| 73 | `73 shader-quad-vertex-dedup -refactor` | Quad offset table hardcoded in 4 shaders → shared include |
| 74 | `74 megacity-private-build-includes -refactor` | **HIGH (GPT)** — fix CMake private-include leakage |
| 75 | `75 utf8-header-extraction -refactor` | Move 100+ lines of `detail::` UTF-8 out of `grid.h` |
| 76 | `76 cli-parsing-exit-separation -refactor` | Remove `std::exit()` from inside argument parser |

### Features (5)
| # | File | Summary |
|---|------|---------|
| 77 | `77 split-divider-drag-feedback -feature` | Visible divider + resize cursor + mouse drag to resize |
| 78 | `78 pane-status-bar -feature` | Per-pane status strip showing host type, dims, cwd |
| 79 | `79 config-error-line-numbers -feature` | TOML parse errors include line number |
| 80 | `80 multi-cell-ligature -feature` | Expand ligature lookahead beyond 2 cells (`===`, `!==`, etc.) |
| 81 | `81 selection-limit-raise -feature` | Raise `kSelectionMaxCells` from 8192 to 65536 |

### Key interdependencies
- **WI 67 → WI 71** (parity test must land before the SSOT refactor)
- **WI 62 ↔ WI 81** (warning + raise are companion items; can batch)
- **WI 59 ↔ WI 60** (copy-on-select + double-click touch same selection code)
- **WI 77 → WI 78** (divider drag and status bar both affect pane chrome layout)
- **WI 74** unblocks any future renderer refactor that touches megacity
