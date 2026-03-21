#include "support/test_support.h"

#include <draxul/vulkan/vk_resource_helpers.h>

using namespace draxul;
using namespace draxul::tests;

void run_vk_resource_helpers_tests()
{
    run_test("vulkan buffer helper preserves the existing buffer on resize failure", []() {
        OwnedMappedBuffer<int, int> buffer;
        buffer.buffer = 11;
        buffer.allocation = 22;
        buffer.mapped = reinterpret_cast<void*>(0x1234);
        buffer.size = 64;

        int destroy_calls = 0;
        const auto result = ensure_buffer_size(buffer, 128, [&](size_t, OwnedMappedBuffer<int, int>&) { return false; }, [&](const OwnedMappedBuffer<int, int>&) { ++destroy_calls; });

        expect(result == BufferResizeResult::Failed, "failed resize reports failure");
        expect_eq(buffer.buffer, 11, "failed resize preserves buffer handle");
        expect_eq(buffer.allocation, 22, "failed resize preserves allocation handle");
        expect_eq(buffer.size, static_cast<size_t>(64), "failed resize preserves size");
        expect_eq(destroy_calls, 0, "failed resize does not destroy the old buffer");
    });

    run_test("vulkan buffer helper replaces the old buffer only after a successful allocation", []() {
        OwnedMappedBuffer<int, int> buffer;
        buffer.buffer = 11;
        buffer.allocation = 22;
        buffer.mapped = reinterpret_cast<void*>(0x1234);
        buffer.size = 64;

        OwnedMappedBuffer<int, int> destroyed;
        int destroy_calls = 0;
        const auto result = ensure_buffer_size(buffer, 128, [&](size_t requested_size, OwnedMappedBuffer<int, int>& replacement) {
                replacement.buffer = 33;
                replacement.allocation = 44;
                replacement.mapped = reinterpret_cast<void*>(0x5678);
                replacement.size = requested_size;
                return true; }, [&](const OwnedMappedBuffer<int, int>& existing) {
                destroyed = existing;
                ++destroy_calls; });

        expect(result == BufferResizeResult::Resized, "successful resize reports replacement");
        expect_eq(destroy_calls, 1, "successful resize destroys the previous buffer");
        expect_eq(destroyed.buffer, 11, "destroy callback receives the previous buffer");
        expect_eq(buffer.buffer, 33, "successful resize installs the new buffer");
        expect_eq(buffer.allocation, 44, "successful resize installs the new allocation");
        expect_eq(buffer.size, static_cast<size_t>(128), "successful resize updates the stored size");
    });

    run_test("full atlas uploads replace queued region uploads", []() {
        std::vector<PendingAtlasUpload> uploads;
        const uint8_t region[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
        const uint8_t full[16] = { 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25 };

        queue_atlas_region_upload(uploads, 4, 5, 2, 1, region);
        queue_full_atlas_upload(uploads, full, 2, 2);

        expect_eq(uploads.size(), static_cast<size_t>(1), "full upload discards earlier region uploads");
        expect(uploads.front().full_upload, "queued upload is marked as full");
        expect_eq(uploads.front().pixels.size(), static_cast<size_t>(16), "full upload copies the full payload");
        expect_eq(uploads.front().pixels.front(), static_cast<uint8_t>(10), "full upload copies the payload bytes");
        expect_eq(uploads.front().pixels.back(), static_cast<uint8_t>(25), "full upload keeps the full payload");
        expect_eq(pending_atlas_upload_size_bytes(uploads), static_cast<size_t>(16), "pending byte count tracks the queued full upload");
    });

    run_test("region atlas uploads patch a queued full upload", []() {
        std::vector<PendingAtlasUpload> uploads;
        const uint8_t full[16] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };
        const uint8_t region[4] = { 9, 8, 7, 6 };

        queue_full_atlas_upload(uploads, full, 2, 2);
        queue_atlas_region_upload(uploads, 1, 0, 1, 1, region);

        expect_eq(uploads.size(), static_cast<size_t>(1), "region uploads still keep a single queued full upload");
        expect(uploads.front().full_upload, "full upload remains queued");
        expect_eq(uploads.front().pixels[0], static_cast<uint8_t>(1), "patching a full upload preserves earlier pixels");
        expect_eq(uploads.front().pixels[4], static_cast<uint8_t>(9), "region upload patches the queued full upload");
        expect_eq(uploads.front().pixels[5], static_cast<uint8_t>(8), "patched payload keeps the new pixel data");
        expect_eq(uploads.front().pixels[6], static_cast<uint8_t>(7), "patched payload keeps the full RGBA texel");
        expect_eq(uploads.front().pixels[7], static_cast<uint8_t>(6), "patched payload keeps the full RGBA texel");
    });
}
