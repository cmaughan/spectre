# Learnings

Useful techniques and tooling discoveries made during the project.

---

## GitHub Project Board Sync

The work-item markdown files in `plans/work-items/` and `plans/work-items-icebox/` can be synced to the GitHub project board (project #1, "Draxul") using:

```
python do.py syncboard
```

Or directly:

```
python sync_project_board.py
```

### How it works

- Items in `plans/work-items/` are created/maintained as **Backlog** on the board.
- Items in `plans/work-items-icebox/` are created/maintained as **IceBox**.
- Items already in **Ready / In progress / In review / Done** are left untouched — the script never resets active work.
- Matching is by title (extracted from the `# H1` heading of each file).
- The script is idempotent; safe to run at any time.

### Setup (first time only)

The `gh` CLI needs the `project` OAuth scope:

```
gh auth refresh -h github.com -s project
```

### Implementation

`sync_project_board.py` at the repo root uses `gh api graphql` mutations:
- `addProjectV2DraftIssue` to create new items
- `updateProjectV2ItemFieldValue` to set the Status field

---

## SonarCloud: Duplicate Header Detection

SonarCloud flagged `libs/draxul-host/src/scrollback_buffer.h` as a duplicate of `libs/draxul-host/include/draxul/scrollback_buffer.h`. The `src/` copy was a leftover from before the module-boundary refactor (work item 15) and had drifted out of sync with the public header.

The fix: delete the `src/` copy and update the `.cpp` include from `"scrollback_buffer.h"` to `<draxul/scrollback_buffer.h>`. The linter enforced this automatically.

**Rule**: each header belongs in exactly one location. Public API → `include/draxul/` (angle-bracket include). Internal implementation detail → `src/` (quote include). Never maintain both. If SonarCloud reports a duplication, the `src/` copy is almost always the stale one.

---

## pre-commit for Automatic clang-format Enforcement

The repo uses [pre-commit](https://pre-commit.com) to run `clang-format` automatically on every `git commit`, catching formatting issues before they reach CI or review.

### Configuration

`.pre-commit-config.yaml` at the repo root:

```yaml
repos:
  - repo: local
    hooks:
      - id: clang-format
        name: clang-format
        language: system
        entry: clang-format
        args: [-i]
        files: \.(cpp|h|mm)$
```

This runs the system `clang-format` (must be on `PATH`) in-place on all staged `.cpp`, `.h`, and `.mm` files before the commit is recorded. If any file is reformatted, the commit is aborted and the reformatted files are left staged-and-modified — just `git add` them and commit again.

### Setup (first time / new machine)

```bash
brew install pre-commit          # macOS
pre-commit install               # installs the hook into .git/hooks/pre-commit
```

After `pre-commit install` the hook is active for all future commits in that clone. CI does not rely on the hook — it is purely a local convenience.

### Why this matters for agents

Sub-agents that write code and commit must ensure `clang-format` is satisfied before committing, otherwise the pre-commit hook aborts the commit. The safe pattern: always run `clang-format -i` on touched files as the last step before `git commit`, exactly as the CLAUDE.md validation expectations require.

---

## Parallel Agent Integration — Code Move + Modify Conflict

When running multiple agents in parallel via `isolation: "worktree"`, each agent gets its own copy of the repo under `/.claude/worktrees/`. A subtle but powerful integration scenario:

**The situation:** Two agents ran in parallel on a refactor batch. Agent A moved a block of code to a different module. Agent B independently modified that same code at its original location (pre-move). Neither agent knew what the other was doing.

**What the main agent did:** After reviewing all 7 agent outputs, it applied Agent A's move first, then — rather than rejecting Agent B's change as conflicting — recognised that B's modification needed to land in the *new* location and transplanted it there automatically.

**Why this works well:** The integration step is where the main agent earns its keep. With a broad view of all diffs, it can reason about intent rather than just applying patches mechanically. The worktree isolation means each sub-agent produces a clean, self-consistent diff that's easy to reason about independently.

**Takeaway:** Don't be afraid to assign overlapping or potentially conflicting work to parallel agents. The main agent can integrate intelligently as long as the individual outputs are coherent. The worst case is a manual merge step; the best case is fully automatic integration even across structural changes.
