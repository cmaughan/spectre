#define CATCH_CONFIG_RUNNER
#include <catch2/catch_all.hpp>

#include <cstdlib>
#include <exception>

#include <draxul/log.h>

void run_app_config_tests();
void run_cursor_blinker_tests();
void run_font_tests();
void run_grid_tests();
void run_grid_rendering_pipeline_tests();
void run_ui_events_tests();
void run_input_tests();
void run_renderer_state_tests();
void run_ui_request_worker_tests();
void run_startup_resize_state_tests();
void run_vk_resource_helpers_tests();
void run_rpc_codec_tests();
void run_rpc_integration_tests();
void run_nvim_process_tests();
void run_render_test_parser_tests();
void run_ui_panel_layout_tests();
void run_clipboard_tests();
void run_font_size_tests();
void run_mpack_fuzz_tests(bool run_slow);
void run_nvim_crash_tests();
void run_dpi_scaling_tests();
void run_shutdown_race_tests();
void run_font_fallback_corpus_tests(bool run_slow);
void run_terminal_vt_tests();
void run_mpackvalue_variant_stability_tests();
void run_terminal_mouse_tests();
void run_selection_truncation_tests();
void run_scrollback_overflow_tests();
void run_ligature_atlas_reset_tests();
void run_rpc_backpressure_tests();
void run_keybinding_dispatch_tests();
void run_hlattr_style_flags_tests();
void run_resize_cascade_tests();
void run_startup_rollback_tests();
void run_powershell_host_tests();
void run_encode_key_tests();
void run_attr_id_tests();
void run_gui_action_handler_tests();

int main()
{
    // Run Catch2 auto-registered TEST_CASEs (log, highlight, unicode, and any new tests)
    int catch_result = Catch::Session().run();

    // Run old-style test suites (not yet migrated to TEST_CASE)
    try
    {
        const bool run_slow = std::getenv("DRAXUL_RUN_SLOW_TESTS") != nullptr;
        run_app_config_tests();
        run_cursor_blinker_tests();
        run_font_tests();
        run_grid_tests();
        run_grid_rendering_pipeline_tests();
        run_ui_events_tests();
        run_input_tests();
        run_renderer_state_tests();
        run_ui_request_worker_tests();
        run_startup_resize_state_tests();
        run_vk_resource_helpers_tests();
        run_rpc_codec_tests();
        run_rpc_integration_tests();
        run_nvim_process_tests();
        run_render_test_parser_tests();
        run_ui_panel_layout_tests();
        run_clipboard_tests();
        run_font_size_tests();
        run_mpack_fuzz_tests(run_slow);
        run_nvim_crash_tests();
        run_dpi_scaling_tests();
        run_shutdown_race_tests();
        run_font_fallback_corpus_tests(run_slow);
        run_terminal_vt_tests();
        run_mpackvalue_variant_stability_tests();
        run_terminal_mouse_tests();
        run_selection_truncation_tests();
        run_scrollback_overflow_tests();
        run_ligature_atlas_reset_tests();
        run_rpc_backpressure_tests();
        run_keybinding_dispatch_tests();
        run_hlattr_style_flags_tests();
        run_resize_cascade_tests();
        run_startup_rollback_tests();
        run_powershell_host_tests();
        run_encode_key_tests();
        run_attr_id_tests();
        run_gui_action_handler_tests();
        DRAXUL_LOG_INFO(draxul::LogCategory::Test, "draxul-tests: ok");
    }
    catch (const std::exception& ex)
    {
        DRAXUL_LOG_ERROR(draxul::LogCategory::Test, "draxul-tests: %s", ex.what());
        return 1;
    }

    return catch_result;
}
