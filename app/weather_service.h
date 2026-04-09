#pragma once

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>

namespace draxul
{

// Periodically fetches the current temperature via the Open-Meteo API.
// Requires `curl` on PATH. Degrades silently if curl is missing, the
// network is down, or the location string cannot be geocoded.
//
// Usage:
//   weather.start("York, UK");   // or "51.5,-0.1"
//   ...
//   auto text = weather.display_text();  // "\U0001F321\uFE0F 18°C" or ""
class WeatherService
{
public:
    WeatherService() = default;
    ~WeatherService();

    // Start periodic fetching for the given location. If location looks like
    // "lat,lon" (two numbers), uses it directly. Otherwise geocodes via
    // Open-Meteo. No-op if location is empty.
    void start(const std::string& location);

    // Stop background fetching.
    void stop();

    // Thread-safe. Returns "" if no data is available yet.
    std::string display_text() const;

    // Thread-safe accessors for separate emoji and temperature parts.
    std::string emoji() const;
    std::string temperature() const;

    // Returns true if a fetch has completed at least once.
    bool has_data() const
    {
        return has_data_.load(std::memory_order_relaxed);
    }

private:
    void worker_func(std::string location);
    bool try_geocode(const std::string& query, double& lat, double& lon);
    bool fetch_temperature(double lat, double lon, double& temp_c, int& weather_code);
    static std::string run_curl(const std::string& url);
    static std::string weather_emoji(int code);

    mutable std::mutex mutex_;
    std::string display_text_;
    std::string emoji_;
    std::string temperature_;
    std::atomic<bool> has_data_{ false };
    std::atomic<bool> running_{ false };
    std::thread thread_;
};

} // namespace draxul
