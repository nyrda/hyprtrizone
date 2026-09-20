#include "pluginConfig.hpp"

#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/config/values/types/BoolValue.hpp>
#include <hyprland/src/config/values/types/ColorValue.hpp>
#include <hyprland/src/config/values/types/FloatValue.hpp>
#include <hyprland/src/config/values/types/IntValue.hpp>
#include <hyprland/src/config/values/types/StringValue.hpp>
#include <hyprland/src/debug/log/Logger.hpp>
#include <typeindex>
#include <vector>

namespace {
    std::vector<SP<Config::Values::IValue>> g_values;

    template <typename T, typename... Args>
    void addValue(HANDLE handle, Args&&... args) {
        auto value = makeShared<T>(std::forward<Args>(args)...);
        if (!HyprlandAPI::addConfigValueV2(handle, value))
            Log::logger->log(Log::ERR, "hyprtrizone config: failed to register {}", value->name());
        g_values.emplace_back(std::move(value));
    }

    Config::SConfigOptionReply getReply(const char* key) {
        if (!Config::mgr())
            return {};
        return Config::mgr()->getConfigValue(key);
    }
}

namespace HyprtrizoneConfig {
    void init(HANDLE handle) {
        g_values.clear();

        addValue<Config::Values::CFloatValue>(handle, "plugin:hyprtrizone:master_factor", "hyprtrizone center master width factor", 0.5F);
        addValue<Config::Values::CFloatValue>(handle, "plugin:hyprtrizone:special_scale_factor", "hyprtrizone special workspace scale factor", 0.8F);
        addValue<Config::Values::CStringValue>(handle, "plugin:hyprtrizone:side_bias", "hyprtrizone default side bias", "right");
        addValue<Config::Values::CBoolValue>(handle, "plugin:hyprtrizone:scrolling_enabled", "hyprtrizone side scrolling enabled", true);
        addValue<Config::Values::CIntValue>(handle, "plugin:hyprtrizone:side_visible_windows", "hyprtrizone fallback visible side rows", 3);
        addValue<Config::Values::CIntValue>(handle, "plugin:hyprtrizone:left_side_visible_windows", "hyprtrizone visible left side rows", 0);
        addValue<Config::Values::CIntValue>(handle, "plugin:hyprtrizone:right_side_visible_windows", "hyprtrizone visible right side rows", 0);
        addValue<Config::Values::CFloatValue>(handle, "plugin:hyprtrizone:scroll_step", "hyprtrizone side scroll step", 0.35F);
        addValue<Config::Values::CBoolValue>(handle, "plugin:hyprtrizone:pairing_left", "hyprtrizone left side pairing", false);
        addValue<Config::Values::CBoolValue>(handle, "plugin:hyprtrizone:pairing_right", "hyprtrizone right side pairing", false);
        addValue<Config::Values::CBoolValue>(handle, "plugin:hyprtrizone:pairing_tag_only", "hyprtrizone pairing requires tags", true);
        addValue<Config::Values::CStringValue>(handle, "plugin:hyprtrizone:pairing_tags", "hyprtrizone pairing tags", "pair");
        addValue<Config::Values::CIntValue>(handle, "plugin:hyprtrizone:indicator_width", "hyprtrizone indicator width", 112);
        addValue<Config::Values::CIntValue>(handle, "plugin:hyprtrizone:indicator_height", "hyprtrizone indicator height", 40);
        addValue<Config::Values::CIntValue>(handle, "plugin:hyprtrizone:indicator_font_size", "hyprtrizone indicator font size", 24);
        addValue<Config::Values::CIntValue>(handle, "plugin:hyprtrizone:indicator_padding", "hyprtrizone indicator padding", 8);
        addValue<Config::Values::CFloatValue>(handle, "plugin:hyprtrizone:indicator_opacity", "hyprtrizone indicator opacity", 0.78F);
        addValue<Config::Values::CFloatValue>(handle, "plugin:hyprtrizone:indicator_text_opacity", "hyprtrizone indicator text opacity", 1.0F);
        addValue<Config::Values::CColorValue>(handle, "plugin:hyprtrizone:indicator_text_color", "hyprtrizone indicator text color", 0xFFFFFFFF);
        addValue<Config::Values::CBoolValue>(handle, "plugin:hyprtrizone:indicator_show_zero", "hyprtrizone show zero indicators", false);
        addValue<Config::Values::CStringValue>(handle, "plugin:hyprtrizone:zone_tags_left", "hyprtrizone left zone tags", "");
        addValue<Config::Values::CStringValue>(handle, "plugin:hyprtrizone:zone_tags_center", "hyprtrizone center zone tags", "");
        addValue<Config::Values::CStringValue>(handle, "plugin:hyprtrizone:zone_tags_right", "hyprtrizone right zone tags", "");
        addValue<Config::Values::CStringValue>(handle, "plugin:hyprtrizone:follow_workspace_tags", "hyprtrizone workspace-follow tags", "");
        addValue<Config::Values::CIntValue>(handle, "plugin:hyprtrizone:left_header_height", "hyprtrizone left reserved top space", 0);
        addValue<Config::Values::CIntValue>(handle, "plugin:hyprtrizone:right_header_height", "hyprtrizone right reserved top space", 0);
    }

    int getInt(const char* key, int fallback) {
        const auto reply = getReply(key);
        if (!reply.dataptr || !reply.type)
            return fallback;

        const std::type_index type{*reply.type};
        if (type == typeid(bool))
            return **reinterpret_cast<bool* const*>(reply.dataptr) ? 1 : 0;
        if (type == typeid(Config::INTEGER))
            return (int)**reinterpret_cast<Config::INTEGER* const*>(reply.dataptr);
        if (type == typeid(Hyprlang::INT))
            return (int)**reinterpret_cast<Hyprlang::INT* const*>(reply.dataptr);

        return fallback;
    }

    float getFloat(const char* key, float fallback) {
        const auto reply = getReply(key);
        if (!reply.dataptr || !reply.type)
            return fallback;

        const std::type_index type{*reply.type};
        if (type == typeid(Config::FLOAT))
            return (float)**reinterpret_cast<Config::FLOAT* const*>(reply.dataptr);
        if (type == typeid(Hyprlang::FLOAT))
            return (float)**reinterpret_cast<Hyprlang::FLOAT* const*>(reply.dataptr);
        if (type == typeid(Config::INTEGER))
            return (float)**reinterpret_cast<Config::INTEGER* const*>(reply.dataptr);

        return fallback;
    }

    std::string getString(const char* key, std::string fallback) {
        const auto reply = getReply(key);
        if (!reply.dataptr || !reply.type)
            return fallback;

        const std::type_index type{*reply.type};
        if (type == typeid(std::string)) {
            const auto ptr = *reinterpret_cast<const std::string* const*>(reply.dataptr);
            return ptr ? *ptr : fallback;
        }
        if (type == typeid(const char*)) {
            const auto ptr = reinterpret_cast<Hyprlang::STRING const*>(reply.dataptr);
            return ptr && *ptr ? std::string(*ptr) : fallback;
        }

        return fallback;
    }
}
