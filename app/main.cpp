#include "app.h"
#include <cstdio>
#include <cstring>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#endif

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR lpCmdLine, int) {
    bool want_console = false;
    if (lpCmdLine && strstr(lpCmdLine, "--console")) {
        want_console = true;
    }

    if (want_console) {
        AllocConsole();
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
    }

    wchar_t exe_buf[MAX_PATH];
    if (GetModuleFileNameW(nullptr, exe_buf, MAX_PATH)) {
        auto exe_path = std::filesystem::path(exe_buf).parent_path();
        if (!exe_path.empty()) {
            std::filesystem::current_path(exe_path);
        }
    }
#else
int main(int argc, char* argv[]) {
    if (argc > 0) {
        auto exe_path = std::filesystem::path(argv[0]).parent_path();
        if (!exe_path.empty()) {
            std::filesystem::current_path(exe_path);
        }
    }
#endif

    spectre::App app;

    if (!app.initialize()) {
        fprintf(stderr, "Failed to initialize spectre\n");
        return 1;
    }

    app.run();
    app.shutdown();

    return 0;
}
