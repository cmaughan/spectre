# WI 118 — SplitTree O(n) Recursive Finds with `std::function` Allocations

**Type:** Bug / Performance  
**Severity:** Medium (latency degrades with many panes; compounds with other split operations)  
**Source:** Claude review  
**Authored by:** claude-sonnet-4-6

---

## Problem

`app/split_tree.cpp` implements `find_leaf_node()`, `find_parent_of()`, and `find_divider_node()` as recursive tree walks, each allocating a `std::function<>` closure on every call. Several operations chain multiple such calls, making common paths effectively O(n²) in pane count.

From Claude's review: "Every `find_leaf_node()`, `find_parent_of()`, and `find_divider_node()` call allocates a `std::function<>` and recursively walks the entire tree. Several operations chain multiple find calls making common paths O(n²). The `std::function` closure allocations are unnecessary given the small tree size."

While the tree is small in typical use, this degrades measurably when stress-testing splits (related to existing WI 12 `splittree-max-depth`), and the `std::function` allocations add pressure to the heap on every pane interaction.

Note: `app/split_tree.cpp` has an unstaged modification in the current working tree — check if this overlaps with in-progress work before starting.

---

## Investigation Steps

- [ ] Check current git status on `app/split_tree.cpp` — is there in-progress work?
- [ ] Profile `find_leaf_node`, `find_parent_of`, `find_divider_node` — confirm `std::function` heap allocation per call
- [ ] Enumerate callers — which operations chain multiple finds?
- [ ] Measure allocation count for a 10-pane split with a flame graph or ASan allocation tracing

---

## Fix Strategy

1. Replace `std::function<>` predicate callbacks with templated lambda parameters or function-pointer overloads. Eliminates heap allocation entirely.
2. For chained finds (e.g. find parent then use to find sibling), combine into a single tree pass that returns both results.
3. Consider caching parent pointers in each node if the tree is frequently queried.

```cpp
// Before
template<typename Fn>
Node* find_leaf_node(std::function<bool(const LeafNode&)> pred);

// After
template<typename Pred>
Node* find_leaf_node(Pred&& pred);
```

---

## Acceptance Criteria

- [ ] `find_leaf_node`, `find_parent_of`, `find_divider_node` take templated callable, not `std::function`.
- [ ] No chained double-traversal for operations that can be combined.
- [ ] Existing split tree tests pass.
- [ ] No regression in `splittree-max-depth` or `splittree-oob-placement` test scenarios.

---

## Interdependencies

- **WI 29** (pane-drag-reorder) and **WI 44** (pane-zoom) both traverse the split tree frequently — this fix benefits them.
- Check against any in-progress `split_tree.cpp` changes in the working tree.
