#pragma once

#include <hyprland/src/plugins/PluginAPI.hpp>
#include <string>

namespace HyprtrizoneConfig {
    void        init(HANDLE handle);
    int         getInt(const char* key, int fallback = 0);
    float       getFloat(const char* key, float fallback = 0.F);
    std::string getString(const char* key, std::string fallback = {});
}
