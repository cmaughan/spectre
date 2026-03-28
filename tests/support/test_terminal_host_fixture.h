#pragma once

#include "fake_renderer.h"
#include "fake_window.h"
#include "test_host_callbacks.h"

#include <draxul/host.h>
#include <draxul/renderer.h>
#include <draxul/text_service.h>
#include <draxul/window.h>

#include <filesystem>

namespace draxul::tests
{

template <typename HostT>
struct TerminalHostFixture
{
    FakeWindow window;
    FakeTermRenderer renderer;
    TextService text_service;
    HostT host;
    TestHostCallbacks callbacks;
    bool ok = false;

    explicit TerminalHostFixture(int cols = 20, int rows = 5)
    {
        host.cols_ = cols;
        host.rows_ = rows;

        TextServiceConfig ts_cfg;
        ts_cfg.font_path
            = (std::filesystem::path(DRAXUL_PROJECT_ROOT)
                / "fonts"
                / "JetBrainsMonoNerdFont-Regular.ttf")
                  .string();
        text_service.initialize(ts_cfg, TextService::DEFAULT_POINT_SIZE, 96.0f);

        HostViewport vp;
        vp.grid_size = { cols, rows };

        HostContext ctx{ &window, &renderer, &text_service, {}, vp, 96.0f };
        ok = host.initialize(ctx, callbacks);
    }
};

} // namespace draxul::tests
