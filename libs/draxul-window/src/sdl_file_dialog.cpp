#include "sdl_file_dialog.h"
#include <SDL3/SDL_dialog.h>

namespace draxul::sdl
{

void show_open_file_dialog(SDL_Window* window, Uint32 result_event_type)
{
    struct Ctx
    {
        SDL_Window* window;
        unsigned int event_type;
    };
    auto* ctx = new Ctx{ window, result_event_type };

    SDL_ShowOpenFileDialog(
        [](void* userdata, const char* const* filelist, int /*filter*/) {
            auto* c = static_cast<Ctx*>(userdata);
            if (filelist && filelist[0])
            {
                // Heap-allocate the path; freed in handle_file_dialog_event().
                auto* path = new std::string(filelist[0]);
                SDL_Event ev = {};
                ev.type = c->event_type;
                ev.user.data1 = path;
                SDL_PushEvent(&ev);
            }
            delete c;
        },
        ctx,
        window,
        nullptr, // no file filters
        0,
        nullptr, // no default location
        false // single selection
    );
}

bool handle_file_dialog_event(const SDL_Event& event,
    Uint32 file_dialog_event_type,
    const std::function<void(const std::string&)>& on_path)
{
    if (event.type != file_dialog_event_type)
        return false;
    auto* path = static_cast<std::string*>(event.user.data1);
    if (path)
    {
        if (on_path)
            on_path(*path);
        delete path;
    }
    return true;
}

} // namespace draxul::sdl
