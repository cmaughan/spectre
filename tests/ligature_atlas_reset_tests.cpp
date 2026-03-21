#include "support/test_support.h"

#include <draxul/text_service.h>

#include <filesystem>
#include <string>

using namespace draxul;
using namespace draxul::tests;

namespace
{

std::filesystem::path repo_root()
{
    auto here = std::filesystem::path(__FILE__).parent_path();
    return here.parent_path();
}

std::filesystem::path default_font_path()
{
    return repo_root() / "fonts" / "CascadiaCode-Regular.ttf";
}

bool font_exists()
{
    return std::filesystem::exists(default_font_path());
}

// Initialize a TextService with the test font. Returns false if font not found.
bool init_text_service(TextService& ts, float point_size = TextService::DEFAULT_POINT_SIZE)
{
    if (!font_exists())
        return false;
    TextServiceConfig cfg;
    cfg.font_path = default_font_path().string();
    cfg.enable_ligatures = true;
    return ts.initialize(cfg, point_size, 96.0f);
}

} // namespace

void run_ligature_atlas_reset_tests()
{
    run_test("ligature atlas reset: single-cell glyph survives re-cache after reset", []() {
        if (!font_exists())
            skip("test font not found");

        TextService ts;
        expect(init_text_service(ts), "TextService must initialize");

        // Cache a single-cell ASCII glyph.
        AtlasRegion before = ts.resolve_cluster("A");
        expect(before.width > 0 || before.height > 0, "glyph 'A' must rasterize to a non-empty region");

        // Trigger an atlas reset by changing the font size back and forth.
        const float old_size = ts.point_size();
        const int new_size = old_size + 1;
        expect(ts.set_point_size(new_size), "point size change must succeed");
        expect(ts.consume_atlas_reset(), "atlas reset must be pending after point size change");

        // Re-query the same glyph — should rasterise at the new size.
        AtlasRegion after = ts.resolve_cluster("A");
        expect(after.width > 0 || after.height > 0, "glyph 'A' re-rasterises after atlas reset");

        // Reset back to original size.
        ts.set_point_size(old_size);
    });

    run_test("ligature atlas reset: atlas reset count increments on font size change", []() {
        if (!font_exists())
            skip("test font not found");

        TextService ts;
        expect(init_text_service(ts), "TextService must initialize");

        const int count_before = ts.atlas_reset_count();

        // Change point size twice — each change should reset the atlas.
        ts.set_point_size(ts.point_size() + 1);
        ts.set_point_size(ts.point_size() - 1);

        const int count_after = ts.atlas_reset_count();
        expect(count_after >= count_before + 2, "atlas reset count must increment on font size changes");
    });

    run_test("ligature atlas reset: atlas dirty flag set after re-caching glyphs post-reset", []() {
        if (!font_exists())
            skip("test font not found");

        TextService ts;
        expect(init_text_service(ts), "TextService must initialize");

        // Cache some glyphs and clear the dirty flag.
        ts.resolve_cluster("H");
        ts.resolve_cluster("e");
        ts.resolve_cluster("l");
        ts.clear_atlas_dirty();
        expect(!ts.atlas_dirty(), "atlas dirty flag should be clear after clearing");

        // Trigger atlas reset via font size change.
        ts.set_point_size(ts.point_size() + 1);

        // Re-caching any glyph after the reset should mark the atlas dirty.
        ts.resolve_cluster("H");
        expect(ts.atlas_dirty(), "atlas must be marked dirty after re-rasterising a glyph post-reset");

        ts.set_point_size(ts.point_size() - 1);
    });

    run_test("ligature atlas reset: multi-cluster ligature re-caches cleanly after reset", []() {
        if (!font_exists())
            skip("test font not found");

        TextService ts;
        expect(init_text_service(ts), "TextService must initialize");

        // Resolve a two-codepoint cluster that CascadiaCode may ligate (e.g. "fi").
        AtlasRegion before = ts.resolve_cluster("fi");
        // Whether it is a ligature or two separate glyphs does not matter;
        // we only care that the region is valid and re-query works post reset.

        // Trigger reset.
        const float old_size = ts.point_size();
        ts.set_point_size(old_size + 2);
        ts.consume_atlas_reset();

        AtlasRegion after = ts.resolve_cluster("fi");
        // Post-reset region should be renderable (non-zero or equal-zero for
        // whitespace/missing glyph — both are safe).
        // The important assertion: no crash and the returned region coords are
        // within the atlas bounds.
        expect(after.u0 >= 0.0f, "fi re-cache: u0 >= 0");
        expect(after.v0 >= 0.0f, "fi re-cache: v0 >= 0");

        ts.set_point_size(old_size);
    });

    run_test("ligature atlas reset: glyph count drops to zero after reset then re-populates", []() {
        if (!font_exists())
            skip("test font not found");

        TextService ts;
        expect(init_text_service(ts), "TextService must initialize");

        // Cache a few glyphs.
        for (const char* ch : { "A", "B", "C", "D", "E" })
            ts.resolve_cluster(ch);

        const size_t count_before = ts.atlas_glyph_count();
        expect(count_before >= 5, "glyph count should be at least 5 after caching 5 glyphs");

        // Reset via font size change.
        const float old_size = ts.point_size();
        ts.set_point_size(old_size + 1);

        // After reset, glyph count drops to zero before any new caching.
        const size_t count_after_reset = ts.atlas_glyph_count();
        expect_eq(count_after_reset, static_cast<size_t>(0), "glyph count is 0 immediately after atlas reset");

        // Re-cache a glyph — count should rise again.
        ts.resolve_cluster("A");
        expect(ts.atlas_glyph_count() >= 1, "glyph count rises again after re-caching post-reset");

        ts.set_point_size(old_size);
    });

    run_test("ligature atlas reset: font size min boundary is enforced gracefully", []() {
        if (!font_exists())
            skip("test font not found");

        TextService ts;
        expect(init_text_service(ts), "TextService must initialize");

        // Attempt to set size below minimum — must not crash.
        bool result = ts.set_point_size(TextService::MIN_POINT_SIZE - 10);
        // Either succeeds clamped or returns false; neither outcome should crash.
        expect(ts.point_size() >= TextService::MIN_POINT_SIZE,
            "point size must not drop below MIN_POINT_SIZE");
        (void)result;
    });

    run_test("ligature atlas reset: atlas usage ratio is 0 immediately after reset", []() {
        if (!font_exists())
            skip("test font not found");

        TextService ts;
        expect(init_text_service(ts), "TextService must initialize");

        // Fill with a few glyphs.
        for (const char* ch : { "X", "Y", "Z" })
            ts.resolve_cluster(ch);

        const float usage_before = ts.atlas_usage_ratio();
        expect(usage_before > 0.0f, "usage ratio is positive after caching glyphs");

        // Reset.
        const float old_size = ts.point_size();
        ts.set_point_size(old_size + 1);

        const float usage_after_reset = ts.atlas_usage_ratio();
        expect(usage_after_reset == 0.0f, "usage ratio is 0.0 immediately after atlas reset");

        ts.set_point_size(old_size);
    });
}
