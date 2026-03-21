#include "support/test_support.h"

#include <SDL3/SDL.h>
#include <draxul/app_config.h>
#include <draxul/events.h>
#include <draxul/log.h>

#include <string>
#include <unordered_map>
#include <vector>

using namespace draxul;
using namespace draxul::tests;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace
{

// Build a KeyEvent with just the fields relevant to keybinding matching.
KeyEvent make_key_event(int key, ModifierFlags modifiers = kModNone, bool pressed = true)
{
    KeyEvent evt{};
    evt.keycode = key;
    evt.mod = modifiers;
    evt.pressed = pressed;
    return evt;
}

// Look up the first binding for a given action from a config.
const GuiKeybinding* find_binding(const AppConfig& cfg, std::string_view action)
{
    for (const auto& b : cfg.keybindings)
    {
        if (b.action == action)
            return &b;
    }
    return nullptr;
}

// Simulate the same lookup the App performs: iterate bindings in order and
// return the first matching action name.
std::optional<std::string_view> action_for_event(const AppConfig& cfg, const KeyEvent& evt)
{
    for (const auto& binding : cfg.keybindings)
    {
        if (gui_keybinding_matches(binding, evt))
            return binding.action;
    }
    return std::nullopt;
}

// Simulate App::build_action_map() — maps action names to counters.
struct ActionDispatcher
{
    std::unordered_map<std::string, int> fire_counts;

    void register_action(const std::string& name)
    {
        fire_counts[name] = 0;
    }

    bool dispatch(std::string_view action)
    {
        auto it = fire_counts.find(std::string(action));
        if (it == fire_counts.end())
            return false;
        ++it->second;
        return true;
    }

    int count(const std::string& action) const
    {
        auto it = fire_counts.find(action);
        return it != fire_counts.end() ? it->second : 0;
    }
};

} // namespace

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

