#include "chrome_host.h"

#include "host_manager.h"
#include "workspace.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <draxul/app_config.h>
#include <draxul/log.h>
#include <draxul/text_service.h>
#include <draxul/unicode.h>
#include <nanovg.h>
#include <string_view>
#include <unordered_set>

namespace draxul
{

ChromeHost::ChromeHost(Deps deps)
    : deps_(std::move(deps))
{
}

bool ChromeHost::initialize(const HostContext& context, IHostCallbacks& /*callbacks*/)
{
    viewport_ = context.initial_viewport;
    nanovg_pass_ = create_nanovg_pass();
    running_ = nanovg_pass_ != nullptr;
    return running_;
}

void ChromeHost::shutdown()
{
    tab_handle_.reset();
    nanovg_pass_.reset();
    running_ = false;
}

bool ChromeHost::is_running() const
{
    return running_;
}

void ChromeHost::set_viewport(const HostViewport& viewport)
{
    viewport_ = viewport;
}

const SplitTree& ChromeHost::active_tree() const
{
    if (!deps_.workspaces || !deps_.active_workspace_id)
    {
        static const SplitTree dummy;
        return dummy;
    }
    for (const auto& ws : *deps_.workspaces)
    {
        if (ws->id == *deps_.active_workspace_id)
            return ws->host_manager.tree();
    }
    static const SplitTree dummy;
    return dummy;
}

int ChromeHost::tab_bar_height() const
{
    if (!deps_.grid_renderer)
        return 0;
    const auto [cw, ch] = deps_.grid_renderer->cell_size_pixels();
    return ch + 2; // extra gap below tab buttons
}

// ---------------------------------------------------------------------------
// Tab bar: Catppuccin Mocha palette
// ---------------------------------------------------------------------------

namespace
{
// Catppuccin Mocha palette
constexpr Color kTabBarBg{ 0.094f, 0.094f, 0.145f, 1.0f }; // #181825 Mantle
constexpr Color kActiveTabBg{ 0.796f, 0.651f, 0.969f, 1.0f }; // #cba6f7 Mauve
constexpr Color kInactiveTabBg{ 0.271f, 0.278f, 0.353f, 1.0f }; // #45475a Surface1
constexpr Color kActiveTabFg{ 0.118f, 0.118f, 0.180f, 1.0f }; // #1e1e2e Base (dark on bright)
constexpr Color kInactiveTabFg{ 0.651f, 0.678f, 0.780f, 1.0f }; // #a6adc8 Subtext0
constexpr Color kResourcePillBg{ 0.976f, 0.886f, 0.686f, 1.0f }; // #f9e2af Yellow
constexpr Color kResourcePillFg{ 0.118f, 0.118f, 0.180f, 1.0f }; // #1e1e2e Base
constexpr Color kResourcePillLabelFg{ 0.118f, 0.118f, 0.180f, 0.68f }; // muted label tint
constexpr Color kResourcePillWarnBg{ 0.973f, 0.761f, 0.510f, 1.0f }; // orange
constexpr Color kResourcePillHotBg{ 0.957f, 0.337f, 0.337f, 1.0f }; // red
constexpr Color kChordPillBg{ 0.271f, 0.278f, 0.353f, 0.95f }; // inactive-tab grey
constexpr Color kChordPillFg{ 0.808f, 0.839f, 0.957f, 1.0f }; // lighter text for quick scan

constexpr int kTabPadCols = 1; // padding cells on each side of tab label
// Pane status pill margin from the right edge of the pane (in cells).
constexpr int kPaneStatusRightMarginCols = 1;

// Pane status accent: Catppuccin-tinted green, same brightness/alpha as the
// burgundy used for active workspace tabs so the two pill kinds visually rhyme
// without being confusable.
constexpr unsigned char kPaneAccentR = 60;
constexpr unsigned char kPaneAccentG = 165;
constexpr unsigned char kPaneAccentB = 95;
constexpr unsigned char kPaneAccentA = 220;
constexpr unsigned char kInactiveAccentR = 110;
constexpr unsigned char kInactiveAccentG = 115;
constexpr unsigned char kInactiveAccentB = 140;
constexpr unsigned char kInactiveAccentA = 200;
constexpr int kGridPadding = 4; // renderer internal cell padding

std::string tab_label(size_t index, const std::string& name)
{
    return std::to_string(index + 1) + ": " + name;
}

std::vector<std::string> split_display_clusters(std::string_view text)
{
    std::vector<std::string> clusters;
    size_t offset = 0;
    while (offset < text.size())
    {
        const size_t cluster_begin = offset;
        uint32_t cp = 0;
        if (!utf8_decode_next(text, offset, cp))
            break;

        bool previous_was_zwj = (cp == 0x200D);
        while (offset < text.size())
        {
            size_t next = offset;
            uint32_t next_cp = 0;
            if (!utf8_decode_next(text, next, next_cp))
                break;

            const bool joins_cluster = previous_was_zwj || next_cp == 0x200D || is_width_ignorable(next_cp)
                || is_emoji_modifier(next_cp);
            if (!joins_cluster)
                break;

            previous_was_zwj = (next_cp == 0x200D);
            offset = next;
        }

        clusters.emplace_back(text.substr(cluster_begin, offset - cluster_begin));
    }
    return clusters;
}

void append_label_clusters(std::vector<ChromeHost::LabelCluster>& out, std::string_view text, const Color& fg)
{
    for (const auto& cluster_text : split_display_clusters(text))
    {
        ChromeHost::LabelCluster cluster;
        cluster.text = cluster_text;
        cluster.width = std::max(1, cluster_cell_width(cluster_text));
        cluster.fg = fg;
        out.push_back(std::move(cluster));
    }
}

int label_cluster_columns(std::span<const ChromeHost::LabelCluster> clusters)
{
    int total = 0;
    for (const auto& cluster : clusters)
        total += std::max(1, cluster.width);
    return total;
}

Color apply_alpha(Color color, float alpha)
{
    color.a *= std::clamp(alpha, 0.0f, 1.0f);
    return color;
}

std::string format_resource_percent_value(int percent)
{
    char buffer[16];
    if (percent < 0)
        std::snprintf(buffer, sizeof(buffer), "--%%");
    else
        std::snprintf(buffer, sizeof(buffer), "%3d%%", percent);
    return std::string(buffer);
}

Color resource_pill_background_color(const SystemResourceSnapshot& snapshot)
{
    if (snapshot.cpu_percent >= 100)
        return kResourcePillHotBg;
    if (snapshot.cpu_percent >= 90 || snapshot.memory_percent >= 90)
        return kResourcePillWarnBg;
    return kResourcePillBg;
}

void append_resource_metric(std::vector<ChromeHost::LabelCluster>& out, const char* label, int percent,
    bool append_separator)
{
    append_label_clusters(out, label, kResourcePillLabelFg);
    append_label_clusters(out, " ", kResourcePillLabelFg);
    append_label_clusters(out, format_resource_percent_value(percent), kResourcePillFg);
    if (append_separator)
        append_label_clusters(out, "  ", kResourcePillFg);
}
} // namespace

int ChromeHost::hit_test_tab(int px, int py) const
{
    if (!deps_.workspaces || deps_.workspaces->empty() || !deps_.grid_renderer)
        return 0;
    const auto& workspaces = *deps_.workspaces;
    const auto [cw, ch] = deps_.grid_renderer->cell_size_pixels();
    if (cw <= 0 || ch <= 0 || py < 0 || py >= ch)
        return 0;

    int col_cursor = 0;
    for (size_t wi = 0; wi < workspaces.size(); ++wi)
    {
        const std::string label = tab_label(wi, workspaces[wi]->name);
        const int total_cols = static_cast<int>(label.size()) + kTabPadCols * 2;
        const int tab_left = col_cursor * cw + kGridPadding;
        const int tab_right = (col_cursor + total_cols) * cw + kGridPadding;
        if (px >= tab_left && px < tab_right)
            return static_cast<int>(wi) + 1; // 1-based
        col_cursor += total_cols;
    }
    return 0;
}

void ChromeHost::draw(IFrameContext& frame)
{
    if (!nanovg_pass_)
        return;

    static const std::vector<std::unique_ptr<Workspace>> kEmpty;
    const auto& workspaces = deps_.workspaces ? *deps_.workspaces : kEmpty;
    const int active_ws_id = deps_.active_workspace_id ? *deps_.active_workspace_id : -1;
    const bool show_tabs = !workspaces.empty();
    const bool show_resource_pill = deps_.system_resource_snapshot && deps_.system_resource_snapshot->available();
    const bool show_top_bar = deps_.grid_renderer && (show_tabs || show_resource_pill);
    const bool show_dividers = active_tree().leaf_count() >= 2;
    const bool show_status = deps_.config && deps_.config->show_pane_status;

    // Collect per-pane status entries (WI 78). One row of cells (cell_h tall)
    // is reserved at the bottom of every pane by App::viewport_from_descriptor.
    std::vector<PaneStatusEntry> status_entries;
    int status_strip_h = 0;
    if (show_status && deps_.grid_renderer)
    {
        const auto [cw, ch] = deps_.grid_renderer->cell_size_pixels();
        status_strip_h = ch;
        if (cw > 0 && ch > 0)
        {
            // Walk the active workspace's hosts so we can pull status_text() per pane.
            const HostManager* active_hm = nullptr;
            for (const auto& ws : workspaces)
            {
                if (ws->id == active_ws_id)
                {
                    active_hm = &ws->host_manager;
                    break;
                }
            }
            if (active_hm)
            {
                const SplitTree& tree = active_hm->tree();
                const LeafId focused_leaf = tree.focused();
                int next_index = 1;
                tree.for_each_leaf([&](LeafId id, const PaneDescriptor& desc) {
                    IHost* h = active_hm->host_for(id);
                    if (!h || desc.pixel_size.x <= 0 || desc.pixel_size.y <= ch)
                        return;
                    PaneStatusEntry e;
                    e.pane_x = desc.pixel_pos.x;
                    e.pane_y = desc.pixel_pos.y;
                    e.pane_w = desc.pixel_size.x;
                    e.pane_h = desc.pixel_size.y;
                    e.index = next_index++;
                    e.text = h->status_text();
                    e.focused = (id == focused_leaf);
                    e.leaf = id;
                    status_entries.push_back(std::move(e));
                });
            }
        }
    }

    if (!show_top_bar && !show_dividers && status_entries.empty())
        return;

    // --- Shared tab layout (used by both NanoVG and grid) ---
    // Grid cells are positioned at: pixel_x = col * cw + padding.
    // We compute tab spans in column space, then derive NanoVG pill pixel coords.
    std::vector<TabLayout> tabs;
    std::vector<RightPillLayout> right_pills;
    int bar_w = viewport_.pixel_size.x;
    int bar_h = 0;
    int cw_shared = 0;
    int ch_shared = 0;

    if (show_top_bar)
    {
        const auto [cw, ch] = deps_.grid_renderer->cell_size_pixels();
        cw_shared = cw;
        ch_shared = ch;
        if (cw > 0 && ch > 0)
        {
            bar_h = tab_bar_height();
            const int grid_cols = std::max(0, bar_w / cw);
            int right_col_cursor = grid_cols;
            if (show_resource_pill && grid_cols > 0)
            {
                RightPillLayout layout;
                layout.bg = resource_pill_background_color(*deps_.system_resource_snapshot);
                layout.flat_right_edge = true;
                append_resource_metric(layout.clusters, "CPU", deps_.system_resource_snapshot->cpu_percent, true);
                append_resource_metric(layout.clusters, "RAM", deps_.system_resource_snapshot->memory_percent, false);

                const int total_cols = label_cluster_columns(layout.clusters) + kTabPadCols * 2;
                layout.col_end = right_col_cursor;
                layout.col_begin = std::max(0, right_col_cursor - total_cols);
                layout.text_col = layout.col_begin + kTabPadCols;
                right_col_cursor = std::max(0, layout.col_begin - 1);
                right_pills.push_back(std::move(layout));
            }

            if (deps_.chord_indicator && grid_cols > 0)
            {
                if (auto state = deps_.chord_indicator(); state && state->second > 0.0f)
                {
                    RightPillLayout layout;
                    layout.bg = apply_alpha(kChordPillBg, state->second);
                    layout.flat_right_edge = false;
                    append_label_clusters(layout.clusters, state->first, apply_alpha(kChordPillFg, state->second));

                    const int total_cols = label_cluster_columns(layout.clusters) + kTabPadCols * 2;
                    layout.col_end = right_col_cursor;
                    layout.col_begin = std::max(0, right_col_cursor - total_cols);
                    layout.text_col = layout.col_begin + kTabPadCols;
                    right_col_cursor = std::max(0, layout.col_begin - 1);
                    right_pills.push_back(std::move(layout));
                }
            }

            int col_cursor = 0; // start at column 0
            const int tabs_end_col = right_pills.empty() ? grid_cols : std::max(0, right_col_cursor);
            for (size_t wi = 0; wi < workspaces.size(); ++wi)
            {
                const auto& ws = workspaces[wi];
                const std::string label = tab_label(wi, ws->name);
                const int label_cols = static_cast<int>(label.size());
                const int total_cols = label_cols + kTabPadCols * 2;
                if (tabs_end_col > 0 && col_cursor + total_cols > tabs_end_col)
                    break;

                TabLayout tl;
                tl.col_begin = col_cursor;
                tl.col_end = col_cursor + total_cols;
                tl.text_col = col_cursor + kTabPadCols;
                tl.text_len = label_cols;
                tl.active = (ws->id == active_ws_id);
                tl.label = label;
                tabs.push_back(std::move(tl));

                col_cursor += total_cols;
            }
        }
    }

    // Build NanoVG pill rects from the column-based layout.
    struct TabRect
    {
        float x, y, w, h;
        float accent_w; // width of the burgundy accent region (number portion)
        bool active;
    };
    std::vector<TabRect> tab_rects;
    if (!tabs.empty() && cw_shared > 0)
    {
        const float pill_h = static_cast<float>(ch_shared) - 4.0f; // 2px margin top+bottom
        const float pill_y = 2.0f;
        const float half_gap = static_cast<float>(cw_shared) * 0.25f; // 0.5 col gap between tabs
        for (size_t i = 0; i < tabs.size(); ++i)
        {
            const auto& tl = tabs[i];
            // Derive pixel position from grid column: pixel_x = col * cw + padding
            const float px = static_cast<float>(tl.col_begin * cw_shared + kGridPadding) + half_gap;
            const float pw = static_cast<float>((tl.col_end - tl.col_begin) * cw_shared) - half_gap * 2.0f;
            // Accent covers left padding + number + ": " (e.g. "1: " = kTabPadCols + digits + 1 cols).
            // The colon is included; the space after it is not.
            const int num_str_len = static_cast<int>(std::to_string(i + 1).size()); // digits
            const int accent_cols = kTabPadCols + num_str_len + 1; // pad + digits + ":"
            const float aw = static_cast<float>(accent_cols * cw_shared);
            tab_rects.push_back({ px, pill_y, pw, pill_h, aw, tl.active });
        }
    }

    struct RightPillRect
    {
        float x = 0.0f;
        float y = 0.0f;
        float w = 0.0f;
        float h = 0.0f;
        Color bg{};
        bool flat_right_edge = false;
    };
    std::vector<RightPillRect> right_pill_rects;
    if (!right_pills.empty() && cw_shared > 0 && ch_shared > 0)
    {
        right_pill_rects.reserve(right_pills.size());
        for (const auto& pill : right_pills)
        {
            const int total_cols = std::max(1, pill.col_end - pill.col_begin);
            const float pill_h = static_cast<float>(ch_shared) - 4.0f;
            const float pill_y = 2.0f;
            const float pill_x = std::max(0.0f, static_cast<float>(pill.col_begin * cw_shared));
            const float pill_w = pill.flat_right_edge && pill.col_end >= bar_w / cw_shared
                ? static_cast<float>(bar_w) - pill_x
                : static_cast<float>(total_cols * cw_shared);
            right_pill_rects.push_back(RightPillRect{ pill_x, pill_y, pill_w, pill_h, pill.bg, pill.flat_right_edge });
        }
    }

    // Divider geometry.
    struct Divider
    {
        float x, y, w, h;
        SplitDirection dir;
    };
    std::vector<Divider> dividers;
    struct FocusRect
    {
        float x, y, w, h;
    };
    std::optional<FocusRect> focus_rect;

    if (show_dividers)
    {
        const auto& tree = active_tree();
        tree.for_each_divider([&](const SplitTree::DividerRect& r) {
            dividers.push_back({ static_cast<float>(r.x), static_cast<float>(r.y),
                static_cast<float>(r.w), static_cast<float>(r.h), r.direction });
        });

        const LeafId focused = tree.focused();
        if (focused != kInvalidLeaf)
        {
            const auto desc = tree.descriptor_for(focused);
            if (desc.pixel_size.x > 0 && desc.pixel_size.y > 0)
            {
                focus_rect = FocusRect{
                    static_cast<float>(desc.pixel_pos.x),
                    static_cast<float>(desc.pixel_pos.y),
                    static_cast<float>(desc.pixel_size.x),
                    static_cast<float>(desc.pixel_size.y)
                };
            }
        }
    }

    // Pane status pills: one rounded rect per pane, right-aligned in the
    // reserved bottom strip and styled to match the workspace tab pills.
    struct StatusPillRect
    {
        float x, y, w, h;
        float accent_w; // burgundy "number" portion (only drawn when focused)
        bool focused;
    };
    std::vector<StatusPillRect> status_pill_rects;
    if (status_strip_h > 0 && deps_.grid_renderer)
    {
        const auto [cw, ch] = deps_.grid_renderer->cell_size_pixels();
        if (cw > 0 && ch > 0)
        {
            status_pill_rects.reserve(status_entries.size());
            for (const auto& e : status_entries)
            {
                // Label = "<index>: <text>" — same prefix scheme as tabs.
                const std::string num_str = std::to_string(e.index);
                const int label_cols = static_cast<int>(num_str.size()) + 2 // "N:"
                    + 1 // space after colon
                    + static_cast<int>(e.text.size());
                const int total_cols = label_cols + kTabPadCols * 2;

                // Right-align: pill ends one cell from the pane's right edge.
                // Mirror the workspace tab pill math: pill is inset by half_gap
                // on each side of the column span so the visual padding around
                // the text is symmetric (~0.75 cell on each side).
                const float half_gap = static_cast<float>(cw) * 0.25f;
                const float pill_w = static_cast<float>(total_cols * cw) - half_gap * 2.0f;
                const float pill_h = static_cast<float>(ch) - 4.0f; // 2px margin top/bottom
                const float right_edge = static_cast<float>(e.pane_x + e.pane_w
                    - kPaneStatusRightMarginCols * cw);
                const float pill_x = right_edge - pill_w;
                const float strip_top = static_cast<float>(e.pane_y + e.pane_h - status_strip_h);
                const float pill_y = strip_top + 2.0f;

                // Match the workspace-tab accent width: left padding + digits + ":".
                // The trailing space after the colon sits outside the accent.
                const int accent_cols = kTabPadCols + static_cast<int>(num_str.size()) + 1;
                const float accent_w = static_cast<float>(accent_cols * cw);

                StatusPillRect r;
                r.x = pill_x;
                r.y = pill_y;
                r.w = pill_w;
                r.h = pill_h;
                r.accent_w = accent_w;
                r.focused = e.focused;
                status_pill_rects.push_back(r);
            }
        }
    }

    // Single NanoVG callback draws everything: tab bar shapes + dividers + focus.
    const float focus_border = deps_.config ? deps_.config->focus_border_width : 3.0f;
    nanovg_pass_->set_draw_callback(
        [tab_rects = std::move(tab_rects), bar_w, bar_h,
            right_pill_rects = std::move(right_pill_rects),
            dividers = std::move(dividers), focus_rect, focus_border,
            status_pill_rects = std::move(status_pill_rects)](NVGcontext* vg, int /*w*/, int /*h*/) {
            // --- Pane status pills (drawn first so dividers/focus border go on top) ---
            for (const auto& s : status_pill_rects)
            {
                const float radius = s.h * 0.5f; // pill shape

                // Pill body — same inactive grey background as workspace tabs.
                nvgBeginPath(vg);
                nvgRoundedRect(vg, s.x, s.y, s.w, s.h, radius);
                nvgFillColor(vg, nvgRGBAf(kInactiveTabBg.r, kInactiveTabBg.g, kInactiveTabBg.b, kInactiveTabBg.a));
                nvgFill(vg);

                // Number-prefix accent — bright green when focused, dimmed
                // green-grey otherwise. Distinguishes pane pills from the
                // burgundy-accented workspace tab pills.
                nvgSave(vg);
                nvgIntersectScissor(vg, s.x, s.y, s.w, s.h);
                nvgBeginPath(vg);
                nvgRoundedRect(vg, s.x, s.y, s.accent_w, s.h, radius);
                if (s.focused)
                    nvgFillColor(vg, nvgRGBA(kPaneAccentR, kPaneAccentG, kPaneAccentB, kPaneAccentA));
                else
                    nvgFillColor(vg, nvgRGBA(kInactiveAccentR, kInactiveAccentG, kInactiveAccentB, kInactiveAccentA));
                nvgFill(vg);
                nvgRestore(vg);
            }

            // --- Tab bar ---
            if (bar_h > 0)
            {
                // Bar background
                nvgBeginPath(vg);
                nvgRect(vg, 0, 0, static_cast<float>(bar_w), static_cast<float>(bar_h));
                nvgFillColor(vg, nvgRGBAf(kTabBarBg.r, kTabBarBg.g, kTabBarBg.b, kTabBarBg.a));
                nvgFill(vg);
            }

            for (const auto& tab : tab_rects)
            {
                const float radius = tab.h * 0.5f; // pill shape

                // Full pill body — inactive bg for all tabs.
                nvgBeginPath(vg);
                nvgRoundedRect(vg, tab.x, tab.y, tab.w, tab.h, radius);
                nvgFillColor(vg, nvgRGBAf(kInactiveTabBg.r, kInactiveTabBg.g, kInactiveTabBg.b, kInactiveTabBg.a));
                nvgFill(vg);

                if (tab.active)
                {
                    // Active tab: bright red accent on the number portion (left side).
                    nvgSave(vg);
                    nvgIntersectScissor(vg, tab.x, tab.y, tab.w, tab.h);
                    nvgBeginPath(vg);
                    nvgRoundedRect(vg, tab.x, tab.y, tab.accent_w, tab.h, radius);
                    nvgFillColor(vg, nvgRGBA(185, 60, 60, 220));
                    nvgFill(vg);
                    nvgRestore(vg);
                }
                else
                {
                    // Inactive tab: dimmed accent on the number portion only.
                    nvgSave(vg);
                    nvgIntersectScissor(vg, tab.x, tab.y, tab.w, tab.h);
                    nvgBeginPath(vg);
                    nvgRoundedRect(vg, tab.x, tab.y, tab.accent_w, tab.h, radius);
                    nvgFillColor(vg, nvgRGBA(110, 115, 140, 200));
                    nvgFill(vg);
                    nvgRestore(vg);
                }
            }

            for (const auto& pill : right_pill_rects)
            {
                const float radius = pill.h * 0.5f;
                nvgBeginPath(vg);
                if (pill.flat_right_edge)
                {
                    nvgRoundedRectVarying(
                        vg, pill.x, pill.y, pill.w, pill.h, radius, 0.0f, 0.0f, radius);
                }
                else
                {
                    nvgRoundedRect(vg, pill.x, pill.y, pill.w, pill.h, radius);
                }
                nvgFillColor(vg, nvgRGBAf(pill.bg.r, pill.bg.g, pill.bg.b, pill.bg.a));
                nvgFill(vg);
            }

            // --- Divider lines ---
            for (const auto& d : dividers)
            {
                nvgBeginPath(vg);
                if (d.dir == SplitDirection::Vertical)
                {
                    float cx = d.x + d.w * 0.5f;
                    nvgMoveTo(vg, cx, d.y);
                    nvgLineTo(vg, cx, d.y + d.h);
                }
                else
                {
                    float cy = d.y + d.h * 0.5f;
                    nvgMoveTo(vg, d.x, cy);
                    nvgLineTo(vg, d.x + d.w, cy);
                }
                nvgStrokeColor(vg, nvgRGBA(120, 120, 140, 220));
                nvgStrokeWidth(vg, 1.0f);
                nvgStroke(vg);
            }

            // --- Focus indicator ---
            if (focus_rect)
            {
                const float border = focus_border;
                const float half = border * 0.5f;
                constexpr float div_half = static_cast<float>(SplitTree::kDividerWidth) * 0.5f;

                const float pane_right = focus_rect->x + focus_rect->w;
                const float pane_bottom = focus_rect->y + focus_rect->h;

                bool has_right_divider = false;
                bool has_bottom_divider = false;
                for (const auto& d : dividers)
                {
                    if (d.dir == SplitDirection::Vertical
                        && std::abs(d.x - pane_right) < 1.0f)
                        has_right_divider = true;
                    if (d.dir == SplitDirection::Horizontal
                        && std::abs(d.y - pane_bottom) < 1.0f)
                        has_bottom_divider = true;
                }
                const float right = has_right_divider ? (pane_right + div_half) : (pane_right - half);
                const float bottom = has_bottom_divider ? (pane_bottom + div_half) : (pane_bottom - half);

                nvgStrokeColor(vg, nvgRGBA(185, 60, 60, 220));
                nvgStrokeWidth(vg, border);

                nvgBeginPath(vg);
                nvgMoveTo(vg, right, focus_rect->y);
                nvgLineTo(vg, right, bottom);
                nvgStroke(vg);

                nvgBeginPath(vg);
                nvgMoveTo(vg, focus_rect->x, bottom);
                nvgLineTo(vg, right, bottom);
                nvgStroke(vg);
            }
        });

    // NanoVG pass covers the full viewport (tab bar + content area).
    RenderViewport vp;
    vp.width = viewport_.pixel_size.x;
    vp.height = viewport_.pixel_size.y;
    frame.record_render_pass(*nanovg_pass_, vp);

    // Grid handle draws top-bar label text on top of NanoVG shapes.
    if (bar_h > 0 && (!tabs.empty() || !right_pills.empty()))
    {
        update_tab_grid(tabs, right_pills);
        if (tab_handle_)
            frame.draw_grid_handle(*tab_handle_);
    }

    // Per-pane status bar text (WI 78).
    update_pane_status_grids(frame, status_entries);

    frame.flush_submit_chunk();
}

void ChromeHost::update_pane_status_grids(IFrameContext& frame, std::span<const PaneStatusEntry> entries)
{
    if (!deps_.grid_renderer || !deps_.text_service)
        return;

    const auto [cw, ch] = deps_.grid_renderer->cell_size_pixels();
    if (cw <= 0 || ch <= 0)
    {
        pane_status_handles_.clear();
        return;
    }

    // Prune handles for leaves that are no longer present.
    std::unordered_set<LeafId> live_ids;
    live_ids.reserve(entries.size());
    for (const auto& e : entries)
        live_ids.insert(e.leaf);
    for (auto it = pane_status_handles_.begin(); it != pane_status_handles_.end();)
    {
        if (!live_ids.count(it->first))
            it = pane_status_handles_.erase(it);
        else
            ++it;
    }

    if (entries.empty())
        return;

    // Match the workspace tab text colors:
    //   - light fg on the burgundy "number" accent
    //   - kInactiveTabFg on the rest of the label
    constexpr Color kAccentFg{ 0.85f, 0.85f, 0.90f, 1.0f }; // light text on burgundy

    for (const auto& e : entries)
    {
        // Build the same "N: <text>" label the pill geometry was sized for.
        const std::string num_str = std::to_string(e.index);
        std::string label = num_str + ": " + e.text;

        const int label_cols = static_cast<int>(label.size());
        const int total_cols = label_cols + kTabPadCols * 2;
        // Cap pill width to the pane width so a long label cannot escape.
        const int max_cols = std::max(1, e.pane_w / cw - kPaneStatusRightMarginCols);
        const int pill_cols = std::min(total_cols, max_cols);

        // If the label was clamped, truncate the visible text portion (keep the
        // "N: " prefix intact) and append an ellipsis.
        const int prefix_cols = static_cast<int>(num_str.size()) + 2; // "N: "
        const int usable_label_cols = std::max(0, pill_cols - kTabPadCols * 2);
        if (label_cols > usable_label_cols)
        {
            const int text_room = std::max(0, usable_label_cols - prefix_cols);
            std::string truncated = num_str + ": ";
            if (text_room >= 1 && !e.text.empty())
            {
                if (static_cast<int>(e.text.size()) > text_room)
                {
                    truncated += e.text.substr(0, static_cast<size_t>(text_room - 1));
                    truncated += "…";
                }
                else
                {
                    truncated += e.text;
                }
            }
            label = std::move(truncated);
        }

        // Pill placement (must mirror the StatusPillRect math in draw()):
        // total column span minus the half_gap inset on each side.
        const int half_gap = cw / 4;
        const int pill_w_px = pill_cols * cw - half_gap * 2;
        const int strip_y = e.pane_y + e.pane_h - ch;
        const int right_edge = e.pane_x + e.pane_w - kPaneStatusRightMarginCols * cw;
        const int pill_x_px = right_edge - pill_w_px;

        auto& handle = pane_status_handles_[e.leaf];
        if (!handle)
        {
            handle = deps_.grid_renderer->create_grid_handle();
            if (!handle)
            {
                DRAXUL_LOG_ERROR(LogCategory::App, "ChromeHost: pane status create_grid_handle() returned null");
                continue;
            }
            handle->set_default_background({ 0, 0, 0, 0 });
            handle->set_cursor(-1, -1, CursorStyle{});
            handle->set_cursor_visible(false);
        }

        // Viewport: shift the grid up by the renderer's internal padding so the
        // single row centers vertically (same trick as update_tab_grid), AND
        // shift it LEFT by (padding + half_gap) so column 1 (the digit) lands
        // ~0.75 cells inside the pill — matching how update_tab_grid positions
        // the digit inside its tab pills.
        const int padding = deps_.grid_renderer->padding();
        PaneDescriptor desc;
        desc.pixel_pos = { pill_x_px - padding - half_gap, strip_y - padding };
        desc.pixel_size = { pill_w_px + padding * 2, ch + padding };
        handle->set_viewport(desc);
        handle->set_grid_size(pill_cols, 1);

        // Warm any uncached glyphs before updating the handle so newly typed
        // chord/pane-status characters do not appear a frame late.
        for (int ci = 0; ci < static_cast<int>(label.size()); ++ci)
        {
            const std::string cluster(1, label[static_cast<size_t>(ci)]);
            deps_.text_service->resolve_cluster(cluster);
        }
        flush_atlas_if_dirty();

        // Cells start fully transparent — the NanoVG pill provides the bg.
        const Color transparent{ 0.0f, 0.0f, 0.0f, 0.0f };
        std::vector<CellUpdate> cells;
        cells.reserve(static_cast<size_t>(pill_cols));
        for (int c = 0; c < pill_cols; ++c)
        {
            CellUpdate cell{};
            cell.col = c;
            cell.row = 0;
            cell.bg = transparent;
            cell.fg = transparent;
            cells.push_back(cell);
        }

        // Pane-status text sits one cell tighter than workspace tabs so the
        // number aligns with the accent block instead of leaving a blank
        // leading cell inside the pill.
        const int num_prefix_len = static_cast<int>(num_str.size()) + 1; // digits + ":"
        for (int ci = 0; ci < static_cast<int>(label.size()); ++ci)
        {
            const int col = ci;
            if (col < 0 || col >= pill_cols)
                continue;
            const Color& fg = (ci < num_prefix_len) ? kAccentFg : kInactiveTabFg;
            const std::string cluster(1, label[static_cast<size_t>(ci)]);
            AtlasRegion glyph = deps_.text_service->resolve_cluster(cluster);
            auto& cell = cells[static_cast<size_t>(col)];
            cell.fg = fg;
            cell.glyph = glyph;
            if (glyph.is_color)
                cell.style_flags |= STYLE_FLAG_COLOR_GLYPH;
        }

        handle->update_cells(cells);
    }

    flush_atlas_if_dirty();

    for (const auto& e : entries)
    {
        auto it = pane_status_handles_.find(e.leaf);
        if (it != pane_status_handles_.end() && it->second)
            frame.draw_grid_handle(*it->second);
    }
}

void ChromeHost::update_tab_grid(std::span<const TabLayout> tabs, std::span<const RightPillLayout> right_pills)
{
    if (!deps_.grid_renderer || !deps_.text_service)
        return;

    const auto [cw, ch] = deps_.grid_renderer->cell_size_pixels();
    if (cw <= 0 || ch <= 0)
        return;

    const int bar_w = viewport_.pixel_size.x;
    const int grid_h = ch; // grid covers cell row, accent line below
    const int grid_cols = bar_w / cw;
    const int grid_rows = 1;

    if (grid_cols <= 0)
        return;

    if (!tab_handle_)
    {
        tab_handle_ = deps_.grid_renderer->create_grid_handle();
        if (!tab_handle_)
        {
            DRAXUL_LOG_ERROR(LogCategory::App, "ChromeHost: create_grid_handle() returned null");
            return;
        }
        tab_handle_->set_default_background({ 0, 0, 0, 0 });
    }

    PaneDescriptor desc;
    desc.pixel_pos = { 0, -kGridPadding }; // shift grid up so text centers in pills
    desc.pixel_size = { bar_w, grid_h + kGridPadding };
    tab_handle_->set_viewport(desc);
    tab_handle_->set_grid_size(grid_cols, grid_rows);
    tab_handle_->set_cursor(-1, -1, CursorStyle{});
    tab_handle_->set_cursor_visible(false);

    // Transparent cells — NanoVG pill shapes provide the tab backgrounds.
    // Grid cells only carry foreground text glyphs.
    std::vector<CellUpdate> cells;
    const Color transparent{ 0.0f, 0.0f, 0.0f, 0.0f };

    for (int c = 0; c < grid_cols; ++c)
    {
        CellUpdate cell{};
        cell.col = c;
        cell.row = 0;
        cell.bg = transparent;
        cell.fg = transparent;
        cells.push_back(cell);
    }

    // Warm any uncached glyphs before the grid handle consumes them so the
    // chord pill does not reveal late-arriving characters over multiple frames.
    for (const auto& tl : tabs)
    {
        for (const auto& cluster_text : split_display_clusters(tl.label))
            deps_.text_service->resolve_cluster(cluster_text);
    }
    for (const auto& pill : right_pills)
    {
        for (const auto& cluster : pill.clusters)
            deps_.text_service->resolve_cluster(cluster.text);
    }
    flush_atlas_if_dirty();

    // Write text into grid cells using the shared column-based layout.
    // Light text on burgundy accent (number portion), standard text elsewhere.
    constexpr Color kAccentFg{ 0.85f, 0.85f, 0.90f, 1.0f }; // light text on burgundy
    for (size_t ti = 0; ti < tabs.size(); ++ti)
    {
        const auto& tl = tabs[ti];
        // Number prefix width: digits of (index+1) + ":" (the space after is name territory).
        const int num_prefix_cols = static_cast<int>(std::to_string(ti + 1).size()) + 1; // digits + ":"
        int label_col = tl.text_col;
        int consumed_cols = 0;
        for (const auto& cluster_text : split_display_clusters(tl.label))
        {
            const int col = label_col;
            if (col < 0 || col >= grid_cols)
            {
                label_col += std::max(1, cluster_cell_width(cluster_text));
                consumed_cols += std::max(1, cluster_cell_width(cluster_text));
                continue;
            }

            // Active tab: number chars on burgundy get light fg, rest gets inactive fg.
            const int cluster_width = std::max(1, cluster_cell_width(cluster_text));
            const Color& fg = (tl.active && consumed_cols < num_prefix_cols) ? kAccentFg : kInactiveTabFg;
            AtlasRegion glyph = deps_.text_service->resolve_cluster(cluster_text);

            auto& cell = cells[static_cast<size_t>(col)];
            cell.fg = fg;
            cell.glyph = glyph;
            if (glyph.is_color)
                cell.style_flags |= STYLE_FLAG_COLOR_GLYPH;
            label_col += cluster_width;
            consumed_cols += cluster_width;
        }
    }

    for (const auto& pill : right_pills)
    {
        int col = pill.text_col;
        for (const auto& cluster : pill.clusters)
        {
            if (col < 0 || col >= grid_cols)
            {
                col += std::max(1, cluster.width);
                continue;
            }

            AtlasRegion glyph = deps_.text_service->resolve_cluster(cluster.text);
            auto& cell = cells[static_cast<size_t>(col)];
            cell.fg = cluster.fg;
            cell.glyph = glyph;
            if (glyph.is_color)
                cell.style_flags |= STYLE_FLAG_COLOR_GLYPH;
            col += std::max(1, cluster.width);
        }
    }

    tab_handle_->update_cells(cells);
    flush_atlas_if_dirty();
}

void ChromeHost::flush_atlas_if_dirty()
{
    if (!deps_.text_service || !deps_.grid_renderer)
        return;
    if (!deps_.text_service->atlas_dirty())
        return;

    const auto dirty = deps_.text_service->atlas_dirty_rect();
    if (dirty.size.x <= 0 || dirty.size.y <= 0)
        return;

    constexpr size_t kPixelSize = 4;
    const size_t row_bytes = static_cast<size_t>(dirty.size.x) * kPixelSize;
    std::vector<uint8_t> scratch(row_bytes * dirty.size.y);
    const uint8_t* atlas = deps_.text_service->atlas_data();
    const int atlas_w = deps_.text_service->atlas_width();
    for (int r = 0; r < dirty.size.y; ++r)
    {
        const uint8_t* src = atlas
            + (static_cast<size_t>(dirty.pos.y + r) * atlas_w + dirty.pos.x) * kPixelSize;
        std::memcpy(scratch.data() + static_cast<size_t>(r) * row_bytes, src, row_bytes);
    }
    deps_.grid_renderer->update_atlas_region(
        dirty.pos.x, dirty.pos.y, dirty.size.x, dirty.size.y, scratch.data());
    deps_.text_service->clear_atlas_dirty();
}

} // namespace draxul
