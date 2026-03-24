#include "isometric_camera.h"
#include "isometric_scene_pass.h"
#include "isometric_world.h"
#include "ui_treesitter_panel.h"
#include <SDL3/SDL.h>
#include <cmath>
#include <draxul/log.h>
#include <draxul/megacity_host.h>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>

#ifndef DRAXUL_REPO_ROOT
#define DRAXUL_REPO_ROOT "."
#endif

namespace draxul
{

namespace
{

constexpr float kMovementSpeedTilesPerSecond = 3.5f;
constexpr float kOrbitSpeedRadiansPerSecond = 1.8f;
constexpr float kOrbitDragReferencePixelsPerSecond = 240.0f;
constexpr float kOrbitDragRadiansPerPixel = kOrbitSpeedRadiansPerSecond / kOrbitDragReferencePixelsPerSecond;
constexpr auto kMovementTick = std::chrono::milliseconds(16);

bool is_left_arrow(const KeyEvent& event)
{
    return event.scancode == SDL_SCANCODE_LEFT || event.keycode == SDLK_LEFT
        || event.scancode == SDL_SCANCODE_A || event.keycode == SDLK_A;
}

bool is_right_arrow(const KeyEvent& event)
{
    return event.scancode == SDL_SCANCODE_RIGHT || event.keycode == SDLK_RIGHT
        || event.scancode == SDL_SCANCODE_D || event.keycode == SDLK_D;
}

bool is_up_arrow(const KeyEvent& event)
{
    return event.scancode == SDL_SCANCODE_UP || event.keycode == SDLK_UP
        || event.scancode == SDL_SCANCODE_W || event.keycode == SDLK_W;
}

bool is_down_arrow(const KeyEvent& event)
{
    return event.scancode == SDL_SCANCODE_DOWN || event.keycode == SDLK_DOWN
        || event.scancode == SDL_SCANCODE_S || event.keycode == SDLK_S;
}

bool is_orbit_left_key(const KeyEvent& event)
{
    return event.scancode == SDL_SCANCODE_Q || event.keycode == SDLK_Q;
}

bool is_orbit_right_key(const KeyEvent& event)
{
    return event.scancode == SDL_SCANCODE_E || event.keycode == SDLK_E;
}

} // namespace

MegaCityHost::MegaCityHost() = default;

MegaCityHost::~MegaCityHost() = default;

bool MegaCityHost::initialize(const HostContext& context, IHostCallbacks& callbacks)
{
    callbacks_ = &callbacks;
    viewport_ = context.initial_viewport;
    pixel_w_ = viewport_.pixel_size.x > 0 ? viewport_.pixel_size.x : 800;
    pixel_h_ = viewport_.pixel_size.y > 0 ? viewport_.pixel_size.y : 600;

    world_ = std::make_unique<IsometricWorld>();
    camera_ = std::make_unique<IsometricCamera>();
    camera_->set_viewport(pixel_w_, pixel_h_);
    camera_->look_at_world_center(world_->width() * world_->tile_size(), world_->height() * world_->tile_size());
    scene_pass_ = std::make_shared<IsometricScenePass>(world_->width(), world_->height(), world_->tile_size());

    running_ = true;
    last_activity_time_ = std::chrono::steady_clock::now();
    last_pump_time_ = last_activity_time_;
    scanner_.start(DRAXUL_REPO_ROOT);
    mark_scene_dirty();

    DRAXUL_LOG_INFO(LogCategory::App, "MegaCityHost initialized (%dx%d), scanning %s",
        pixel_w_, pixel_h_, DRAXUL_REPO_ROOT);
    return true;
}

void MegaCityHost::mark_scene_dirty()
{
    scene_dirty_ = true;
    last_activity_time_ = std::chrono::steady_clock::now();
    if (callbacks_)
        callbacks_->request_frame();
}

void MegaCityHost::on_key(const KeyEvent& event)
{
    bool changed = false;
    if (is_left_arrow(event))
    {
        changed = move_left_ != event.pressed;
        move_left_ = event.pressed;
    }
    else if (is_right_arrow(event))
    {
        changed = move_right_ != event.pressed;
        move_right_ = event.pressed;
    }
    else if (is_up_arrow(event))
    {
        changed = move_up_ != event.pressed;
        move_up_ = event.pressed;
    }
    else if (is_down_arrow(event))
    {
        changed = move_down_ != event.pressed;
        move_down_ = event.pressed;
    }
    else if (is_orbit_left_key(event))
    {
        changed = orbit_left_ != event.pressed;
        orbit_left_ = event.pressed;
    }
    else if (is_orbit_right_key(event))
    {
        changed = orbit_right_ != event.pressed;
        orbit_right_ = event.pressed;
    }
    else
    {
        return;
    }

    if (!changed)
        return;

    last_pump_time_ = std::chrono::steady_clock::now();
    mark_scene_dirty();
}

void MegaCityHost::on_mouse_move(const MouseMoveEvent& event)
{
    if (!dragging_scene_ || !camera_)
        return;

    if (SDL_WasInit(SDL_INIT_VIDEO) != 0
        && (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON_LMASK) == 0)
    {
        dragging_scene_ = false;
        return;
    }

    glm::vec2 pixel_delta = event.delta;
    if (glm::dot(pixel_delta, pixel_delta) <= 0.0f)
    {
        const glm::ivec2 fallback_delta = event.pos - last_drag_pos_;
        pixel_delta = glm::vec2(static_cast<float>(fallback_delta.x), static_cast<float>(fallback_delta.y));
    }
    last_drag_pos_ = event.pos;
    if (glm::dot(pixel_delta, pixel_delta) <= 0.0f)
        return;

    if ((event.mod & kModAlt) != 0)
    {
        if (pixel_delta.x != 0.0f)
        {
            camera_->orbit_target(-pixel_delta.x * kOrbitDragRadiansPerPixel);
            mark_scene_dirty();
        }
        return;
    }

    const glm::vec2 pan = camera_->pan_delta_for_screen_drag(pixel_delta);
    if (glm::dot(pan, pan) <= 0.0f)
        return;

    camera_->translate_target(pan.x, pan.y);
    mark_scene_dirty();
}

void MegaCityHost::on_mouse_button(const MouseButtonEvent& event)
{
    if (event.button != SDL_BUTTON_LEFT)
        return;

    dragging_scene_ = event.pressed;
    last_drag_pos_ = event.pos;
}

void MegaCityHost::on_mouse_wheel(const MouseWheelEvent& /*event*/)
{
    // The Megacity view currently has no wheel-driven zoom behavior.
}

void MegaCityHost::set_imgui_font(const std::string&, float)
{
    // Megacity uses the shared app ImGui context and does not own fonts itself.
}

void MegaCityHost::render_imgui(float dt)
{
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(pixel_w_), static_cast<float>(pixel_h_));
    io.DeltaTime = dt > 0.0f ? dt : (1.0f / 60.0f);

