1. **CRITICAL** — [libs/draxul-renderer/src/metal/metal_renderer.mm:1000](/Users/cmaughan/dev/Draxul/libs/draxul-renderer/src/metal/metal_renderer.mm#L1000)  
   **What goes wrong:** `flush_pending_atlas_uploads()` updates `atlas_staging_sizes_[slot]` immediately after `newBufferWithLength:`. If Metal returns `nil` under memory pressure, `atlas_staging_[slot]` stays null, `dst` becomes null at line 1008, and line 1014 does `memcpy(dst + offset, ...)`, which is a null-pointer write and crashes.  
   **Trigger:** A large glyph-atlas upload or low-memory condition on macOS during text rendering.  
   **Suggested fix:** Only publish the new size after the buffer allocation succeeds, and bail out before `memcpy` if the buffer or mapped pointer is null.
   ```cpp
   id<MTLBuffer> buf = [device_.get() newBufferWithLength:total_bytes options:MTLResourceStorageModeShared];
   if (!buf) { DRAXUL_LOG_ERROR(...); return; }
   atlas_staging_[slot].reset(buf);
   atlas_staging_sizes_[slot] = total_bytes;
   ```

2. **HIGH** — [libs/draxul-renderer/src/metal/metal_renderer.mm:110](/Users/cmaughan/dev/Draxul/libs/draxul-renderer/src/metal/metal_renderer.mm#L110)  
   **What goes wrong:** `MetalGridHandle::upload_state()` sets `buffer_sizes_[slot] = required_size` before verifying that `newBufferWithLength:` actually returned a buffer. If the allocation fails once, later frames stop retrying because the size check now passes, `current_buffer()` remains null, and the pane never renders again for that frame slot.  
   **Trigger:** Transient Metal shared-buffer allocation failure while resizing, opening a split, or uploading a larger grid.  
   **Suggested fix:** Update `buffer_sizes_[slot]` only after allocation succeeds; otherwise leave it unchanged or reset it to `0` so the next frame retries.

3. **HIGH** — [libs/draxul-renderer/src/metal/metal_renderer.mm:285](/Users/cmaughan/dev/Draxul/libs/draxul-renderer/src/metal/metal_renderer.mm#L285)  
   **What goes wrong:** `initialize()` does not validate several mandatory Metal objects before returning success: `command_queue_` at line 285, `atlas_texture_` at line 388, `atlas_sampler_` at line 399, and `frame_semaphore_` at line 403. Line 406 returns `true` even if any of them are null. That leaves the renderer "initialized" in a broken state, so later frames silently no-op or fail unpredictably instead of surfacing startup failure.  
   **Trigger:** Startup under resource pressure or device allocation failure on macOS.  
   **Suggested fix:** Check each created object and `return false` with an error log if any allocation returns null.
   ```cpp
   command_queue_.reset([device newCommandQueue]);
   if (!command_queue_) return false;
   ```