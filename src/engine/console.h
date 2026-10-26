#pragma once

#include "core/object.h"

#include <cstdint>
#include <functional>
#include <string>

namespace rune {
    namespace console {
        std::string execute(UObject* context, const std::string& command, bool* handled = nullptr);
        std::string executeEngine(const std::string& command);
        void loadBindings();
        bool key(uint8_t key, uint8_t action, float delta);
        void readInput(UObject* player, float deltaSeconds);
        void resetInput(UObject* player);
        std::string binding(uint8_t key);
        std::string keyName(uint8_t key);
        uint8_t keyFromName(const std::string& name);
        void applyAudioSettings();

        extern std::function<void(const std::string&, int32_t)> openMapHook;
        extern std::function<void()> quitHook;
        extern std::function<std::string()> currentResHook;
        extern std::function<std::string()> resolutionsHook;
        extern std::function<void(int, int)> setResHook;
        extern std::function<void()> toggleFullscreenHook;
        extern std::function<std::string()> currentWindowModeHook;
        extern std::function<bool(const std::string&)> setWindowModeHook;
        extern std::function<void()> screenshotHook;
        extern std::function<void()> paintProgressHook;
        extern std::function<void()> changeVideoHook;
        extern std::function<void(const std::string&)> playVideoHook;
        extern std::function<void()> stopVideoHook;
    }
}
