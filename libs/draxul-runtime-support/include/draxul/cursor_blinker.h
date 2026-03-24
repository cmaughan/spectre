#pragma once
#include <chrono>
#include <optional>

namespace draxul
{

struct BlinkTiming
{
    int blinkwait = 0; // ms before first blink after cursor move
    int blinkon = 0; // ms cursor is visible per cycle
    int blinkoff = 0; // ms cursor is hidden per cycle

    bool enabled() const
    {
        return blinkwait > 0 && blinkon > 0 && blinkoff > 0;
    }
};

// Owns cursor blink state machine logic.
// Caller supplies timing/busy on restart and calls advance() each frame.
// Does not talk to renderer directly — visibility changes are polled via visible().
class CursorBlinker
{
public:
    // Restart the blink cycle with given busy state and timing.
    // busy=true immediately hides the cursor and stops blinking.
    void restart(std::chrono::steady_clock::time_point now, bool busy, const BlinkTiming& timing);

    // Advance the state machine. Returns true if visibility changed.
    bool advance(std::chrono::steady_clock::time_point now);

    bool visible() const
    {
        return visible_;
    }

    // Next wakeup deadline for the main loop, or nullopt if no blink is active.
    std::optional<std::chrono::steady_clock::time_point> next_deadline() const
    {
        return next_deadline_;
    }

private:
    enum class Phase
    {
        Steady, // no blink
        Wait, // waiting blinkwait ms before first hide
        On, // visible, waiting blinkon ms
        Off, // hidden, waiting blinkoff ms
    };

    bool visible_ = true;
    Phase phase_ = Phase::Steady;
    BlinkTiming timing_;
    std::optional<std::chrono::steady_clock::time_point> next_deadline_;
};

} // namespace draxul
