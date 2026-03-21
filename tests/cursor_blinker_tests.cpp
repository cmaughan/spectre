#include "support/test_support.h"

#include <draxul/cursor_blinker.h>

using namespace draxul;
using namespace draxul::tests;

using tp = std::chrono::steady_clock::time_point;

static tp T(int ms_offset)
{
    return tp{} + std::chrono::milliseconds(ms_offset);
}

void run_cursor_blinker_tests()
{
    run_test("cursor blinker starts visible with no blink timing", []() {
        CursorBlinker b;
        b.restart(T(0), false, {});
        expect(b.visible(), "cursor should be visible with no blink timing");
        expect(!b.next_deadline().has_value(), "no deadline when blink is disabled");
        expect(!b.advance(T(9999)), "advance should not change visibility without a deadline");
    });

    run_test("cursor blinker hides cursor when busy", []() {
        CursorBlinker b;
        b.restart(T(0), true, { 500, 500, 500 });
        expect(!b.visible(), "busy should hide cursor");
        expect(!b.next_deadline().has_value(), "no deadline when busy");
        expect(!b.advance(T(9999)), "advance should not change visibility when busy");
    });

    run_test("cursor blinker does not fire before blinkwait elapses", []() {
        CursorBlinker b;
        b.restart(T(0), false, { 500, 400, 300 });
        expect(b.visible(), "cursor visible before blinkwait");
        expect(b.next_deadline().has_value(), "deadline set after restart");
        expect(!b.advance(T(499)), "should not advance before deadline");
        expect(b.visible(), "still visible before blinkwait");
    });

    run_test("cursor blinker hides after blinkwait", []() {
        CursorBlinker b;
        b.restart(T(0), false, { 500, 400, 300 });
        bool changed = b.advance(T(500));
        expect(changed, "advance should return true when visibility changes");
        expect(!b.visible(), "cursor should be hidden after blinkwait");
        expect(b.next_deadline().has_value(), "next deadline set for blinkon");
    });

    run_test("cursor blinker runs full on/off cycle", []() {
        CursorBlinker b;
        b.restart(T(0), false, { 500, 400, 300 });

        b.advance(T(500)); // wait -> off
        expect(!b.visible(), "hidden after blinkwait");

        b.advance(T(800)); // off -> on
        expect(b.visible(), "visible after blinkoff");

        b.advance(T(1200)); // on -> off
        expect(!b.visible(), "hidden after blinkon");

        b.advance(T(1500)); // off -> on
        expect(b.visible(), "visible again after blinkoff");
    });

    run_test("cursor blinker deadline advances correctly through cycle", []() {
        CursorBlinker b;
        b.restart(T(0), false, { 500, 400, 300 });
        expect_eq(*b.next_deadline(), T(500), "initial deadline = blinkwait");

        b.advance(T(500)); // off; next = T(500) + blinkoff(300)
        expect_eq(*b.next_deadline(), T(800), "deadline after blinkwait = T(500) + blinkoff");

        b.advance(T(800)); // on; next = T(800) + blinkon(400)
        expect_eq(*b.next_deadline(), T(1200), "deadline after blinkoff = T(800) + blinkon");
    });

    run_test("cursor blinker restart resets cycle to visible", []() {
        CursorBlinker b;
        b.restart(T(0), false, { 500, 400, 300 });
        b.advance(T(500)); // now hidden

        b.restart(T(600), false, { 500, 400, 300 });
        expect(b.visible(), "restart should make cursor visible again");
        expect(!b.advance(T(1099)), "should not fire before new blinkwait");
        expect(b.visible(), "still visible before new blinkwait elapses");
    });
}
