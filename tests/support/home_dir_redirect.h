#pragma once

#include <cstdlib>
#include <filesystem>
#include <string>

namespace draxul::tests
{

struct HomeDirRedirect
{
    std::filesystem::path config_path;

#ifdef _WIN32
    std::string original_appdata_;
    explicit HomeDirRedirect(const std::filesystem::path& home)
    {
        const char* orig = std::getenv("APPDATA");
        original_appdata_ = orig ? orig : "";
        config_path = home / "draxul" / "config.toml";
        _putenv_s("APPDATA", home.string().c_str());
    }
    ~HomeDirRedirect()
    {
        _putenv_s("APPDATA", original_appdata_.c_str());
    }
#elif defined(__APPLE__)
    std::string original_home_;
    explicit HomeDirRedirect(const std::filesystem::path& home)
    {
        const char* orig = std::getenv("HOME");
        original_home_ = orig ? orig : "";
        config_path = home / "Library" / "Application Support" / "draxul" / "config.toml";
        setenv("HOME", home.string().c_str(), 1);
    }
    ~HomeDirRedirect()
    {
        setenv("HOME", original_home_.c_str(), 1);
    }
#else
    std::string original_xdg_;
    std::string original_home_;
    explicit HomeDirRedirect(const std::filesystem::path& home)
    {
        const char* xdg = std::getenv("XDG_CONFIG_HOME");
        original_xdg_ = xdg ? xdg : "";
        const char* orig = std::getenv("HOME");
        original_home_ = orig ? orig : "";
        setenv("HOME", home.string().c_str(), 1);
        unsetenv("XDG_CONFIG_HOME");
        config_path = home / ".config" / "draxul" / "config.toml";
    }
    ~HomeDirRedirect()
    {
        setenv("HOME", original_home_.c_str(), 1);
        if (!original_xdg_.empty())
            setenv("XDG_CONFIG_HOME", original_xdg_.c_str(), 1);
    }
#endif
};

} // namespace draxul::tests
