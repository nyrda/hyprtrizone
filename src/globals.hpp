#pragma once

#include <atomic>
#include <hyprland/src/plugins/PluginAPI.hpp>

inline HANDLE PHANDLE = nullptr;
inline std::atomic_bool GPLUGIN_SHUTTING_DOWN = false;