    const ImGuiWindowFlags ds_flags = ImGuiWindowFlags_NoDocking
        | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse
        | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus
        | ImGuiWindowFlags_NoBackground;
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(static_cast<float>(pixel_w_), static_cast<float>(pixel_h_)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("##dockspace_root", nullptr, ds_flags);
    ImGui::PopStyleVar(3);
    ImGui::DockSpace(ImGui::GetID("MegaCityDock"), ImVec2(0.0f, 0.0f),
        ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::End();

    render_treesitter_panel(pixel_w_, pixel_h_, scanner_.snapshot());
}

void MegaCityHost::attach_3d_renderer(I3DRenderer& renderer)
{
    renderer_3d_ = &renderer;
    renderer_3d_->register_render_pass(scene_pass_);
    renderer_3d_->set_3d_viewport(viewport_.pixel_pos.x, viewport_.pixel_pos.y, pixel_w_, pixel_h_);

    if (scene_pass_)
    {
        scene_pass_->set_scene(build_scene_snapshot());
        scene_dirty_ = false;
    }
    if (callbacks_)
        callbacks_->request_frame();

    DRAXUL_LOG_INFO(LogCategory::App, "MegaCityHost: 3D renderer attached, scene pass registered");
}

void MegaCityHost::detach_3d_renderer()
{
    if (renderer_3d_)
    {
        renderer_3d_->unregister_render_pass();
        renderer_3d_ = nullptr;
    }
}

void MegaCityHost::shutdown()
{
    scanner_.stop();
    detach_3d_renderer();
    scene_pass_.reset();
    camera_.reset();
    world_.reset();
    running_ = false;
}

bool MegaCityHost::is_running() const
{
    return running_;
}

std::string MegaCityHost::init_error() const
{
    return {};
}

void MegaCityHost::set_viewport(const HostViewport& viewport)
{
    viewport_ = viewport;
    pixel_w_ = viewport.pixel_size.x > 0 ? viewport.pixel_size.x : pixel_w_;
    pixel_h_ = viewport.pixel_size.y > 0 ? viewport.pixel_size.y : pixel_h_;

    if (camera_)
        camera_->set_viewport(pixel_w_, pixel_h_);
    if (renderer_3d_)
        renderer_3d_->set_3d_viewport(viewport_.pixel_pos.x, viewport_.pixel_pos.y, pixel_w_, pixel_h_);

    mark_scene_dirty();
}

SceneSnapshot MegaCityHost::build_scene_snapshot() const
{
    SceneSnapshot scene;
    if (!camera_ || !world_)
        return scene;

    scene.camera.view = camera_->view_matrix();
    scene.camera.proj = camera_->proj_matrix();
    scene.camera.light_dir = glm::normalize(glm::vec4(-0.5f, -1.0f, -0.3f, 0.0f));

    const GroundFootprint footprint = camera_->visible_ground_footprint(0.0f);
    const float tile_size = world_->tile_size();
    scene.floor_grid.enabled = true;
    scene.floor_grid.min_x = static_cast<int>(std::floor(footprint.min_x / tile_size)) - 1;
    scene.floor_grid.max_x = static_cast<int>(std::ceil(footprint.max_x / tile_size)) + 1;
    scene.floor_grid.min_z = static_cast<int>(std::floor(footprint.min_z / tile_size)) - 1;
    scene.floor_grid.max_z = static_cast<int>(std::ceil(footprint.max_z / tile_size)) + 1;
    scene.floor_grid.tile_size = tile_size;
    scene.floor_grid.line_width = tile_size * 0.08f;
    scene.floor_grid.y = -0.001f;
    scene.floor_grid.color = glm::vec4(0.62f, 0.62f, 0.66f, 1.0f);

    for (const auto& obj : world_->objects())
    {
        SceneObject cube;
        cube.mesh = MeshId::Cube;
        const glm::vec3 pos = world_->grid_to_world(obj.x, obj.y, static_cast<float>(obj.elevation));
        cube.world = glm::translate(glm::mat4(1.0f), pos + glm::vec3(0.0f, 0.5f, 0.0f));
        cube.color = glm::vec4(obj.color, 1.0f);
        scene.objects.push_back(cube);
    }

    return scene;
}

bool MegaCityHost::movement_active() const
{
    return move_left_ || move_right_ || move_up_ || move_down_ || orbit_left_ || orbit_right_;
}

void MegaCityHost::pump()
{
    const auto now = std::chrono::steady_clock::now();
    if (movement_active() && camera_)
    {
        glm::vec2 pan_input{ 0.0f, 0.0f };
        if (move_left_)
            pan_input.x -= 1.0f;
        if (move_right_)
            pan_input.x += 1.0f;
        if (move_up_)
            pan_input.y += 1.0f;
        if (move_down_)
            pan_input.y -= 1.0f;

        const float dt = std::chrono::duration<float>(now - last_pump_time_).count();
        const float pan_distance = dt * kMovementSpeedTilesPerSecond;
        if (pan_distance > 0.0f && glm::dot(pan_input, pan_input) > 0.0f)
        {
            const glm::vec2 right = camera_->planar_right_vector();
            const glm::vec2 up = camera_->planar_up_vector();
            const glm::vec2 pan = glm::normalize(pan_input.x * right + pan_input.y * up);
            camera_->translate_target(pan.x * pan_distance, pan.y * pan_distance);
        }

        float orbit = 0.0f;
        if (orbit_left_)
            orbit += 1.0f;
        if (orbit_right_)
            orbit -= 1.0f;
        if (orbit != 0.0f && dt > 0.0f)
            camera_->orbit_target(orbit * dt * kOrbitSpeedRadiansPerSecond);

        if (pan_distance > 0.0f || orbit != 0.0f)
        {
            scene_dirty_ = true;
            last_activity_time_ = now;
            if (callbacks_)
                callbacks_->request_frame();
        }
    }
    last_pump_time_ = now;

    if (scene_dirty_ && scene_pass_)
    {
        scene_pass_->set_scene(build_scene_snapshot());
        scene_dirty_ = false;
    }
    if (running_ && continuous_refresh_enabled_ && callbacks_)
        callbacks_->request_frame();
}

std::optional<std::chrono::steady_clock::time_point> MegaCityHost::next_deadline() const
{
    if (!running_)
        return std::nullopt;
    if (!continuous_refresh_enabled_ && !movement_active())
        return std::nullopt;
    return std::chrono::steady_clock::now() + kMovementTick;
}

bool MegaCityHost::dispatch_action(std::string_view action)
{
    if (action == "quit" || action == "request_quit")
    {
        running_ = false;
        if (callbacks_)
            callbacks_->request_quit();
        return true;
    }
    return false;
}

void MegaCityHost::request_close()
{
    running_ = false;
}

Color MegaCityHost::default_background() const
{
    return Color(0.05f, 0.05f, 0.10f, 1.0f);
}

HostRuntimeState MegaCityHost::runtime_state() const
{
    HostRuntimeState s;
    s.content_ready = true;
    s.last_activity_time = last_activity_time_;
    return s;
}

HostDebugState MegaCityHost::debug_state() const
{
    HostDebugState s;
    s.name = "megacity";
    s.grid_cols = 0;
    s.grid_rows = 0;
    s.dirty_cells = scene_dirty_ ? 1u : 0u;
    return s;
}

std::unique_ptr<IHost> create_megacity_host()
{
    return std::make_unique<MegaCityHost>();
}

} // namespace draxul
