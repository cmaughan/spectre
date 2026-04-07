// Acceptance / regression guard for WI 102 (tab-keybinding-config-allowlist).
// Verifies that tab-management actions survive a TOML round-trip and that
// every action recognised at runtime is also accepted by the config parser.

#include <catch2/catch_test_macros.hpp>

#include "gui_action_handler.h"
#include <SDL3/SDL.h>
#include <draxul/app_config.h>
#include <draxul/gui_actions.h>
#include <draxul/input_types.h>
#include <draxul/keybinding_parser.h>

#include <string>
#include <string_view>
#include <vector>

using namespace draxul;

namespace
{

const GuiKeybinding* find_binding(const std::vector<GuiKeybinding>& bindings, std::string_view action)
{
    for (const auto& kb : bindings)
    {
        if (kb.action == action)
            return &kb;
    }
    return nullptr;
}

} // namespace

TEST_CASE("tab keybindings round-trip through TOML serialize/parse",
    "[config][keybinding][tab]")
{
    AppConfig original;

    // Replace defaults for the tab actions with custom combos so we can
    // verify the exact bindings survive the round-trip.
    auto set_binding = [&](std::string_view action, int32_t key, ModifierFlags mods) {
        for (auto& kb : original.keybindings)
        {
            if (kb.action == action)
            {
                kb.prefix_key = 0;
                kb.prefix_modifiers = kModNone;
                kb.key = key;
                kb.modifiers = mods;
                return;
            }
        }
        original.keybindings.push_back({ std::string(action), 0, kModNone, key, mods });
    };

    set_binding("new_tab", static_cast<int32_t>(SDLK_T), kModCtrl);
    set_binding("close_tab", static_cast<int32_t>(SDLK_W), kModCtrl);
    set_binding("next_tab", static_cast<int32_t>(SDLK_TAB), kModCtrl);
    set_binding("prev_tab", static_cast<int32_t>(SDLK_TAB), kModCtrl | kModShift);
    set_binding("activate_tab:3", static_cast<int32_t>(SDLK_3), kModCtrl);

    const std::string toml = original.serialize();

    // Custom action keys must appear in the serialized output.
    REQUIRE(toml.find("new_tab") != std::string::npos);
    REQUIRE(toml.find("close_tab") != std::string::npos);
    REQUIRE(toml.find("next_tab") != std::string::npos);
    REQUIRE(toml.find("prev_tab") != std::string::npos);
    REQUIRE(toml.find("activate_tab:3") != std::string::npos);

    const AppConfig parsed = AppConfig::parse(toml);

    auto require_binding = [&](std::string_view action, int32_t key, ModifierFlags mods) {
        const GuiKeybinding* kb = find_binding(parsed.keybindings, action);
        INFO("missing binding after round-trip: " << action);
        REQUIRE(kb != nullptr);
        REQUIRE(kb->key == key);
        REQUIRE(kb->modifiers == mods);
        REQUIRE(kb->prefix_key == 0);
    };

    require_binding("new_tab", static_cast<int32_t>(SDLK_T), kModCtrl);
    require_binding("close_tab", static_cast<int32_t>(SDLK_W), kModCtrl);
    require_binding("next_tab", static_cast<int32_t>(SDLK_TAB), kModCtrl);
    require_binding("prev_tab", static_cast<int32_t>(SDLK_TAB), kModCtrl | kModShift);
    require_binding("activate_tab:3", static_cast<int32_t>(SDLK_3), kModCtrl);
}

TEST_CASE("tab keybinding parser accepts new_tab as a config key", "[config][keybinding][tab]")
{
    const std::string toml = "[keybindings]\n"
                             "new_tab = \"Ctrl+T\"\n";
    const AppConfig parsed = AppConfig::parse(toml);
    const GuiKeybinding* kb = find_binding(parsed.keybindings, "new_tab");
    REQUIRE(kb != nullptr);
    REQUIRE(kb->key == static_cast<int32_t>(SDLK_T));
    REQUIRE(kb->modifiers == kModCtrl);
}

TEST_CASE("every runtime GUI action is recognised by the config-key validator",
    "[config][keybinding][parity]")
{
    // Build the set of all action names exposed by the dispatch table.
    auto runtime = GuiActionHandler::action_names();
    REQUIRE_FALSE(runtime.empty());

    for (std::string_view name : runtime)
    {
        // Tab-indexed actions ("activate_tab") are exposed at runtime as the
        // bare name; the config validator only accepts the suffixed form.
        const GuiActionInfo* info = find_gui_action(name);
        INFO("runtime action missing from canonical registry: " << name);
        REQUIRE(info != nullptr);

        if (info->tab_indexed)
        {
            for (int i = 1; i <= kGuiActionMaxTabIndex; ++i)
            {
                const std::string key = std::string(name) + ":" + std::to_string(i);
                INFO("tab-indexed config key rejected: " << key);
                REQUIRE(is_known_gui_action_config_key(key));
            }
            // Bare tab-indexed name must be rejected.
            REQUIRE_FALSE(is_known_gui_action_config_key(name));
        }
        else
        {
            INFO("config key rejected for runtime action: " << name);
            REQUIRE(is_known_gui_action_config_key(name));
        }
    }
}
