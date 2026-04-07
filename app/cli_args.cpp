#include "cli_args.h"

#include <exception>
#include <string>

namespace draxul
{

ParseArgsResult parse_args(const std::vector<std::string>& args)
{
    ParseArgsResult result;
    ParsedArgs& parsed = result.args;

    for (size_t i = 1; i < args.size(); ++i)
    {
        if (args[i] == "--console")
            parsed.want_console = true;
        else if (args[i] == "--smoke-test")
            parsed.smoke_test = true;
        else if (args[i] == "--continuous-refresh")
            parsed.continuous_refresh = true;
        else if (args[i] == "--no-vblank")
            parsed.no_vblank = true;
        else if (args[i] == "--no-ui")
            parsed.no_ui = true;
#ifdef DRAXUL_ENABLE_RENDER_TESTS
        else if (args[i] == "--bless-render-test")
            parsed.bless_render_test = true;
        else if (args[i] == "--show-render-test-window")
            parsed.show_render_test_window = true;
        else if (args[i] == "--render-test" && i + 1 < args.size())
        {
            ++i;
            parsed.render_test_path = args[i];
        }
        else if (args[i] == "--export-render-test" && i + 1 < args.size())
        {
            ++i;
            parsed.export_render_test_path = args[i];
        }
#endif
        else if (args[i] == "--host" && i + 1 < args.size())
        {
            ++i;
            parsed.host_kind = parse_host_kind(args[i]);
        }
        else if (args[i] == "--command" && i + 1 < args.size())
        {
            ++i;
            parsed.host_command = args[i];
        }
        else if (args[i] == "--source" && i + 1 < args.size())
        {
            ++i;
            parsed.host_source_path = args[i];
        }
        else if (args[i] == "--log-file" && i + 1 < args.size())
        {
            ++i;
            parsed.log_file = args[i];
        }
        else if (args[i] == "--log-level" && i + 1 < args.size())
        {
            ++i;
            parsed.log_level = args[i];
        }
        else if (args[i] == "--screenshot" && i + 1 < args.size())
        {
            ++i;
            parsed.screenshot_path = args[i];
        }
        else if (args[i] == "--screenshot-delay" && i + 1 < args.size())
        {
            ++i;
            try
            {
                size_t consumed = 0;
                int value = std::stoi(args[i], &consumed);
                if (consumed != args[i].size() || value < 0)
                {
                    result.error = "error: --screenshot-delay requires a non-negative integer";
                    return result;
                }
                parsed.screenshot_delay_ms = value;
            }
            catch (const std::exception&)
            {
                result.error = "error: --screenshot-delay requires a non-negative integer";
                return result;
            }
        }
        else if (args[i] == "--screenshot-size" && i + 1 < args.size())
        {
            ++i;
            const auto& size_str = args[i];
            auto x_pos = size_str.find('x');
            if (x_pos == std::string::npos)
            {
                result.error = "error: --screenshot-size requires valid dimensions (e.g. 800x600)";
                return result;
            }
            try
            {
                const std::string width_str = size_str.substr(0, x_pos);
                const std::string height_str = size_str.substr(x_pos + 1);
                size_t w_consumed = 0;
                size_t h_consumed = 0;
                parsed.screenshot_width = std::stoi(width_str, &w_consumed);
                parsed.screenshot_height = std::stoi(height_str, &h_consumed);
                if (w_consumed != width_str.size() || h_consumed != height_str.size())
                {
                    result.error = "error: --screenshot-size requires valid dimensions (e.g. 800x600)";
                    return result;
                }
                if (parsed.screenshot_width <= 0 || parsed.screenshot_height <= 0)
                {
                    result.error = "error: --screenshot-size requires positive dimensions (e.g. 800x600)";
                    return result;
                }
            }
            catch (const std::exception&)
            {
                result.error = "error: --screenshot-size requires valid dimensions (e.g. 800x600)";
                return result;
            }
        }
    }
    return result;
}

} // namespace draxul
