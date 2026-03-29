# Bug: Raw callbacks_ pointer called from route worker thread without lifetime guarantee

**Severity**: MEDIUM
**File**: `libs/draxul-megacity/src/megacity_host.cpp:367`
**Source**: review-bugs-consensus.md (M4)

## Description

`route_worker_loop()` calls `callbacks_->request_frame()` at line 367 on the background `route_thread_`. `callbacks_` is a raw pointer stored in `MegaCityHost` and set externally. There is no lock and no lifetime check on this access.

If `MegaCityHost` is torn down while a route build is in flight (e.g., an exception, or if the host owner destroys the host before `route_thread_` has been joined), `callbacks_` may point to a freed object by the time `request_frame()` is called.

## Trigger Scenario

MegaCityHost destruction races with the route worker thread completing a build (e.g., rapid session teardown while analysis is running).

## Fix Strategy

- [ ] Change the `callbacks_` member type from `IHostCallbacks*` (or equivalent raw pointer) to `std::weak_ptr<IHostCallbacks>`
- [ ] In the route worker, lock the weak pointer before use:
  ```cpp
  if (auto cb = callbacks_.lock())
      cb->request_frame();
  ```
- [ ] Update the setter to store a `weak_ptr`; ensure the owner provides a `shared_ptr` lifetime
- [ ] Fix C1 (renderer_config_ data race) in the same session — see work item 48

## Acceptance Criteria

- [ ] Destroying `MegaCityHost` while a route build is in flight does not cause a use-after-free or crash
- [ ] Under TSan, no data race is reported on `callbacks_` during concurrent destruction and route completion
