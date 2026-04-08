# WI 133 — megacity-plugin-boundary

**Type:** architecture
**Priority:** 18
**Raised by:** Chris Maughan (product scope / packaging direction)
**Status:** proposed

---

## Goal

Keep MegaCity as a valuable Draxul-owned capability without forcing it to remain part of the core terminal/Neovim product surface.

The likely product reality is:

- the **terminal / Neovim frontend is the core shippable**
- MegaCity is **useful, differentiating, and worth keeping**
- but MegaCity should become **optional at the module boundary**, so the terminal can ship, build, test, and evolve without dragging MegaCity through every core decision

This work item is about creating the architecture that makes MegaCity feel like a plugin or external module, even if it initially still lives in the main repo.

---

## Problem

MegaCity is already better isolated than it used to be:

- WI 17 made it optional behind `DRAXUL_ENABLE_MEGACITY`
- WI 24 made it a first-class host tier instead of an ad hoc renderer hack

That was the right move at the time, but it still leaves a product-shape mismatch:

- the app still conceptually treats MegaCity as one of the built-in host types
- CI, testing, docs, and architecture discussions still have to keep MegaCity in mind as part of the main product
- "core Draxul" and "optional Draxul experiences" are not cleanly separated
- making MegaCity an actual external module later would still be awkward because the current boundary is build-time optional, not ownership-boundary clean

The result is that the terminal product and the experimental / 3D / code-city product are still too entangled at the packaging and repo-structure level.

---

## Recommendation

Do **not** start with runtime `dlopen()` / shared-library plugins.

That is the most expensive and least necessary version of "plugin". It introduces ABI, packaging, crash-isolation, versioning, and platform-specific loader complexity before we have even finished defining the stable boundary.

Recommended sequence:

1. **Create a plugin-style host-provider boundary in-tree**
   - MegaCity becomes an optional provider module registered via a narrow interface
   - the terminal build works with zero MegaCity headers or symbols in the core path
2. **Make that provider boundary submodule-friendly**
   - MegaCity can move to `modules/megacity/` or an external repo without redesigning the app
3. **Only later decide whether runtime dynamic loading is worth it**
   - probably only if distribution or third-party host ecosystems become a real product need

So the immediate target is not "true binary plugin loading".
The target is: **MegaCity can be built, tested, versioned, and eventually hosted outside the core repo without changing the terminal architecture again.**

---

## Proposed Target Shape

### Core Product

Core Draxul should own:

- app shell
- windowing
- renderer
- font/grid/input/runtime support
- host interfaces and host registry
- terminal/shell/nvim hosts
- core chrome, panes, workspaces, diagnostics, config

### Optional Module Boundary

MegaCity should be treated as an optional host module that provides:

- one or more host registrations
- its own config extensions
- its own tests
- its own assets/shaders/data expectations
- its own docs page(s), with only a summary in the core features doc

### Practical Layout

A likely end state:

```text
app/
libs/
  draxul-host-api/        # host interfaces / registry contracts
  draxul-terminal-hosts/  # nvim + shell family
modules/
  megacity/               # in-tree at first, submodule-capable later
    CMakeLists.txt
    include/
    src/
    shaders/
    tests/ or test-support/
```

This does **not** require a separate process. MegaCity can still run in-process as a host. The point is ownership and dependency direction, not IPC.

---

## Design Principles

- The terminal product must build and ship cleanly with MegaCity absent.
- Core code should not include MegaCity headers.
- Core docs should describe MegaCity as an optional module, not as the center of the product.
- MegaCity must depend on the host/render APIs; the core app must not depend on MegaCity implementation details.
- The first boundary should be source-level and CMake-level, not ABI-level.
- Tests for core terminal behavior should not link MegaCity.

---

## Non-Goals

- Do not remove MegaCity.
- Do not redesign MegaCity rendering internals as part of this item.
- Do not build a general third-party plugin marketplace.
- Do not commit to runtime dynamic loading yet.
- Do not split the repo immediately unless the provider boundary is already proven.

---

## Work Plan

### Phase 1 — Define the Module Boundary

- [ ] Read the current MegaCity touchpoints end-to-end:
  - `app/host_manager.cpp`
  - `app/host_factory.cpp`
  - top-level `CMakeLists.txt`
  - `libs/draxul-megacity/CMakeLists.txt`
  - config/docs/test paths that mention MegaCity directly
