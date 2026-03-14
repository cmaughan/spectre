#include "support/test_support.h"

#include <filesystem>
#include <spectre/font.h>

using namespace spectre;
using namespace spectre::tests;

namespace
{

std::filesystem::path repo_root()
{
    auto here = std::filesystem::path(__FILE__).parent_path();
    return here.parent_path();
}

} // namespace

void run_font_tests()
{
    run_test("bundled nerd font shapes and rasterizes current lazy icon", []() {
        auto font_path = repo_root() / "fonts" / "JetBrainsMonoNerdFont-Regular.ttf";
        expect(std::filesystem::exists(font_path), "bundled font exists");

        FontManager font;
        expect(font.initialize(font_path.string(), 11, 96.0f), "font initializes");

        TextShaper shaper;
        shaper.initialize(font.hb_font());

        const std::string lazy_icon = "\xF3\xB0\x92\xB2"; // U+F04B2
        auto shaped = shaper.shape(lazy_icon);
        expect(!shaped.empty(), "lazy icon shapes");
        expect(shaped[0].glyph_id != 0, "lazy icon does not map to .notdef");

        GlyphCache cache;
        expect(cache.initialize(font.face(), font.point_size()), "glyph cache initializes");
        const auto& region = cache.get_cluster(lazy_icon, font.face(), shaper);
        expect(region.width > 0, "lazy icon rasterizes");
        expect(region.height > 0, "lazy icon has height");

        shaper.shutdown();
        font.shutdown();
    });
}
