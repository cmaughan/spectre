#include "app.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <vector>

namespace spectre
{

namespace
{

std::vector<std::string> fallback_font_candidates()
{
#ifdef _WIN32
    const char* windir = std::getenv("WINDIR");
    std::string windows_dir = windir ? windir : "C:\\Windows";
    return {
        windows_dir + "\\Fonts\\seguisym.ttf",
        windows_dir + "\\Fonts\\seguiemj.ttf",
    };
#elif defined(__APPLE__)
    return {
        "/System/Library/Fonts/Apple Color Emoji.ttc",
        "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
    };
#else
    return {
        "/usr/share/fonts/truetype/noto/NotoColorEmoji.ttf",
        "/usr/share/fonts/truetype/noto/NotoEmoji-Regular.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    };
#endif
}

std::vector<std::string> primary_font_candidates()
{
#ifdef _WIN32
    const char* windir = std::getenv("WINDIR");
    const char* local_app_data = std::getenv("LOCALAPPDATA");
    std::string windows_dir = windir ? windir : "C:\\Windows";
    std::string local_fonts = local_app_data
        ? (std::string(local_app_data) + "\\Microsoft\\Windows\\Fonts\\")
        : "";

    std::vector<std::string> candidates;
    if (!local_fonts.empty())
    {
        candidates.push_back(local_fonts + "JetBrainsMonoNerdFontMono-Regular.ttf");
        candidates.push_back(local_fonts + "JetBrainsMonoNerdFont-Regular.ttf");
    }
    candidates.push_back(windows_dir + "\\Fonts\\JetBrainsMonoNerdFontMono-Regular.ttf");
    candidates.push_back(windows_dir + "\\Fonts\\JetBrainsMonoNerdFont-Regular.ttf");
    candidates.push_back(windows_dir + "\\Fonts\\JetBrains Mono Regular Nerd Font Complete Mono Windows Compatible.ttf");
    candidates.push_back(windows_dir + "\\Fonts\\JetBrains Mono Regular Nerd Font Complete Windows Compatible.ttf");
    candidates.push_back("fonts/JetBrainsMonoNerdFont-Regular.ttf");
    return candidates;
#else
    return { "fonts/JetBrainsMonoNerdFont-Regular.ttf" };
#endif
}

std::string resolve_primary_font_path()
{
    for (const auto& path : primary_font_candidates())
    {
        if (std::filesystem::exists(path))
            return path;
    }

    return "fonts/JetBrainsMonoNerdFont-Regular.ttf";
}

bool can_render_cluster(FT_Face face, TextShaper& shaper, const std::string& text)
{
    if (!face)
        return false;

    auto shaped = shaper.shape(text);
    if (shaped.empty())
        return false;

    bool has_glyph = false;
    for (const auto& glyph : shaped)
    {
        if (glyph.glyph_id == 0)
            return false;
        if (FT_Load_Glyph(face, glyph.glyph_id, FT_LOAD_DEFAULT))
            return false;
        has_glyph = true;
    }

    return has_glyph;
}

} // namespace

bool App::initialize()
{
    fprintf(stderr, "[spectre] Initializing...\n");

    // 1. Create window
    if (!window_.initialize("Spectre", 1280, 800))
    {
        fprintf(stderr, "[spectre] Failed to create window\n");
        return false;
    }
    fprintf(stderr, "[spectre] Window created\n");

    // 2. Init renderer
    renderer_ = create_renderer();
    if (!renderer_ || !renderer_->initialize(window_))
    {
        fprintf(stderr, "[spectre] Failed to init renderer\n");
        return false;
    }
    fprintf(stderr, "[spectre] Renderer initialized\n");

    // 3. Load font
    display_ppi_ = window_.display_ppi();
    fprintf(stderr, "[spectre] Display PPI: %.0f\n", display_ppi_);
    font_path_ = resolve_primary_font_path();
    fprintf(stderr, "[spectre] Primary font path: %s\n", font_path_.c_str());
    if (!font_.initialize(font_path_, font_size_, display_ppi_))
    {
        fprintf(stderr, "[spectre] Failed to load font\n");
        return false;
    }
    fprintf(stderr, "[spectre] Font loaded\n");

    // Set cell size from font metrics
    auto& metrics = font_.metrics();
    renderer_->set_cell_size(metrics.cell_width, metrics.cell_height);
    renderer_->set_ascender(metrics.ascender);

    // Init glyph cache and shaper
    glyph_cache_.initialize(font_.face(), font_.point_size());
    shaper_.initialize(font_.hb_font());
    initialize_fallback_fonts();

    // 4. Calculate grid dimensions
    auto [pw, ph] = window_.size_pixels();
    int pad = renderer_->padding();
    grid_cols_ = (pw - pad * 2) / metrics.cell_width;
    grid_rows_ = (ph - pad * 2) / metrics.cell_height;
    if (grid_cols_ < 1)
        grid_cols_ = 80;
    if (grid_rows_ < 1)
        grid_rows_ = 24;

    grid_.resize(grid_cols_, grid_rows_);
    renderer_->set_grid_size(grid_cols_, grid_rows_);

    // Upload initial (empty) atlas
    renderer_->set_atlas_texture(glyph_cache_.atlas_data(),
        glyph_cache_.atlas_width(), glyph_cache_.atlas_height());
    atlas_needs_full_upload_ = false;

    // 5. Spawn nvim
    if (!nvim_process_.spawn())
    {
        fprintf(stderr, "Failed to spawn nvim\n");
        return false;
    }

    // 6. Init RPC
    if (!rpc_.initialize(nvim_process_))
    {
        fprintf(stderr, "Failed to init RPC\n");
        return false;
    }

    // 7. Setup UI event handler
    ui_events_.set_grid(&grid_);
    ui_events_.set_highlights(&highlights_);
    ui_events_.on_flush = [this]() { on_flush(); };
    ui_events_.on_grid_resize = [this](int cols, int rows) {
        grid_cols_ = cols;
        grid_rows_ = rows;
        renderer_->set_grid_size(cols, rows);
    };

    // 8. Setup input handler
    input_.initialize(&rpc_, metrics.cell_width, metrics.cell_height);

    // 9. Wire window events
    window_.on_key = [this](const KeyEvent& e) {
        if (e.pressed && (e.mod & SDL_KMOD_CTRL))
        {
            if (e.keycode == SDLK_EQUALS || e.keycode == SDLK_PLUS)
            {
                change_font_size(font_size_ + 1);
                return;
            }
            else if (e.keycode == SDLK_MINUS)
            {
                change_font_size(font_size_ - 1);
                return;
            }
            else if (e.keycode == SDLK_0)
            {
                change_font_size(DEFAULT_FONT_SIZE);
                return;
            }
        }
        input_.on_key(e);
    };
    window_.on_text_input = [this](const TextInputEvent& e) { input_.on_text_input(e); };
    window_.on_mouse_button = [this](const MouseButtonEvent& e) { input_.on_mouse_button(e); };
    window_.on_mouse_move = [this](const MouseMoveEvent& e) { input_.on_mouse_move(e); };
    window_.on_mouse_wheel = [this](const MouseWheelEvent& e) { input_.on_mouse_wheel(e); };
    window_.on_resize = [this](const WindowResizeEvent& e) { on_resize(e.width, e.height); };

    // 10. Attach UI
    rpc_.request("nvim_ui_attach", { NvimRpc::make_int(grid_cols_), NvimRpc::make_int(grid_rows_), NvimRpc::make_map({
                                                                                                       { NvimRpc::make_str("rgb"), NvimRpc::make_bool(true) },
                                                                                                       { NvimRpc::make_str("ext_linegrid"), NvimRpc::make_bool(true) },
                                                                                                   }) });

    fprintf(stderr, "[spectre] UI attached: %dx%d\n", grid_cols_, grid_rows_);
    running_ = true;
    return true;
}

void App::run()
{
    while (running_)
    {
        if (pending_window_activation_)
        {
            window_.activate();
            pending_window_activation_ = false;
        }

        if (!window_.poll_events())
        {
            rpc_.notify("nvim_input", { NvimRpc::make_str("<C-\\><C-n>:qa!<CR>") });
            running_ = false;
            break;
        }

        if (!nvim_process_.is_running())
        {
            running_ = false;
            break;
        }

        auto notifications = rpc_.drain_notifications();
        for (auto& notif : notifications)
        {
            if (notif.method == "redraw")
            {
                ui_events_.process_redraw(notif.params);
            }
        }

        if (renderer_->begin_frame())
        {
            renderer_->end_frame();
        }
    }
}

void App::on_flush()
{
    flush_count_++;
    update_grid_to_renderer();

    CursorStyle cursor_style;
    cursor_style.bg = highlights_.default_fg();
    cursor_style.fg = highlights_.default_bg();

    int mode = ui_events_.current_mode();
    if (mode >= 0 && mode < (int)ui_events_.modes().size())
    {
        const auto& mode_info = ui_events_.modes()[mode];
        cursor_style.shape = mode_info.cursor_shape;
        cursor_style.cell_percentage = mode_info.cell_percentage;
        if (mode_info.attr_id != 0)
        {
            Color fg;
            Color bg;
            highlights_.resolve(highlights_.get((uint16_t)mode_info.attr_id), fg, bg);
            cursor_style.fg = fg;
            cursor_style.bg = bg;
            cursor_style.use_explicit_colors = true;
        }
    }
    renderer_->set_cursor(ui_events_.cursor_col(), ui_events_.cursor_row(), cursor_style);
}

void App::update_grid_to_renderer()
{
    auto dirty = grid_.get_dirty_cells();
    if (dirty.empty())
        return;

    std::vector<CellUpdate> updates;
    updates.reserve(dirty.size());

    bool atlas_updated = false;

    for (auto& [col, row] : dirty)
    {
        const auto& cell = grid_.get_cell(col, row);
        const auto& hl = highlights_.get(cell.hl_attr_id);

        Color fg, bg;
        highlights_.resolve(hl, fg, bg);

        CellUpdate update;
        update.col = col;
        update.row = row;
        update.bg = bg;
        update.fg = fg;
        update.style_flags = hl.style_flags();

        if (!cell.double_width_cont && !cell.text.empty() && cell.text != " ")
        {
            auto [face, text_shaper] = resolve_font_for_text(cell.text);
            update.glyph = glyph_cache_.get_cluster(cell.text, face, *text_shaper);
            if (glyph_cache_.atlas_dirty())
            {
                atlas_updated = true;
            }
        }
        updates.push_back(update);
    }

    if (atlas_updated || atlas_needs_full_upload_)
    {
        renderer_->set_atlas_texture(glyph_cache_.atlas_data(),
            glyph_cache_.atlas_width(), glyph_cache_.atlas_height());
        glyph_cache_.clear_dirty();
        atlas_needs_full_upload_ = false;
    }

    renderer_->update_cells(updates);
    grid_.clear_dirty();
}

void App::on_resize(int pixel_w, int pixel_h)
{
    renderer_->resize(pixel_w, pixel_h);

    auto [cell_w, cell_h] = renderer_->cell_size_pixels();
    int pad = renderer_->padding();
    int new_cols = (pixel_w - pad * 2) / cell_w;
    int new_rows = (pixel_h - pad * 2) / cell_h;

    if (new_cols < 1)
        new_cols = 1;
    if (new_rows < 1)
        new_rows = 1;

    if (new_cols != grid_cols_ || new_rows != grid_rows_)
    {
        rpc_.request("nvim_ui_try_resize", { NvimRpc::make_int(new_cols), NvimRpc::make_int(new_rows) });
    }
}

void App::change_font_size(int new_size)
{
    new_size = std::max(MIN_FONT_SIZE, std::min(MAX_FONT_SIZE, new_size));
    if (new_size == font_size_)
        return;
    font_size_ = new_size;

    font_.set_point_size(font_size_);
    auto& metrics = font_.metrics();

    glyph_cache_.reset(font_.face(), font_.point_size());
    shaper_.initialize(font_.hb_font());
    font_choice_cache_.clear();

    for (auto& fallback : fallback_fonts_)
    {
        fallback.font.set_point_size(font_size_);
        fallback.shaper.initialize(fallback.font.hb_font());
    }

    renderer_->set_cell_size(metrics.cell_width, metrics.cell_height);
    renderer_->set_ascender(metrics.ascender);
    renderer_->set_grid_size(grid_cols_, grid_rows_);
    input_.set_cell_size(metrics.cell_width, metrics.cell_height);
    grid_.mark_all_dirty();

    auto [pw, ph] = window_.size_pixels();
    int pad = renderer_->padding();
    int new_cols = (pw - pad * 2) / metrics.cell_width;
    int new_rows = (ph - pad * 2) / metrics.cell_height;
    if (new_cols < 1)
        new_cols = 1;
    if (new_rows < 1)
        new_rows = 1;

    atlas_needs_full_upload_ = true;
    update_grid_to_renderer();

    if (new_cols != grid_cols_ || new_rows != grid_rows_)
    {
        rpc_.request("nvim_ui_try_resize", { NvimRpc::make_int(new_cols), NvimRpc::make_int(new_rows) });
    }
}

void App::shutdown()
{
    if (nvim_process_.is_running())
    {
        rpc_.notify("nvim_input", { NvimRpc::make_str("<C-\\><C-n>:qa!<CR>") });
    }
    nvim_process_.shutdown();
    rpc_.shutdown();

    shaper_.shutdown();
    for (auto& fallback : fallback_fonts_)
    {
        fallback.shaper.shutdown();
        fallback.font.shutdown();
    }
    fallback_fonts_.clear();
    font_choice_cache_.clear();
    font_.shutdown();
    if (renderer_)
        renderer_->shutdown();
    window_.shutdown();
}

void App::initialize_fallback_fonts()
{
    fallback_fonts_.clear();
    font_choice_cache_.clear();

    auto candidates = fallback_font_candidates();
    fallback_fonts_.reserve(candidates.size());

    for (const auto& path : candidates)
    {
        if (path == font_path_ || !std::filesystem::exists(path))
            continue;

        fallback_fonts_.emplace_back();
        auto& fallback = fallback_fonts_.back();
        fallback.path = path;
        if (!fallback.font.initialize(path, font_size_, display_ppi_))
        {
            fallback.font.shutdown();
            fallback_fonts_.pop_back();
            continue;
        }

        fallback.shaper.initialize(fallback.font.hb_font());
        fprintf(stderr, "[spectre] Fallback font loaded: %s\n", path.c_str());
    }
}

std::pair<FT_Face, TextShaper*> App::resolve_font_for_text(const std::string& text)
{
    auto cached = font_choice_cache_.find(text);
    if (cached != font_choice_cache_.end())
    {
        if (cached->second < 0)
            return { font_.face(), &shaper_ };

        int idx = cached->second;
        if (idx >= 0 && idx < (int)fallback_fonts_.size())
            return { fallback_fonts_[idx].font.face(), &fallback_fonts_[idx].shaper };
    }

    if (can_render_cluster(font_.face(), shaper_, text))
    {
        font_choice_cache_[text] = -1;
        return { font_.face(), &shaper_ };
    }

    for (int i = 0; i < (int)fallback_fonts_.size(); i++)
    {
        if (can_render_cluster(fallback_fonts_[i].font.face(), fallback_fonts_[i].shaper, text))
        {
            font_choice_cache_[text] = i;
            return { fallback_fonts_[i].font.face(), &fallback_fonts_[i].shaper };
        }
    }

    font_choice_cache_[text] = -1;
    return { font_.face(), &shaper_ };
}

} // namespace spectre