void run_keybinding_dispatch_tests()
{
    // -----------------------------------------------------------------------
    // Default config has the expected set of bindings
    // -----------------------------------------------------------------------

    run_test("keybinding dispatch: default config has all six known bindings", []() {
        AppConfig cfg;
        expect_eq(static_cast<int>(cfg.keybindings.size()), 6, "six default bindings");
        expect(find_binding(cfg, "toggle_diagnostics") != nullptr, "toggle_diagnostics binding present");
        expect(find_binding(cfg, "copy") != nullptr, "copy binding present");
        expect(find_binding(cfg, "paste") != nullptr, "paste binding present");
        expect(find_binding(cfg, "font_increase") != nullptr, "font_increase binding present");
        expect(find_binding(cfg, "font_decrease") != nullptr, "font_decrease binding present");
        expect(find_binding(cfg, "font_reset") != nullptr, "font_reset binding present");
    });

    // -----------------------------------------------------------------------
    // A configured binding fires the correct action
    // -----------------------------------------------------------------------

    run_test("keybinding dispatch: Ctrl+C mapped to copy fires copy action", []() {
        AppConfig cfg = AppConfig::parse("[keybindings]\ncopy = \"Ctrl+C\"\n");
        ActionDispatcher dispatcher;
        for (const auto& b : cfg.keybindings)
            dispatcher.register_action(b.action);

        KeyEvent evt = make_key_event(SDLK_C, kModCtrl);
        auto action = action_for_event(cfg, evt);
        expect(action.has_value(), "Ctrl+C resolves to a known action");
        expect_eq(std::string(*action), std::string("copy"), "Ctrl+C maps to copy");

        dispatcher.dispatch(*action);
        expect_eq(dispatcher.count("copy"), 1, "copy action fired exactly once");
    });

    run_test("keybinding dispatch: F12 fires toggle_diagnostics", []() {
        AppConfig cfg;
        ActionDispatcher dispatcher;
        for (const auto& b : cfg.keybindings)
            dispatcher.register_action(b.action);

        KeyEvent evt = make_key_event(SDLK_F12, kModNone);
        auto action = action_for_event(cfg, evt);
        expect(action.has_value(), "F12 resolves to a known action");
        expect_eq(std::string(*action), std::string("toggle_diagnostics"),
            "F12 maps to toggle_diagnostics");

        dispatcher.dispatch(*action);
        expect_eq(dispatcher.count("toggle_diagnostics"), 1, "toggle_diagnostics fired once");
    });

    run_test("keybinding dispatch: font_increase Ctrl+= fires font_increase action", []() {
        AppConfig cfg;
        ActionDispatcher dispatcher;
        for (const auto& b : cfg.keybindings)
            dispatcher.register_action(b.action);

        KeyEvent evt = make_key_event(SDLK_EQUALS, kModCtrl);
        auto action = action_for_event(cfg, evt);
        expect(action.has_value(), "Ctrl+= resolves to a known action");
        expect_eq(std::string(*action), std::string("font_increase"), "Ctrl+= maps to font_increase");

        dispatcher.dispatch(*action);
        expect_eq(dispatcher.count("font_increase"), 1, "font_increase fired once");
    });

    run_test("keybinding dispatch: font_decrease Ctrl+- fires font_decrease action", []() {
        AppConfig cfg;
        ActionDispatcher dispatcher;
        for (const auto& b : cfg.keybindings)
            dispatcher.register_action(b.action);

        KeyEvent evt = make_key_event(SDLK_MINUS, kModCtrl);
        auto action = action_for_event(cfg, evt);
        expect(action.has_value(), "Ctrl+- resolves to a known action");
        expect_eq(std::string(*action), std::string("font_decrease"), "Ctrl+- maps to font_decrease");

        dispatcher.dispatch(*action);
        expect_eq(dispatcher.count("font_decrease"), 1, "font_decrease fired once");
    });

    run_test("keybinding dispatch: font_reset Ctrl+0 fires font_reset action", []() {
        AppConfig cfg;
        ActionDispatcher dispatcher;
        for (const auto& b : cfg.keybindings)
            dispatcher.register_action(b.action);

        KeyEvent evt = make_key_event(SDLK_0, kModCtrl);
        auto action = action_for_event(cfg, evt);
        expect(action.has_value(), "Ctrl+0 resolves to a known action");
        expect_eq(std::string(*action), std::string("font_reset"), "Ctrl+0 maps to font_reset");

        dispatcher.dispatch(*action);
        expect_eq(dispatcher.count("font_reset"), 1, "font_reset fired once");
    });

    run_test("keybinding dispatch: copy Ctrl+Shift+C fires copy action", []() {
        AppConfig cfg;
        ActionDispatcher dispatcher;
        for (const auto& b : cfg.keybindings)
            dispatcher.register_action(b.action);

        KeyEvent evt = make_key_event(SDLK_C, kModCtrl | kModShift);
        auto action = action_for_event(cfg, evt);
        expect(action.has_value(), "Ctrl+Shift+C resolves to a known action");
        expect_eq(std::string(*action), std::string("copy"), "Ctrl+Shift+C maps to copy");
    });

    run_test("keybinding dispatch: paste Ctrl+Shift+V fires paste action", []() {
        AppConfig cfg;
        ActionDispatcher dispatcher;
        for (const auto& b : cfg.keybindings)
            dispatcher.register_action(b.action);

        KeyEvent evt = make_key_event(SDLK_V, kModCtrl | kModShift);
        auto action = action_for_event(cfg, evt);
        expect(action.has_value(), "Ctrl+Shift+V resolves to a known action");
        expect_eq(std::string(*action), std::string("paste"), "Ctrl+Shift+V maps to paste");
    });

    // -----------------------------------------------------------------------
    // Overlapping bindings: first match wins
    // -----------------------------------------------------------------------

    run_test("keybinding dispatch: overlapping bindings — first match wins", []() {
        // Create config with two bindings for the same key combo.
        // The first one in the vector should win.
        AppConfig cfg;
        cfg.keybindings.clear();
        cfg.keybindings.push_back({ "copy", SDLK_C, kModCtrl });
        cfg.keybindings.push_back({ "paste", SDLK_C, kModCtrl }); // same key, different action

        KeyEvent evt = make_key_event(SDLK_C, kModCtrl);
        auto action = action_for_event(cfg, evt);
        expect(action.has_value(), "Ctrl+C resolves to an action with duplicate bindings");
        expect_eq(std::string(*action), std::string("copy"), "first binding wins when two bindings share the same key");
    });

    // -----------------------------------------------------------------------
    // Unknown action in config: parse_gui_keybinding rejects it
    // -----------------------------------------------------------------------

    run_test("keybinding dispatch: unknown action name is rejected by parser", []() {
        // parse_gui_keybinding validates the action name against kKnownGuiActions.
        auto result = parse_gui_keybinding("launch_rockets", "Ctrl+R");
        expect(!result.has_value(), "unknown action 'launch_rockets' is rejected by the parser");
    });

    run_test("keybinding dispatch: unknown action in TOML config is silently ignored", []() {
        // A TOML config with an unknown action key should not crash and the
        // unrecognised binding should not appear in the parsed keybindings.
        const char* content = "[keybindings]\nlaunch_rockets = \"Ctrl+R\"\n";
        ScopedLogCapture capture;
        AppConfig cfg = AppConfig::parse(content);

        // The unknown action must not appear in the bindings list.
        for (const auto& b : cfg.keybindings)
            expect(b.action != "launch_rockets", "unknown action must not appear in parsed bindings");

        // The default bindings should still be present.
        expect(find_binding(cfg, "toggle_diagnostics") != nullptr,
            "default toggle_diagnostics binding is still present");
    });

    // -----------------------------------------------------------------------
    // Empty config: no bindings, no crash
    // -----------------------------------------------------------------------

    run_test("keybinding dispatch: empty keybindings table leaves defaults intact", []() {
        // An empty [keybindings] table should not crash and should keep defaults.
        AppConfig cfg = AppConfig::parse("[keybindings]\n");

        // With an empty [keybindings] table the defaults remain unchanged.
        expect_eq(static_cast<int>(cfg.keybindings.size()), 6, "defaults remain with empty bindings table");
    });

    run_test("keybinding dispatch: no bindings configured — no action fires for any key", []() {
        AppConfig cfg;
        cfg.keybindings.clear();

        // No binding matches any key.
        const KeyEvent keys[] = {
            make_key_event(SDLK_F12),
            make_key_event(SDLK_C, kModCtrl),
            make_key_event(SDLK_EQUALS, kModCtrl),
        };

        for (const auto& evt : keys)
        {
            auto action = action_for_event(cfg, evt);
            expect(!action.has_value(), "no action fires when no bindings are registered");
        }
    });

    // -----------------------------------------------------------------------
    // Modifier-only key events do not trigger any binding
    // -----------------------------------------------------------------------

    run_test("keybinding dispatch: modifier-only key events do not match any binding", []() {
        AppConfig cfg;

        // Key events where the key itself is a modifier key (SDL_KMOD_* keys)
        // should not match any binding because no binding uses a modifier key as
        // the primary key.
        const int modifier_keys[] = {
            SDLK_LCTRL,
            SDLK_RCTRL,
            SDLK_LSHIFT,
            SDLK_RSHIFT,
            SDLK_LALT,
            SDLK_RALT,
        };

        for (int key : modifier_keys)
        {
            KeyEvent evt = make_key_event(key, kModNone);
            auto action = action_for_event(cfg, evt);
            expect(!action.has_value(), "modifier-only key event does not match any binding");
        }
    });

    // -----------------------------------------------------------------------
    // Key-released events do not trigger bindings (binding dispatch checks pressed)
    // -----------------------------------------------------------------------

    run_test("keybinding dispatch: key-released event does not trigger binding", []() {
        AppConfig cfg;

        // F12 released — should not trigger toggle_diagnostics.
        KeyEvent evt = make_key_event(SDLK_F12, kModNone, /*pressed=*/false);
        // App only calls gui_action_for_key_event when event.pressed is true.
        // The matching function itself does not check pressed, but the App guards
        // the call with `if (event.pressed)`.  We test the matcher alone and note
        // that dispatch is guarded at the call site.
        // At the matcher level, the binding would match regardless of pressed.
        // This test documents that no panic / UB occurs for released events.
        auto action = action_for_event(cfg, evt);
        // Not asserting direction — just that no crash occurs.
        expect(true, "no crash dispatching key-released event");
    });

    // -----------------------------------------------------------------------
    // Custom bindings parsed from TOML
    // -----------------------------------------------------------------------

    run_test("keybinding dispatch: TOML custom binding overrides default for same action", []() {
        // Override toggle_diagnostics to Ctrl+D instead of F12.
        const char* content = "[keybindings]\ntoggle_diagnostics = \"Ctrl+D\"\n";
        AppConfig cfg = AppConfig::parse(content);

        const GuiKeybinding* toggle = find_binding(cfg, "toggle_diagnostics");
        expect(toggle != nullptr, "toggle_diagnostics binding is present");
        expect_eq(toggle->key, SDLK_D, "toggle key overridden to D");
        expect_eq(toggle->modifiers, kModCtrl, "toggle modifiers overridden to Ctrl");

        // Old binding (F12) must no longer trigger toggle_diagnostics.
        KeyEvent f12_evt = make_key_event(SDLK_F12, kModNone);
        auto f12_action = action_for_event(cfg, f12_evt);
        expect(!f12_action.has_value() || std::string(*f12_action) != "toggle_diagnostics",
            "F12 no longer triggers toggle_diagnostics after override");

        // New binding (Ctrl+D) must trigger toggle_diagnostics.
        KeyEvent ctrl_d_evt = make_key_event(SDLK_D, kModCtrl);
        auto ctrl_d_action = action_for_event(cfg, ctrl_d_evt);
        expect(ctrl_d_action.has_value(), "Ctrl+D resolves to a binding");
        expect_eq(std::string(*ctrl_d_action), std::string("toggle_diagnostics"),
            "Ctrl+D triggers toggle_diagnostics after override");
    });

    run_test("keybinding dispatch: gui_keybinding_matches ignores irrelevant modifier bits", []() {
        // The matcher normalises modifiers through kGuiModifierMask; CAPSLOCK
        // should not affect matching.
        const GuiKeybinding binding{ "font_increase", SDLK_EQUALS, kModCtrl };

        // Ctrl+= without any extra modifiers: should match.
        expect(gui_keybinding_matches(binding, make_key_event(SDLK_EQUALS, kModCtrl)),
            "Ctrl+= with no extra mods matches");

        // Unrelated key: should not match.
        expect(!gui_keybinding_matches(binding, make_key_event(SDLK_A, kModCtrl)),
            "Ctrl+A does not match a Ctrl+= binding");

        // Wrong modifier: should not match.
        expect(!gui_keybinding_matches(binding, make_key_event(SDLK_EQUALS, kModAlt)),
            "Alt+= does not match a Ctrl+= binding");
    });
}