- [ ] Write down the **minimum core-side contract** MegaCity actually needs:
  - host registration
  - renderer access
  - config access
  - action/keybinding hooks if any
  - diagnostics integration
- [ ] Identify every place where the core app still knows MegaCity by concrete type, enum case, include, or build rule.
- [ ] Produce a dependency map showing which current references are:
  - legitimate API dependencies
  - packaging/build wiring
  - avoidable leakage

### Phase 2 — Introduce a Host Provider Registry

- [ ] Add a narrow host-provider/host-registry abstraction in core:
  - "register host kind"
  - "create host by kind"
  - optional metadata such as display name / feature flag / availability
- [ ] Move the terminal/nvim/shell host registrations behind that same registry so MegaCity is not special.
- [ ] Remove any core compile path that includes MegaCity headers directly.
- [ ] Make MegaCity register itself through the same provider interface.

### Phase 3 — Isolate MegaCity as an Optional Module

- [ ] Move MegaCity from `libs/draxul-megacity/` to a boundary that reads as optional module ownership:
  - likely `modules/megacity/`
  - or keep path stable temporarily but make the target shape equivalent
- [ ] Ensure MegaCity’s `CMakeLists.txt` can be included conditionally as a self-contained module.
- [ ] Move MegaCity-specific shaders/assets/build logic under the module boundary.
- [ ] Split MegaCity-specific test support from core tests:
  - terminal/core tests should not pull MegaCity sources or headers
  - MegaCity tests should be grouped behind MegaCity enablement

### Phase 4 — Make It Submodule-Capable

- [ ] Ensure the module can be built from:
  - an in-tree directory
  - a git submodule path
  - potentially a sibling checkout path via CMake option
- [ ] Replace assumptions that MegaCity always lives under the main source tree.
- [ ] Audit include paths, asset paths, DB paths, and shader staging logic for repo-relative assumptions.
- [ ] Document the supported integration shapes:
  - in-tree module
  - git submodule
  - disabled entirely

### Phase 5 — Product and Documentation Cleanup

- [ ] Update `docs/features.md` to present MegaCity as an optional module/experience.
- [ ] Update architecture docs/module map to distinguish core terminal product vs optional modules.
- [ ] Decide whether the default product presets should:
  - build MegaCity by default for developer builds
  - disable MegaCity for lean release packaging
- [ ] Make CI reflect the product split:
  - core build/test lane without MegaCity
  - MegaCity-enabled lane for module health

### Phase 6 — Optional Future Step: Runtime Plugins

- [ ] Only after the provider boundary is stable, evaluate whether runtime-discovered plugins are worth the complexity.
- [ ] If yes, design that as a separate work item with explicit ABI/versioning/loading constraints.

---

## Acceptance Criteria

- [ ] A build with MegaCity disabled produces a clean terminal-first product with no MegaCity implementation leakage in the core compile path.
- [ ] The app creates hosts through a provider/registry boundary rather than concrete MegaCity special-casing.
- [ ] MegaCity can live behind a self-contained module target that is compatible with both in-tree and submodule layouts.
- [ ] Core terminal tests/builds do not require MegaCity headers, assets, or source files.
- [ ] Documentation clearly distinguishes the core shippable product from optional modules.
- [ ] A follow-up work item can move MegaCity into a submodule without another architecture rewrite.

---

## Suggested Follow-Up Work Items

This architecture item should likely decompose into smaller implementation items:

1. `megacity-host-provider-registry -refactor`
2. `megacity-module-path-extraction -refactor`
3. `megacity-test-boundary-cleanup -refactor`
4. `megacity-submodule-compatible-cmake -refactor`
5. `docs-core-vs-modules-product-shape -docs`

---

## Interdependencies

- **WI 17** `megacity-optional-cmake-flag -refactor`:
  baseline capability already exists; reuse it rather than replacing it
- **WI 24** `renderer-host-layering -architecture`:
  keep MegaCity as a first-class host type, but not a first-class product dependency
- **WI 74 / WI 56** renderer/private-include cleanup:
  continue the same dependency-boundary discipline
- **WI 55** MegaCity threading/runtime cleanup:
  can proceed independently, but long-term easier once MegaCity is module-shaped

---

## Key Decision

If this item is approved, the intended direction should be:

**MegaCity remains part of Draxul, but not part of Draxul-core.**

That means:

- keep it
- support it
- test it
- but architect it so the terminal can stand fully on its own

