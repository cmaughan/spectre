# Rendering Efficiency Notes

Last updated: 2026-03-24

## External References

- Arseny Kapoulkine, [Writing an efficient Vulkan renderer](https://zeux.io/2020/02/27/writing-an-efficient-vulkan-renderer/)

## Working Principles

- Prefer persistent mapping for dynamic CPU-written GPU data.
- Prefer per-frame transient arenas and suballocation over many small dynamic buffer allocations.
- Keep dynamic vertex/index/uniform data in host-visible memory chosen for frequent CPU writes.
- Keep static geometry and textures on the "upload once, reuse many times" path.
- Treat fence waits, upload flushes, and queue submissions as first-class performance costs that must be measured.

## Current Draxul Direction

- MegaCity floor-grid geometry now uses transient streamed slices instead of creating a fresh GPU mesh object whenever the camera changes the visible footprint.
- Vulkan MegaCity uses a per-frame transient geometry arena with suballocated vertex/index slices.
- Metal MegaCity mirrors the same arena idea, but the backend is still effectively single-frame-in-flight today, so its arena is reset once per frame instead of rotated across multiple live frames.

## Follow-Up Targets

- Promote the MegaCity-only transient arena pattern into a shared renderer utility once the shape settles.
- Instrument Vulkan fence wait time in `begin_frame()`.
- Instrument atlas upload stalls, especially the path that waits on other in-flight frame fences before recording uploads.
- Audit VMA allocation choices for dynamic buffers on target GPUs so "AUTO" is confirmed instead of assumed.
- Move larger static scene geometry to device-local buffers via staging if MegaCity grows beyond tiny demo meshes.
- Revisit Metal frames-in-flight so dynamic data can use real rotating buffer slots instead of a single reset-per-frame arena.

## Current Suspects For Sticky Motion

- Blocking waits at the start of the Vulkan frame loop.
- Atlas upload synchronization that can wait for multiple in-flight frames.
- Any remaining event-loop coalescing that causes input bursts to collapse into fewer presented frames.
