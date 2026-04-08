#pragma once

// WI 25 — canonical FakeClock for tests.
//
// A monotonic steady-clock fake that only advances when tests ask it to.
// Use with subsystems that accept an injectable TimeSource callable
// (e.g. ToastHost::set_time_source). Callers typically do:
//
//     tests::FakeClock clock;
//     host.set_time_source(clock.source());
//     clock.advance(500ms);
//
// Starts at a non-zero baseline so code that treats the epoch as "never"
// still sees a valid timestamp.

#include <chrono>
#include <functional>

namespace draxul::tests
{

class FakeClock
{
public:
    using clock = std::chrono::steady_clock;
    using time_point = clock::time_point;
    using TimeSource = std::function<time_point()>;

    FakeClock()
        : now_(time_point{} + std::chrono::seconds(1000))
    {
    }

    explicit FakeClock(time_point start)
        : now_(start)
    {
    }

    void advance(std::chrono::milliseconds dt)
    {
        now_ += dt;
    }

    void advance(std::chrono::nanoseconds dt)
    {
        now_ += std::chrono::duration_cast<clock::duration>(dt);
    }

    void set_now(time_point tp)
    {
        now_ = tp;
    }

    time_point now() const
    {
        return now_;
    }

    // Bind this clock to a std::function-compatible TimeSource. The returned
    // callable keeps a pointer to this object, so the FakeClock must outlive
    // the subsystem it is bound into.
    TimeSource source()
    {
        return [this] { return now_; };
    }

private:
    time_point now_;
};

} // namespace draxul::tests
