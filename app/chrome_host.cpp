#include "chrome_host.h"

#include <algorithm>
#include <cmath>
#include <nanovg.h>

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
    for (auto& ws : workspaces_)
        ws->host_manager.shutdown();
    workspaces_.clear();
    active_workspace_ = -1;
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

bool ChromeHost::create_initial_workspace(IHostCallbacks& callbacks, int pixel_w, int pixel_h)
{
    auto ws = std::make_unique<Workspace>(next_workspace_id_++, make_host_manager_deps());
    if (!ws->host_manager.create(callbacks, pixel_w, pixel_h))
    {
        last_create_error_ = ws->host_manager.error();
        return false;
    }
    ws->initialized = true;
    ws->name = "Tab 1";
    active_workspace_ = ws->id;
    workspaces_.push_back(std::move(ws));
    return true;
}

int ChromeHost::add_workspace(IHostCallbacks& callbacks, int pixel_w, int pixel_h)
{
    auto ws = std::make_unique<Workspace>(next_workspace_id_++, make_host_manager_deps());
    if (!ws->host_manager.create(callbacks, pixel_w, pixel_h))
    {
        last_create_error_ = ws->host_manager.error();
        return -1;
    }
    ws->initialized = true;
    ws->name = "Tab " + std::to_string(workspaces_.size() + 1);
    int id = ws->id;
    workspaces_.push_back(std::move(ws));
    activate_workspace(id);
    return id;
}

bool ChromeHost::close_workspace(int workspace_id, IHostCallbacks& /*callbacks*/)
{
    if (workspaces_.size() <= 1)
        return false;

    auto it = std::find_if(workspaces_.begin(), workspaces_.end(),
        [workspace_id](const auto& ws) { return ws->id == workspace_id; });
    if (it == workspaces_.end())
        return false;

    (*it)->host_manager.shutdown();

    bool was_active = (workspace_id == active_workspace_);
    workspaces_.erase(it);

    if (was_active)
        activate_workspace(workspaces_.front()->id);

    return true;
}

void ChromeHost::activate_workspace(int workspace_id)
{
    if (workspace_id == active_workspace_)
        return;

    // Notify old workspace's focused host of focus loss.
    for (auto& ws : workspaces_)
    {
        if (ws->id == active_workspace_)
        {
            if (IHost* h = ws->host_manager.focused_host())
                h->on_focus_lost();
            break;
        }
    }

    active_workspace_ = workspace_id;

    // Notify new workspace's focused host of focus gain.
    for (auto& ws : workspaces_)
    {
        if (ws->id == active_workspace_)
        {
            if (IHost* h = ws->host_manager.focused_host())
                h->on_focus_gained();
            break;
        }
    }
}

void ChromeHost::next_workspace()
{
    if (workspaces_.size() <= 1)
        return;
    for (size_t i = 0; i < workspaces_.size(); ++i)
    {
        if (workspaces_[i]->id == active_workspace_)
        {
            size_t next = (i + 1) % workspaces_.size();
            activate_workspace(workspaces_[next]->id);
            return;
        }
    }
}

void ChromeHost::prev_workspace()
{
    if (workspaces_.size() <= 1)
        return;
    for (size_t i = 0; i < workspaces_.size(); ++i)
    {
        if (workspaces_[i]->id == active_workspace_)
        {
            size_t prev = (i == 0) ? workspaces_.size() - 1 : i - 1;
            activate_workspace(workspaces_[prev]->id);
            return;
        }
    }
}

HostManager& ChromeHost::active_host_manager()
{
    for (auto& ws : workspaces_)
    {
        if (ws->id == active_workspace_)
            return ws->host_manager;
    }
    // Should never happen — caller must ensure at least one workspace exists.
    static HostManager dummy(HostManager::Deps{});
    return dummy;
}

const HostManager& ChromeHost::active_host_manager() const
{
    for (const auto& ws : workspaces_)
    {
        if (ws->id == active_workspace_)
            return ws->host_manager;
    }
    static const HostManager dummy(HostManager::Deps{});
    return dummy;
}

const SplitTree& ChromeHost::active_tree() const
{
    return active_host_manager().tree();
}

int ChromeHost::tab_bar_height() const
{
    // Single workspace: no tab bar.
    return 0;
}

int ChromeHost::active_workspace_id() const
{
    return active_workspace_;
}

HostManager::Deps ChromeHost::make_host_manager_deps() const
{
    HostManager::Deps hm_deps;
    hm_deps.options = deps_.options;
    hm_deps.config = deps_.config;
    hm_deps.config_document = deps_.config_document;
    hm_deps.window = deps_.window;
    hm_deps.grid_renderer = deps_.grid_renderer;
    hm_deps.imgui_host = deps_.imgui_host;
    hm_deps.text_service = deps_.text_service;
    hm_deps.display_ppi = deps_.display_ppi;
    hm_deps.owner_lifetime = deps_.owner_lifetime;
    hm_deps.compute_viewport = deps_.compute_viewport;
    return hm_deps;
}

void ChromeHost::draw(IFrameContext& frame)
{
    if (!nanovg_pass_)
        return;

    const auto& tree = active_tree();
    if (tree.leaf_count() < 2)
        return;

    // Collect divider rects from the active workspace's split tree.
    struct Divider
    {
        float x, y, w, h;
        SplitDirection dir;
    };
    std::vector<Divider> dividers;
    tree.for_each_divider([&](const SplitTree::DividerRect& r) {
        dividers.push_back({ static_cast<float>(r.x), static_cast<float>(r.y),
            static_cast<float>(r.w), static_cast<float>(r.h), r.direction });
    });

    if (dividers.empty())
        return;

    // Focused pane border rect (in pixels).
    struct FocusRect
    {
        float x, y, w, h;
    };
    std::optional<FocusRect> focus_rect;
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

    nanovg_pass_->set_draw_callback(
        [dividers = std::move(dividers), focus_rect](NVGcontext* vg, int /*w*/, int /*h*/) {
            // Divider lines
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

            // Focus indicator — muted burgundy on right and bottom edges only.
            // At divider boundaries, draw on the divider centre line;
            // at window edges, inset so the stroke stays visible.
            if (focus_rect)
            {
                constexpr float border = 2.0f;
                constexpr float half = border * 0.5f;
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

                nvgStrokeColor(vg, nvgRGBA(140, 50, 55, 200));
                nvgStrokeWidth(vg, border);

                // Right edge
                nvgBeginPath(vg);
                nvgMoveTo(vg, right, focus_rect->y);
                nvgLineTo(vg, right, bottom);
                nvgStroke(vg);

                // Bottom edge
                nvgBeginPath(vg);
                nvgMoveTo(vg, focus_rect->x, bottom);
                nvgLineTo(vg, right, bottom);
                nvgStroke(vg);
            }
        });

    RenderViewport vp;
    vp.width = viewport_.pixel_size.x;
    vp.height = viewport_.pixel_size.y;
    frame.record_render_pass(*nanovg_pass_, vp);
}

} // namespace draxul
