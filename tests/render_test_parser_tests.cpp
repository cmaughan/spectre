#include "support/test_support.h"
#include <draxul/json_util.h>
#include <draxul/render_test.h>

#include <filesystem>
#include <fstream>
#include <string>

using draxul::tests::expect;
using draxul::tests::expect_eq;
using draxul::tests::run_test;

namespace
{

void write_text_file(const std::filesystem::path& path, const std::string& text)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::trunc);
    out << text;
}

} // namespace

void run_render_test_parser_tests()
{
    // --- Scenario loading ---

    run_test("multiline commands array parses", [] {
        const auto dir = std::filesystem::temp_directory_path() / "draxul-render-test-parser-multi";
        const auto path = dir / "multiline.toml";
        write_text_file(path,
            "name = \"multiline\"\n"
            "width = 900\n"
            "height = 700\n"
            "debug_overlay = true\n"
            "enable_ligatures = false\n"
            "commands = [\n"
            "  \"edit ${PROJECT_ROOT}/README.md\",\n"
            "  \"set nowrap\",\n"
            "  \"lua vim.cmd([[normal! ggzt]])\",\n"
            "]\n");

        std::string err;
        auto scenario = draxul::load_render_test_scenario(path, &err);
        expect(scenario.has_value(), "multiline render scenario should load");
        expect_eq(scenario->name, std::string("multiline"), "name");
        expect_eq(scenario->width, 900, "width");
        expect_eq(scenario->height, 700, "height");
        expect(scenario->debug_overlay, "debug_overlay flag");
        expect(!scenario->enable_ligatures, "enable_ligatures flag");
        expect_eq(static_cast<int>(scenario->commands.size()), 3, "command count");
        expect_eq(scenario->commands[0],
            std::string("edit " + (std::filesystem::path{ DRAXUL_PROJECT_ROOT } / "README.md").lexically_normal().generic_string()),
            "project root placeholder expands");
        expect_eq(scenario->commands[1], std::string("set nowrap"), "second command");
        expect_eq(scenario->commands[2], std::string("lua vim.cmd([[normal! ggzt]])"), "third command");

        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    });

    run_test("missing commands field returns error", [] {
        const auto dir = std::filesystem::temp_directory_path() / "draxul-render-test-parser-nocmd";
        const auto path = dir / "nocmd.toml";
        write_text_file(path, "name = \"no-commands\"\nwidth = 800\n");

        std::string err;
        auto scenario = draxul::load_render_test_scenario(path, &err);
        expect(!scenario.has_value(), "missing commands should fail");
        expect(!err.empty(), "error message should be set");

        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    });

    run_test("hash comments are stripped in scenario files", [] {
        const auto dir = std::filesystem::temp_directory_path() / "draxul-render-test-parser-comment";
        const auto path = dir / "commented.toml";
        write_text_file(path,
            "# This is a comment\n"
            "width = 640 # inline comment\n"
            "height = 480\n"
            "commands = [\"echo\"]\n");

        std::string err;
        auto scenario = draxul::load_render_test_scenario(path, &err);
        expect(scenario.has_value(), "scenario with comments should load");
        expect_eq(scenario->width, 640, "width after stripping inline comment");
        expect_eq(scenario->height, 480, "height");

        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    });

    run_test("${PROJECT_ROOT} placeholder expands to the compile-time project root", [] {
        const auto dir = std::filesystem::temp_directory_path() / "draxul-render-test-parser-projroot";
        const auto path = dir / "projroot.toml";
        write_text_file(path,
            "font_path = \"${PROJECT_ROOT}/fonts/test.ttf\"\n"
            "commands = [\"echo\"]\n");

        std::string err;
        auto scenario = draxul::load_render_test_scenario(path, &err);
        expect(scenario.has_value(), "scenario should load");
        const auto expected = (std::filesystem::path{ DRAXUL_PROJECT_ROOT } / "fonts/test.ttf").lexically_normal().generic_string();
        expect_eq(scenario->font_path.lexically_normal().generic_string(), expected, "${PROJECT_ROOT} expands to project root");

        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    });

    run_test("invalid TOML scenario returns a parse error", [] {
        const auto dir = std::filesystem::temp_directory_path() / "draxul-render-test-parser-invalid";
        const auto path = dir / "invalid.toml";
        write_text_file(path,
            "width = 640\n"
            "commands = [\"echo\"\n");

        std::string err;
        auto scenario = draxul::load_render_test_scenario(path, &err);
        expect(!scenario.has_value(), "invalid TOML should fail");
        expect(!err.empty(), "parse error should be set");

        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    });

    run_test("host fields parse for powershell scenarios", [] {
        const auto dir = std::filesystem::temp_directory_path() / "draxul-render-test-parser-host";
        const auto path = dir / "host.toml";
        write_text_file(path,
            "host = \"powershell\"\n"
            "host_command = \"pwsh.exe\"\n"
            "host_args = [\"-NoLogo\", \"-NoProfile\"]\n"
            "commands = [\"Write-Host ready\"]\n");

        std::string err;
        auto scenario = draxul::load_render_test_scenario(path, &err);
        expect(scenario.has_value(), "powershell scenario should load");
        expect_eq(scenario->host_kind, draxul::HostKind::PowerShell, "host kind parses");
        expect_eq(scenario->host_command, std::string("pwsh.exe"), "host command parses");
        expect_eq(static_cast<int>(scenario->host_args.size()), 2, "host args count");
        expect_eq(scenario->host_args[0], std::string("-NoLogo"), "first host arg");
        expect_eq(scenario->host_args[1], std::string("-NoProfile"), "second host arg");

        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    });

    // --- JSON escaping ---

    run_test("json_escape_string escapes double quotes and backslashes", [] {
        expect_eq(draxul::json_escape_string("hello"), std::string("hello"), "plain passthrough");
        expect_eq(draxul::json_escape_string("say \"hi\""), std::string("say \\\"hi\\\""), "embedded quotes");
        expect_eq(draxul::json_escape_string("path\\file"), std::string("path\\\\file"), "backslash");
        expect_eq(draxul::json_escape_string("line1\nline2"), std::string("line1\\nline2"), "newline");
        expect_eq(draxul::json_escape_string("tab\there"), std::string("tab\\there"), "tab");
    });
}
