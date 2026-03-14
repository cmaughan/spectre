#pragma once
#include <spectre/sdl_window.h>
#ifdef __APPLE__
// Metal renderer header is internal to spectre-renderer, forward declare
#else
// Vulkan renderer header is internal to spectre-renderer, forward declare
#endif
#include <memory>
#include <spectre/font.h>
#include <spectre/grid.h>
#include <spectre/nvim.h>
#include <spectre/renderer.h>
#include <unordered_map>
#include <vector>

namespace spectre
{

class App
{
public:
    bool initialize();
    void run();
    void shutdown();

private:
    struct FallbackFont
    {
        FontManager font;
        TextShaper shaper;
        std::string path;
    };

    void on_flush();
    void on_resize(int pixel_w, int pixel_h);
    void update_grid_to_renderer();
    void change_font_size(int new_size);
    void initialize_fallback_fonts();
    std::pair<FT_Face, TextShaper*> resolve_font_for_text(const std::string& text);

    SdlWindow window_;
    std::unique_ptr<IRenderer> renderer_;
    FontManager font_;
    GlyphCache glyph_cache_;
    TextShaper shaper_;
    Grid grid_;
    HighlightTable highlights_;
    NvimProcess nvim_process_;
    NvimRpc rpc_;
    UiEventHandler ui_events_;
    NvimInput input_;

    static constexpr int DEFAULT_FONT_SIZE = 11; // points (physical size)
    static constexpr int MIN_FONT_SIZE = 6;
    static constexpr int MAX_FONT_SIZE = 36;
    std::string font_path_;
    int font_size_ = DEFAULT_FONT_SIZE;
    float display_ppi_ = 96.0f;
    std::vector<FallbackFont> fallback_fonts_;
    std::unordered_map<std::string, int> font_choice_cache_;

    bool atlas_needs_full_upload_ = true;
    bool running_ = false;
    bool pending_window_activation_ = true;
    int grid_cols_ = 0, grid_rows_ = 0;
    int flush_count_ = 0;
};

} // namespace spectre
