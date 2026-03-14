#pragma once
#include <spectre/window.h>

struct SDL_Window;

namespace spectre
{

class SdlWindow : public IWindow
{
public:
    bool initialize(const std::string& title, int width, int height) override;
    void shutdown() override;
    bool poll_events() override;
    void activate();
    SDL_Window* native_handle() override
    {
        return window_;
    }
    std::pair<int, int> size_pixels() const override;
    float display_ppi() const override;

private:
    SDL_Window* window_ = nullptr;
};

} // namespace spectre
