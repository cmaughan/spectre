#include "support/test_support.h"

#include <draxul/startup_resize_state.h>

using namespace draxul;
using namespace draxul::tests;

void run_startup_resize_state_tests()
{
    run_test("startup resize state defers dispatch until the first flush", []() {
        StartupResizeState state;
        state.defer(120, 40);

        expect(state.pending(), "a pre-flush resize should stay pending");

        auto deferred = state.consume_if_needed(80, 24);
        expect(deferred.has_value(), "the first flush should dispatch a changed pending resize");
        expect_eq(deferred->first, 120, "pending columns are preserved");
        expect_eq(deferred->second, 40, "pending rows are preserved");
        expect(!state.pending(), "dispatch clears the pending resize");
    });

    run_test("startup resize state skips redundant resizes and keeps the latest request", []() {
        StartupResizeState state;
        state.defer(100, 30);
        state.defer(140, 50);

        auto redundant = state.consume_if_needed(140, 50);
        expect(!redundant.has_value(), "first flush should skip redundant deferred resizes");
        expect(!state.pending(), "consuming a redundant resize still clears the pending flag");

        state.defer(150, 55);
        auto latest = state.consume_if_needed(120, 40);
        expect(latest.has_value(), "a later deferred resize should still dispatch");
        expect_eq(latest->first, 150, "latest pending columns win");
        expect_eq(latest->second, 55, "latest pending rows win");
    });
}
