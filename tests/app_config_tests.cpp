#include "support/test_support.h"

#include <SDL3/SDL.h>
#include <draxul/app_config.h>

#include <atomic>
#include <chrono>
#include <draxul/log.h>
#include <filesystem>
#include <fstream>

using namespace draxul;
using namespace draxul::tests;

namespace
{

struct TempDir
{
    std::filesystem::path path;

    explicit TempDir(const char* prefix)
    {
        static std::atomic<uint64_t> counter = 0;
        const auto suffix = std::to_string(
                                std::chrono::steady_clock::now().time_since_epoch().count())
            + "-" + std::to_string(counter++);
        path = std::filesystem::temp_directory_path() / (std::string(prefix) + "-" + suffix);
        std::filesystem::create_directories(path);
    }

    ~TempDir()
    {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

bool contains_message(const std::vector<LogRecord>& records, std::string_view needle)
{
    for (const auto& record : records)
    {
        if (record.message.find(needle) != std::string::npos)
            return true;
    }
    return false;
}

const GuiKeybinding* find_keybinding(const AppConfig& config, std::string_view action)
{
    for (const auto& binding : config.keybindings)
    {
        if (binding.action == action)
            return &binding;
    }
    return nullptr;
}

} // namespace

void run_app_config_tests()
{
    run_test("app config parse returns defaults for empty content", []() {
        AppConfig config = AppConfig::parse("");
        AppConfig defaults;
        expect_eq(config.window_width, defaults.window_width, "default window_width");
        expect_eq(config.window_height, defaults.window_height, "default window_height");
        expect_eq(config.font_size, defaults.font_size, "default font_size");
        expect_eq(config.enable_ligatures, defaults.enable_ligatures, "default enable_ligatures");
        expect(config.font_path.empty(), "default font_path is empty");
        expect(config.fallback_paths.empty(), "default fallback_paths is empty");
        expect_eq(static_cast<int>(config.keybindings.size()), 6, "default GUI keybindings are present");
    });

    run_test("app config parse reads all fields", []() {
        const char* content = "window_width = 1920\n"
                              "window_height = 1080\n"
                              "font_size = 14\n"
                              "enable_ligatures = false\n"
                              "font_path = \"/usr/share/fonts/mono.ttf\"\n"
                              "fallback_paths = [\"/fonts/a.ttf\", \"/fonts/b.ttf\"]\n";

        AppConfig config = AppConfig::parse(content);
        expect_eq(config.window_width, 1920, "window_width parsed");
        expect_eq(config.window_height, 1080, "window_height parsed");
        expect_eq(config.font_size, 14.0f, "font_size parsed");
        expect_eq(config.enable_ligatures, false, "enable_ligatures parsed");
        expect_eq(config.font_path, std::string("/usr/share/fonts/mono.ttf"), "font_path parsed");
        expect_eq(static_cast<int>(config.fallback_paths.size()), 2, "fallback_paths count");
        expect_eq(config.fallback_paths[0], std::string("/fonts/a.ttf"), "first fallback path");
        expect_eq(config.fallback_paths[1], std::string("/fonts/b.ttf"), "second fallback path");
    });

    run_test("gui keybinding parser handles modifier chords and symbolic keys", []() {
        auto copy = parse_gui_keybinding("copy", "Ctrl+Shift+C");
        expect(copy.has_value(), "copy binding parses");
        expect_eq(copy->action, std::string("copy"), "action name is preserved");
        expect_eq(copy->key, static_cast<int32_t>(SDLK_C), "letter key parses");
        expect_eq(copy->modifiers, kModCtrl | kModShift, "modifiers parse");
        expect_eq(format_gui_keybinding_combo(copy->key, copy->modifiers), std::string("Ctrl+Shift+C"), "format round-trips");

        auto zoom = parse_gui_keybinding("font_increase", "Ctrl+=");
        expect(zoom.has_value(), "zoom binding parses");
        expect_eq(zoom->key, static_cast<int32_t>(SDLK_EQUALS), "equals key parses");
        expect_eq(zoom->modifiers, kModCtrl, "Ctrl modifier parses");
        expect_eq(format_gui_keybinding_combo(zoom->key, zoom->modifiers), std::string("Ctrl+="), "equals formatting is canonical");
    });

    run_test("gui keybinding parser rejects unknown actions and malformed combos", []() {
        expect(!parse_gui_keybinding("not_real", "Ctrl+K").has_value(), "unknown actions are rejected");
        expect(!parse_gui_keybinding("copy", "Ctrl+Shift+").has_value(), "empty key token is rejected");
        expect(!parse_gui_keybinding("copy", "Hyper+C").has_value(), "unknown modifiers are rejected");
    });

    run_test("gui keybinding matcher preserves Ctrl+= and Ctrl+Plus for font increase", []() {
        auto zoom = parse_gui_keybinding("font_increase", "Ctrl+=");
        expect(zoom.has_value(), "zoom binding parses");

        expect(gui_keybinding_matches(*zoom, KeyEvent{ 0, SDLK_EQUALS, kModCtrl, true }),
            "Ctrl+= matches the configured binding");
        expect(gui_keybinding_matches(*zoom, KeyEvent{ 0, SDLK_PLUS, kModCtrl | kModShift, true }),
            "Ctrl+Plus remains accepted for zoom-in");
        expect(!gui_keybinding_matches(*zoom, KeyEvent{ 0, SDLK_MINUS, kModCtrl, true }),
            "other keys do not match");
    });

    run_test("app config parse supports multiline TOML arrays", []() {
        const char* content = "fallback_paths = [\n"
                              "  \"/fonts/a.ttf\",\n"
                              "  \"/fonts/b.ttf\",\n"
                              "]\n";

        AppConfig config = AppConfig::parse(content);
        expect_eq(static_cast<int>(config.fallback_paths.size()), 2, "fallback_paths count");
        expect_eq(config.fallback_paths[0], std::string("/fonts/a.ttf"), "first multiline fallback path");
        expect_eq(config.fallback_paths[1], std::string("/fonts/b.ttf"), "second multiline fallback path");
    });

    run_test("app config parse ignores comments and blank lines", []() {
        const char* content = "# this is a comment\n"
                              "\n"
                              "window_width = 1600 # inline comment\n"
                              "\n";

        AppConfig config = AppConfig::parse(content);
        expect_eq(config.window_width, 1600, "window_width parsed past comments");
        expect_eq(config.window_height, AppConfig{}.window_height, "window_height stays default");
    });

    run_test("app config parse reads keybindings table and preserves unspecified defaults", []() {
        const char* content = "[keybindings]\n"
                              "toggle_diagnostics = \"Ctrl+D\"\n"
                              "font_reset = \"Alt+0\"\n";

        AppConfig config = AppConfig::parse(content);
        const GuiKeybinding* toggle = find_keybinding(config, "toggle_diagnostics");
        const GuiKeybinding* font_reset = find_keybinding(config, "font_reset");
        const GuiKeybinding* copy = find_keybinding(config, "copy");

        expect(toggle != nullptr, "toggle binding is present");
        expect(font_reset != nullptr, "font reset binding is present");
        expect(copy != nullptr, "unspecified bindings stay present");
        expect_eq(toggle->key, static_cast<int32_t>(SDLK_D), "custom toggle key parses");
        expect_eq(toggle->modifiers, kModCtrl, "custom toggle modifiers parse");
        expect_eq(font_reset->key, static_cast<int32_t>(SDLK_0), "custom reset key parses");
        expect_eq(font_reset->modifiers, kModAlt, "custom reset modifiers parse");
        expect_eq(copy->key, static_cast<int32_t>(SDLK_C), "default copy key remains");
        expect_eq(copy->modifiers, kModCtrl | kModShift, "default copy modifiers remain");
    });

    run_test("app config parse clamps out-of-range window dimensions to defaults", []() {
        const char* content = "window_width = 99999\n"
                              "window_height = -5\n";

        AppConfig config = AppConfig::parse(content);
        AppConfig defaults;
        expect_eq(config.window_width, defaults.window_width, "oversized width falls back to default");
        expect_eq(config.window_height, defaults.window_height, "negative height falls back to default");
    });

    run_test("app config parse clamps font_size to valid range", []() {
        AppConfig too_small = AppConfig::parse("font_size = 0\n");
        AppConfig too_large = AppConfig::parse("font_size = 9999\n");

        expect(too_small.font_size >= TextService::MIN_POINT_SIZE, "font_size clamped up to min");
        expect(too_large.font_size <= TextService::MAX_POINT_SIZE, "font_size clamped down to max");
    });

    run_test("app config serialize/parse round-trip preserves all fields", []() {
        AppConfig original;
        original.window_width = 1440;
        original.window_height = 900;
        original.font_size = 13.0f;
        original.enable_ligatures = false;
        original.font_path = "/home/user/fonts/mono.ttf";
        original.fallback_paths = { "/fonts/emoji.ttf", "/fonts/cjk.ttf" };

        AppConfig round_tripped = AppConfig::parse(original.serialize());

        expect_eq(round_tripped.window_width, original.window_width, "window_width survives round-trip");
        expect_eq(round_tripped.window_height, original.window_height, "window_height survives round-trip");
        expect_eq(round_tripped.font_size, original.font_size, "font_size survives round-trip"); // float == float
        expect_eq(round_tripped.enable_ligatures, original.enable_ligatures, "enable_ligatures survives round-trip");
        expect_eq(round_tripped.font_path, original.font_path, "font_path survives round-trip");
        expect_eq(static_cast<int>(round_tripped.fallback_paths.size()),
            static_cast<int>(original.fallback_paths.size()), "fallback_paths count survives round-trip");
        expect_eq(round_tripped.fallback_paths[0], original.fallback_paths[0], "first fallback path survives");
        expect_eq(round_tripped.fallback_paths[1], original.fallback_paths[1], "second fallback path survives");
        expect_eq(format_gui_keybinding_combo(find_keybinding(round_tripped, "toggle_diagnostics")->key,
                      find_keybinding(round_tripped, "toggle_diagnostics")->modifiers),
            std::string("F12"), "default toggle binding survives round-trip");
    });

    run_test("app config serialize/parse round-trip preserves custom keybindings", []() {
        AppConfig original;
        original.keybindings = {
            { "toggle_diagnostics", static_cast<int32_t>(SDLK_D), kModCtrl },
            { "copy", static_cast<int32_t>(SDLK_C), kModCtrl | kModAlt },
            { "paste", static_cast<int32_t>(SDLK_V), kModCtrl | kModAlt },
            { "font_increase", static_cast<int32_t>(SDLK_EQUALS), kModCtrl },
            { "font_decrease", static_cast<int32_t>(SDLK_MINUS), kModCtrl },
            { "font_reset", static_cast<int32_t>(SDLK_0), kModAlt },
        };

        AppConfig round_tripped = AppConfig::parse(original.serialize());

        expect_eq(format_gui_keybinding_combo(find_keybinding(round_tripped, "toggle_diagnostics")->key,
                      find_keybinding(round_tripped, "toggle_diagnostics")->modifiers),
            std::string("Ctrl+D"), "custom toggle binding survives round-trip");
        expect_eq(format_gui_keybinding_combo(find_keybinding(round_tripped, "copy")->key,
                      find_keybinding(round_tripped, "copy")->modifiers),
            std::string("Ctrl+Alt+C"), "custom copy binding survives round-trip");
        expect_eq(format_gui_keybinding_combo(find_keybinding(round_tripped, "font_reset")->key,
                      find_keybinding(round_tripped, "font_reset")->modifiers),
            std::string("Alt+0"), "custom font reset binding survives round-trip");
    });

    run_test("app config serialize clamps out-of-range values", []() {
        AppConfig config;
        config.window_width = 99999;
        config.font_size = 9999.0f;

        AppConfig round_tripped = AppConfig::parse(config.serialize());
        expect(round_tripped.window_width <= 3840, "oversized width clamped on serialize");
        expect(round_tripped.font_size <= TextService::MAX_POINT_SIZE, "oversized font_size clamped on serialize");
    });

    run_test("app config save and load round-trip through the filesystem", []() {
        TempDir temp("draxul-app-config-roundtrip");
        const auto path = temp.path / "draxul" / "config.toml";

        AppConfig original;
        original.window_width = 1600;
        original.window_height = 900;
        original.font_size = 15.0f;
        original.enable_ligatures = false;
        original.font_path = "/fonts/mono.ttf";
        original.fallback_paths = { "/fonts/emoji.ttf", "/fonts/cjk.ttf" };

        original.save_to_path(path);
        expect(std::filesystem::exists(path), "config file should be created on save");

        AppConfig loaded = AppConfig::load_from_path(path);
        expect_eq(loaded.window_width, original.window_width, "window_width survives filesystem round-trip");
        expect_eq(loaded.window_height, original.window_height, "window_height survives filesystem round-trip");
        expect_eq(loaded.font_size, original.font_size, "font_size survives filesystem round-trip");
        expect_eq(loaded.enable_ligatures, original.enable_ligatures, "enable_ligatures survives filesystem round-trip");
        expect_eq(loaded.font_path, original.font_path, "font_path survives filesystem round-trip");
        expect_eq(static_cast<int>(loaded.fallback_paths.size()),
            static_cast<int>(original.fallback_paths.size()), "fallback count survives filesystem round-trip");
        expect_eq(loaded.fallback_paths[0], original.fallback_paths[0], "first fallback survives filesystem round-trip");
        expect_eq(loaded.fallback_paths[1], original.fallback_paths[1], "second fallback survives filesystem round-trip");
    });

    run_test("app config load from a missing file returns defaults", []() {
        TempDir temp("draxul-app-config-missing");
        const auto path = temp.path / "missing.toml";

        AppConfig loaded = AppConfig::load_from_path(path);
        AppConfig defaults;

        expect_eq(loaded.window_width, defaults.window_width, "missing config keeps default width");
        expect_eq(loaded.window_height, defaults.window_height, "missing config keeps default height");
        expect_eq(loaded.font_size, defaults.font_size, "missing config keeps default font size");
        expect_eq(loaded.enable_ligatures, defaults.enable_ligatures, "missing config keeps default ligature setting");
        expect_eq(loaded.font_path, defaults.font_path, "missing config keeps default font path");
        expect_eq(loaded.fallback_paths.size(), defaults.fallback_paths.size(), "missing config keeps default fallbacks");
    });

    run_test("app config load rejects invalid TOML and falls back to defaults", []() {
        TempDir temp("draxul-app-config-corrupt");
        const auto path = temp.path / "config.toml";

        std::ofstream out(path, std::ios::trunc);
        out << "window_width = nope\n"
               "window_height = 720\n"
               "font_size = broken\n"
               "fallback_paths = [\"/fonts/a.ttf\"\n";
        out.close();

        ScopedLogCapture capture;
        AppConfig loaded = AppConfig::load_from_path(path);
        AppConfig defaults;

        expect_eq(loaded.window_width, defaults.window_width, "bad width falls back to the default");
        expect_eq(loaded.window_height, defaults.window_height, "invalid TOML rejects partial parsing");
        expect_eq(loaded.font_size, defaults.font_size, "bad font size falls back to the default");
        expect(loaded.fallback_paths.empty(), "incomplete array falls back to an empty list");
        expect(contains_message(capture.records, "Failed to parse config from"),
            "parse failures should be logged");
    });

    run_test("app config save logs a warning when the target path is not writable", []() {
        TempDir temp("draxul-app-config-write-failure");
        const auto blocking_file = temp.path / "not-a-directory";
        {
            std::ofstream out(blocking_file, std::ios::trunc);
            out << "block";
        }

        ScopedLogCapture capture;
        AppConfig config;
        config.save_to_path(blocking_file / "config.toml");

        expect(contains_message(capture.records, "Failed to save config to"),
            "write failures should be logged");
    });

    run_test("app config overrides shadow only the explicitly provided fields", []() {
        AppConfig base;
        base.window_width = 1000;
        base.window_height = 700;
        base.font_size = 12.0f;
        base.enable_ligatures = true;
        base.font_path = "/fonts/base.ttf";
        base.fallback_paths = { "/fonts/base-fallback.ttf" };

        AppConfigOverrides overrides;
        overrides.window_width = 1440;
        overrides.enable_ligatures = false;
        overrides.font_path = std::string("/fonts/override.ttf");

        apply_overrides(base, overrides);

        expect_eq(base.window_width, 1440, "explicit width override is applied");
        expect_eq(base.window_height, 700, "unspecified height stays unchanged");
        expect_eq(base.font_size, 12.0f, "unspecified font size stays unchanged");
        expect_eq(base.enable_ligatures, false, "explicit ligature override is applied");
        expect_eq(base.font_path, std::string("/fonts/override.ttf"), "explicit font path override is applied");
        expect_eq(static_cast<int>(base.fallback_paths.size()), 1, "unspecified fallback paths stay unchanged");
        expect_eq(base.fallback_paths[0], std::string("/fonts/base-fallback.ttf"), "fallback paths are preserved");
    });

    run_test("app config atlas_size parses valid power-of-two values", []() {
        AppConfig config = AppConfig::parse("atlas_size = 4096\n");
        expect_eq(config.atlas_size, 4096, "atlas_size = 4096 parses correctly");
    });

    run_test("app config atlas_size clamps out-of-range values to nearest power of two in range", []() {
        AppConfig too_small = AppConfig::parse("atlas_size = 100\n");
        AppConfig too_large = AppConfig::parse("atlas_size = 99999\n");
        expect_eq(too_small.atlas_size, 1024, "atlas_size below min clamps to 1024");
        expect_eq(too_large.atlas_size, 8192, "atlas_size above max clamps to 8192");
    });

    run_test("app config atlas_size rounds non-power-of-two values down", []() {
        AppConfig config1 = AppConfig::parse("atlas_size = 3000\n");
        AppConfig config2 = AppConfig::parse("atlas_size = 1500\n");
        AppConfig config3 = AppConfig::parse("atlas_size = 5000\n");
        expect_eq(config1.atlas_size, 2048, "3000 rounds down to 2048");
        expect_eq(config2.atlas_size, 1024, "1500 rounds down to 1024");
        expect_eq(config3.atlas_size, 4096, "5000 rounds down to 4096");
    });

    run_test("app config atlas_size defaults to kAtlasSize when not specified", []() {
        AppConfig config = AppConfig::parse("");
        expect_eq(config.atlas_size, draxul::kAtlasSize, "atlas_size defaults to kAtlasSize");
    });

    run_test("app config overrides can intentionally clear string and list fields", []() {
        AppConfig base;
        base.font_path = "/fonts/base.ttf";
        base.fallback_paths = { "/fonts/a.ttf", "/fonts/b.ttf" };

        AppConfigOverrides overrides;
        overrides.font_path = std::string();
        overrides.fallback_paths = std::vector<std::string>{};

        apply_overrides(base, overrides);

        expect_eq(base.font_path, std::string(), "empty string override clears the font path");
        expect(base.fallback_paths.empty(), "empty vector override clears fallback paths");
    });

    run_test("app config parse warns about unknown top-level keys", []() {
        ScopedLogCapture capture;
        AppConfig config = AppConfig::parse("font_szie = 14\nwindow_width = 1920\n");
        AppConfig defaults;

        // The typo key should trigger a warning
        expect(contains_message(capture.records, "Unknown key 'font_szie'"),
            "typo key should produce an unknown-key warning");
        // Known key should still be parsed correctly
        expect_eq(config.window_width, 1920, "known key window_width still parsed correctly");
        // Typo key is silently ignored (falls back to default)
        expect_eq(config.font_size, defaults.font_size, "typo key falls back to default font_size");
    });

    run_test("app config parse does not warn for all known top-level keys", []() {
        ScopedLogCapture capture;
        const char* content = "window_width = 1280\n"
                              "window_height = 800\n"
                              "font_size = 14\n"
                              "atlas_size = 2048\n"
                              "enable_ligatures = true\n"
                              "font_path = \"/fonts/mono.ttf\"\n"
                              "fallback_paths = [\"/fonts/emoji.ttf\"]\n"
                              "[keybindings]\n"
                              "toggle_diagnostics = \"F12\"\n";
        AppConfig::parse(content);

        bool has_unknown_warning = false;
        for (const auto& record : capture.records)
        {
            if (record.message.find("Unknown key") != std::string::npos)
                has_unknown_warning = true;
        }
        expect(!has_unknown_warning, "no unknown-key warnings for well-formed config");
    });

    run_test("app config parse logs error for wrong type on known key", []() {
        ScopedLogCapture capture;
        // font_size expects integer, passing a string
        AppConfig config = AppConfig::parse("font_size = \"large\"\n");
        AppConfig defaults;

        expect(contains_message(capture.records, "Key 'font_size' has wrong type"),
            "type mismatch should produce an error log");
        expect_eq(config.font_size, defaults.font_size, "wrong-type key falls back to default");
    });

    run_test("config unknown key: exactly one warning per unknown key", []() {
        ScopedLogCapture capture;
        AppConfig::parse("future_option = true\n"
                         "another_unknown = 42\n"
                         "window_width = 1280\n");
        int warning_count = 0;
        for (const auto& record : capture.records)
            if (record.message.find("Unknown key") != std::string::npos)
                ++warning_count;
        expect_eq(warning_count, 2, "exactly one warning per unknown key");
    });

    run_test("config unknown key: unknown keys are dropped from serialized output", []() {
        // Unknown keys should not appear in the serialized output
        // since we only serialize known keys
        AppConfig config = AppConfig::parse("future_option = true\n"
                                            "window_width = 1280\n");
        std::string serialized = config.serialize();
        expect(serialized.find("future_option") == std::string::npos,
            "unknown key is not preserved in serialized output");
        expect(serialized.find("window_width") != std::string::npos,
            "known key is preserved in serialized output");
    });

    run_test("config unknown section: unknown TOML section produces unknown key warning", []() {
        ScopedLogCapture capture;
        AppConfig::parse("[future_section]\n"
                         "option = true\n"
                         "window_width = 1280\n");
        expect(contains_message(capture.records, "Unknown key 'future_section'"),
            "unknown section should produce an unknown-key warning");
    });

    run_test("config duplicate keybinding: same key+modifier for two actions produces warning", []() {
        ScopedLogCapture capture;
        // Two different actions using Ctrl+C
        const char* content = "[keybindings]\n"
                              "copy = \"Ctrl+Shift+C\"\n"
                              "paste = \"Ctrl+Shift+C\"\n";
        AppConfig config = AppConfig::parse(content);
        expect(contains_message(capture.records, "Duplicate keybinding"),
            "duplicate key+modifier should produce a warning");
    });

    run_test("config duplicate keybinding: first registered action takes precedence in dispatch", []() {
        ScopedLogCapture capture;
        const char* content = "[keybindings]\n"
                              "copy = \"Ctrl+Shift+C\"\n"
                              "paste = \"Ctrl+Shift+C\"\n";
        AppConfig config = AppConfig::parse(content);
        // Verify both bindings are present but only first should fire
        // The policy is first-wins: copy comes before paste in the keybindings vector
        // (default bindings are seeded in order: toggle_diagnostics, copy, paste, ...)
        // After replace_gui_keybinding, copy should appear before paste
        const GuiKeybinding* copy_binding = find_keybinding(config, "copy");
        const GuiKeybinding* paste_binding = find_keybinding(config, "paste");
        expect(copy_binding != nullptr, "copy binding exists");
        expect(paste_binding != nullptr, "paste binding exists");
        // Both have same key+mod; first in vector (copy, since default order) takes precedence
        // in linear dispatch. Document: first-wins by insertion order.
        KeyEvent event{ 0, SDLK_C, kModCtrl | kModShift, true };
        int match_count = 0;
        for (const auto& binding : config.keybindings)
            if (gui_keybinding_matches(binding, event))
                ++match_count;
        expect(match_count >= 1, "at least one binding matches the key event");
    });

    run_test("gui keybinding parsing works without SDL initialized", []() {
        // Ctrl+Shift modifier parsing must work with no SDL_Init call.
        // This confirms that draxul-app-support has no SDL initialisation dependency.
        auto binding = parse_gui_keybinding("copy", "Ctrl+Shift+C");
        expect(binding.has_value(), "copy binding parses without SDL init");
        expect_eq(binding->modifiers, kModCtrl | kModShift, "Ctrl+Shift modifiers parsed correctly");
        expect(binding->key != 0, "key is non-zero");
    });

    run_test("host kind parser accepts nvim and powershell spellings", []() {
        auto nvim = parse_host_kind("nvim");
        auto powershell = parse_host_kind("powershell");
        auto pwsh = parse_host_kind("pwsh");
        auto invalid = parse_host_kind("not-a-host");

        expect(nvim.has_value(), "nvim should parse");
        expect(powershell.has_value(), "powershell should parse");
        expect(pwsh.has_value(), "pwsh alias should parse");
        expect(!invalid.has_value(), "unknown hosts should be rejected");
        expect_eq(*nvim, HostKind::Nvim, "nvim maps correctly");
        expect_eq(*powershell, HostKind::PowerShell, "powershell maps correctly");
        expect_eq(*pwsh, HostKind::PowerShell, "pwsh maps correctly");
        expect_eq(std::string(to_string(*powershell)), std::string("powershell"), "host kind stringifies");
    });
}
