#include "weather_service.h"

#include <draxul/log.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#endif

namespace draxul
{

WeatherService::~WeatherService()
{
    stop();
}

void WeatherService::start(const std::string& location)
{
    if (location.empty())
        return;
    if (running_.exchange(true))
        return; // already running
    thread_ = std::thread(&WeatherService::worker_func, this, location);
}

void WeatherService::stop()
{
    running_ = false;
    if (thread_.joinable())
        thread_.join();
}

std::string WeatherService::display_text() const
{
    std::lock_guard lock(mutex_);
    if (emoji_.empty() && temperature_.empty())
        return {};
    return emoji_ + " " + temperature_;
}

std::string WeatherService::emoji() const
{
    std::lock_guard lock(mutex_);
    return emoji_;
}

std::string WeatherService::temperature() const
{
    std::lock_guard lock(mutex_);
    return temperature_;
}

void WeatherService::worker_func(std::string location)
{
    double lat = 0.0, lon = 0.0;

    // Check if location looks like "lat,lon" (two numbers separated by comma).
    {
        auto comma = location.find(',');
        if (comma != std::string::npos)
        {
            char* end1 = nullptr;
            char* end2 = nullptr;
            double a = std::strtod(location.c_str(), &end1);
            double b = std::strtod(location.c_str() + comma + 1, &end2);
            if (end1 != location.c_str() && end2 != location.c_str() + comma + 1)
            {
                lat = a;
                lon = b;
            }
        }
    }

    // If not numeric, geocode the location string.
    if (lat == 0.0 && lon == 0.0)
    {
        if (!try_geocode(location, lat, lon))
        {
            DRAXUL_LOG_DEBUG(LogCategory::App,
                "WeatherService: geocode failed for '%s'; disabling", location.c_str());
            running_ = false;
            return;
        }
    }

    DRAXUL_LOG_DEBUG(LogCategory::App,
        "WeatherService: resolved location to %.4f,%.4f", lat, lon);

    constexpr auto kFetchInterval = std::chrono::minutes(10);
    constexpr auto kSleepGranularity = std::chrono::seconds(1);

    while (running_)
    {
        double temp_c = 0.0;
        int weather_code = 0;
        if (fetch_temperature(lat, lon, temp_c, weather_code))
        {
            char temp_buf[32];
            std::snprintf(temp_buf, sizeof(temp_buf), "%.0f°C", temp_c);
            {
                std::lock_guard lock(mutex_);
                emoji_ = weather_emoji(weather_code);
                temperature_ = temp_buf;
            }
            has_data_ = true;
        }

        // Sleep in small increments so stop() is responsive.
        auto deadline = std::chrono::steady_clock::now() + kFetchInterval;
        while (running_ && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(kSleepGranularity);
    }
}

bool WeatherService::try_geocode(const std::string& query, double& lat, double& lon)
{
    // Split "City, Country" into city name and optional country filter.
    std::string city_name = query;
    std::string country_filter;
    if (auto comma = city_name.find(','); comma != std::string::npos)
    {
        country_filter = city_name.substr(comma + 1);
        city_name = city_name.substr(0, comma);
    }
    // Trim whitespace.
    while (!city_name.empty() && city_name.back() == ' ')
        city_name.pop_back();
    while (!country_filter.empty() && country_filter.front() == ' ')
        country_filter.erase(country_filter.begin());
    while (!country_filter.empty() && country_filter.back() == ' ')
        country_filter.pop_back();

    // URL-encode the city name for the query parameter.
    std::string encoded;
    for (char c : city_name)
    {
        if (c == ' ')
            encoded += '+';
        else if (c == ',' || c == '&' || c == '=' || c == '#' || c == '%')
        {
            char hex[4];
            std::snprintf(hex, sizeof(hex), "%%%02X", static_cast<unsigned char>(c));
            encoded += hex;
        }
        else
            encoded += c;
    }

    // Fetch multiple results so we can match the country filter.
    std::string url = "https://geocoding-api.open-meteo.com/v1/search?name="
        + encoded + "&count=10&language=en&format=json";
    std::string json = run_curl(url);
    if (json.empty())
        return false;

    // Case-insensitive substring match for country filtering.
    auto icontains = [](const std::string& haystack, const std::string& needle) -> bool {
        if (needle.empty())
            return true;
        auto it = std::search(haystack.begin(), haystack.end(),
            needle.begin(), needle.end(),
            [](char a, char b) { return std::tolower(a) == std::tolower(b); });
        return it != haystack.end();
    };

    // Walk through "results" array entries. Each entry has "latitude",
    // "longitude", "country", and "country_code" fields. Pick the first
    // entry whose country or country_code matches the filter (or the
    // first entry if no filter).
    auto extract_double_at = [&](size_t search_from, const char* key) -> double {
        auto pos = json.find(key, search_from);
        if (pos == std::string::npos)
            return 0.0;
        pos = json.find(':', pos + std::strlen(key));
        if (pos == std::string::npos)
            return 0.0;
        ++pos;
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t'))
            ++pos;
        return std::strtod(json.c_str() + pos, nullptr);
    };
    auto extract_string_at = [&](size_t search_from, const char* key) -> std::string {
        auto pos = json.find(key, search_from);
        if (pos == std::string::npos)
            return {};
        pos = json.find('"', pos + std::strlen(key) + 2); // skip key":<space>"
        if (pos == std::string::npos)
            return {};
        ++pos; // skip opening quote
        auto end = json.find('"', pos);
        if (end == std::string::npos)
            return {};
        return json.substr(pos, end - pos);
    };

    // Iterate over result entries by finding successive "latitude" keys.
    size_t search_pos = json.find("\"results\"");
    if (search_pos == std::string::npos)
        return false;

    bool found_any = false;
    double first_lat = 0.0, first_lon = 0.0;
    while (true)
    {
        auto entry_pos = json.find("\"latitude\"", search_pos);
        if (entry_pos == std::string::npos)
            break;

        double entry_lat = extract_double_at(entry_pos, "\"latitude\"");
        double entry_lon = extract_double_at(entry_pos, "\"longitude\"");
        std::string entry_country = extract_string_at(entry_pos, "\"country\"");
        std::string entry_code = extract_string_at(entry_pos, "\"country_code\"");

        if (!found_any)
        {
            first_lat = entry_lat;
            first_lon = entry_lon;
            found_any = true;
        }

        if (country_filter.empty()
            || icontains(entry_country, country_filter)
            || icontains(entry_code, country_filter))
        {
            lat = entry_lat;
            lon = entry_lon;
            return true;
        }

        search_pos = entry_pos + 10;
    }

    // No country match — fall back to the first result.
    if (found_any)
    {
        lat = first_lat;
        lon = first_lon;
        return true;
    }
    return false;
}

bool WeatherService::fetch_temperature(double lat, double lon, double& temp_c, int& weather_code)
{
    char url_buf[256];
    std::snprintf(url_buf, sizeof(url_buf),
        "https://api.open-meteo.com/v1/forecast?"
        "latitude=%.4f&longitude=%.4f&current_weather=true",
        lat, lon);
    std::string json = run_curl(url_buf);
    if (json.empty())
        return false;

    // Find the "current_weather" object first, then extract fields within it.
    // This avoids hitting the "current_weather_units" section which has the
    // same key names but string values.
    auto cw_pos = json.find("\"current_weather\"");
    if (cw_pos == std::string::npos)
        return false;
    // Narrow search to the substring starting from current_weather.
    std::string_view cw_json(json.data() + cw_pos, json.size() - cw_pos);

    auto extract_number = [](std::string_view src, const char* key) -> double {
        auto pos = src.find(key);
        if (pos == std::string_view::npos)
            return 0.0;
        pos = src.find(':', pos + std::strlen(key));
        if (pos == std::string_view::npos)
            return 0.0;
        ++pos;
        while (pos < src.size() && (src[pos] == ' ' || src[pos] == '\t'))
            ++pos;
        // src is a string_view into json which is null-terminated, so
        // strtod is safe as long as pos < json.size().
        return std::strtod(src.data() + pos, nullptr);
    };

    temp_c = extract_number(cw_json, "\"temperature\"");
    weather_code = static_cast<int>(extract_number(cw_json, "\"weathercode\""));
    return true;
}

std::string WeatherService::run_curl(const std::string& url)
{
    std::string cmd = "curl -s --max-time 5 --connect-timeout 3 '";
    cmd += url;
    cmd += "' 2>/dev/null";

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe)
        return {};

    std::string result;
    std::array<char, 4096> buf{};
    while (auto n = fread(buf.data(), 1, buf.size(), pipe))
        result.append(buf.data(), n);
    int status = pclose(pipe);
    if (status != 0)
        return {};
    return result;
}

std::string WeatherService::weather_emoji(int code)
{
    // WMO weather interpretation codes:
    // https://open-meteo.com/en/docs#weathervariables
    if (code == 0)
        return "\u2600\uFE0F"; // ☀️ clear
    if (code <= 3)
        return "\u26C5"; // ⛅ partly cloudy
    if (code <= 48)
        return "\u2601\uFE0F"; // ☁️ fog/overcast
    if (code <= 57)
        return "\U0001F327\uFE0F"; // 🌧️ drizzle
    if (code <= 67)
        return "\U0001F327\uFE0F"; // 🌧️ rain
    if (code <= 77)
        return "\u2744\uFE0F"; // ❄️ snow
    if (code <= 82)
        return "\U0001F327\uFE0F"; // 🌧️ rain showers
    if (code <= 86)
        return "\u2744\uFE0F"; // ❄️ snow showers
    if (code <= 99)
        return "\u26A1"; // ⚡ thunderstorm
    return "\U0001F321\uFE0F"; // 🌡️ fallback
}

} // namespace draxul
