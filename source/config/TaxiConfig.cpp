#include "config/TaxiConfig.h"

#include <Windows.h>

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <fstream>
#include <string>
#include <unordered_map>

namespace {
constexpr std::array<const char *, 19> kActionNames{
    "Hail",
    "ToggleDestinations",
    "PreviousDestination",
    "NextDestination",
    "ConfirmDestination",
    "PreviousDestinationCategory",
    "NextDestinationCategory",
    "SkipTravel",
    "StopTaxi",
    "LockDoors",
    "UnlockDoors",
    "ToggleFirstPerson",
    "OpenDestinationBrowser",
    "BrowserPreviousCategory",
    "BrowserNextCategory",
    "BrowserPreviousDestination",
    "BrowserNextDestination",
    "BrowserConfirm",
    "BrowserCancel",
};

std::string Trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");

    if (first == std::string::npos) {
        return {};
    }

    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

int ReadInt(const std::unordered_map<std::string, std::string> &values, const char *key,
            int fallback, int minimum, int maximum) {
    const auto it = values.find(key);

    if (it == values.end()) {
        return fallback;
    }

    int parsed{};
    const auto result =
        std::from_chars(it->second.data(), it->second.data() + it->second.size(), parsed, 10);
    return result.ec == std::errc{} && result.ptr == it->second.data() + it->second.size()
               ? std::clamp(parsed, minimum, maximum)
               : fallback;
}

float ReadFloat(const std::unordered_map<std::string, std::string> &values, const char *key,
                float fallback, float minimum, float maximum) {
    const auto it = values.find(key);

    if (it == values.end()) {
        return fallback;
    }

    char *end{};
    const float parsed = std::strtof(it->second.c_str(), &end);
    return end == it->second.c_str() + it->second.size() ? std::clamp(parsed, minimum, maximum)
                                                         : fallback;
}

} // namespace

TaxiConfig::TaxiConfig() {
    bindings = {{{'E', 0, -1},
                 {VK_UP, 'W', 8},
                 {VK_LEFT, 'A', 10},
                 {VK_RIGHT, 'D', 11},
                 {VK_DOWN, 'S', 9},
                 {VK_LSHIFT, 0, -1},
                 {VK_RSHIFT, 0, -1},
                 {'G', 0, 12},
                 {VK_SPACE, 0, 14},
                 {'1', 0, 18},
                 {'2', 0, 19},
                 {VK_F6, 0, -1},
                 {VK_TAB, 0, 13},
                 {VK_LEFT, 'A', 4},
                 {VK_RIGHT, 'D', 6},
                 {VK_UP, 'W', 8},
                 {VK_DOWN, 'S', 9},
                 {VK_SHIFT, 0, 16},
                 {VK_BACK, 0, 17}}};
}

TaxiConfig TaxiConfig::Load(const std::filesystem::path &path) {
    TaxiConfig config;
    std::ifstream file(path);

    if (!file) {
        return config;
    }

    std::unordered_map<std::string, std::string> values;
    std::string section;
    for (std::string line; std::getline(file, line);) {
        line = Trim(line);

        if (line.empty() || line.front() == ';' || line.front() == '#') {
            continue;
        }

        if (line.front() == '[' && line.back() == ']') {
            section = Trim(line.substr(1, line.size() - 2));
            continue;
        }

        const auto equals = line.find('=');

        if (equals == std::string::npos) {
            continue;
        }

        values[section + "." + Trim(line.substr(0, equals))] = Trim(line.substr(equals + 1));
    }

    config.fareMultiplier =
        ReadFloat(values, "Gameplay.FareMultiplier", config.fareMultiplier, 0.0F, 100.0F);
    config.hailRadius = ReadFloat(values, "Gameplay.HailRadius", config.hailRadius, 1.0F, 100.0F);
    config.minimumTripDistance = ReadFloat(values, "Gameplay.MinimumTripDistance",
                                           config.minimumTripDistance, 0.0F, 1000.0F);
    config.passengerWaitMs =
        ReadInt(values, "Gameplay.PassengerWaitMs", config.passengerWaitMs, 1000, 60000);
    config.travelCruiseSpeed =
        ReadInt(values, "Gameplay.TravelCruiseSpeed", config.travelCruiseSpeed, 1, 100);
    config.wanderCruiseSpeed =
        ReadInt(values, "Gameplay.WanderCruiseSpeed", config.wanderCruiseSpeed, 1, 100);
    config.fadeOutMs = ReadInt(values, "Gameplay.FadeOutMs", config.fadeOutMs, 250, 10000);
    config.fadeInMs = ReadInt(values, "Gameplay.FadeInMs", config.fadeInMs, 250, 10000);
    config.freeRides = ReadInt(values, "Gameplay.FreeRides", config.freeRides, 0, 1);
    config.cameraForwardOffset =
        ReadFloat(values, "Camera.ForwardOffset", config.cameraForwardOffset, -1.0F, 1.0F);
    config.cameraUpOffset =
        ReadFloat(values, "Camera.UpOffset", config.cameraUpOffset, -1.0F, 1.0F);
    config.cameraNearClip =
        ReadFloat(values, "Camera.NearClip", config.cameraNearClip, 0.01F, 1.0F);
    config.cameraFov = ReadFloat(values, "Camera.FOV", config.cameraFov, 30.0F, 120.0F);
    config.cameraMouseSensitivity =
        ReadFloat(values, "Camera.MouseSensitivity", config.cameraMouseSensitivity, 0.0F, 2.0F);
    config.cameraMaxYawDegrees =
        ReadFloat(values, "Camera.MaxYawDegrees", config.cameraMaxYawDegrees, 30.0F, 170.0F);
    // VC safe font enum verified by TestMenu: 0 Rage/Gothic, 1 Subtitles,
    // 2 Pricedown. Never pass the previously assumed raw value 3.
    config.menuFontStyle = ReadInt(values, "UI.MenuFontStyle", config.menuFontStyle, 0, 2);
    config.waypointMaximumRoadSnapDistance =
        ReadFloat(values, "Waypoint.MaximumRoadSnapDistance",
                  config.waypointMaximumRoadSnapDistance, 5.0F, 100.0F);

    for (std::size_t i = 0; i < config.bindings.size(); ++i) {
        config.bindings[i].virtualKey =
            ReadInt(values, (std::string("Keyboard.") + kActionNames[i]).c_str(),
                    config.bindings[i].virtualKey, 0, 255);
        config.bindings[i].alternateVirtualKey =
            ReadInt(values, (std::string("KeyboardAlternate.") + kActionNames[i]).c_str(),
                    config.bindings[i].alternateVirtualKey, 0, 255);
        config.bindings[i].padButton =
            ReadInt(values, (std::string("Gamepad.") + kActionNames[i]).c_str(),
                    config.bindings[i].padButton, -1, 19);
    }

    return config;
}

void TaxiConfig::SaveDebugSettings(const std::filesystem::path &path) const {
    const std::string iniPath = path.string();
    const auto write = [&iniPath](const char *section, const char *key, int value) {
        const std::string text = std::to_string(value);
        WritePrivateProfileStringA(section, key, text.c_str(), iniPath.c_str());
    };

    const auto writeFloat = [&iniPath](const char *section, const char *key, float value) {
        const std::string text = std::to_string(value);
        WritePrivateProfileStringA(section, key, text.c_str(), iniPath.c_str());
    };

    for (std::size_t i = 0; i < bindings.size(); ++i) {
        write("Keyboard", kActionNames[i], bindings[i].virtualKey);
        write("KeyboardAlternate", kActionNames[i], bindings[i].alternateVirtualKey);
        write("Gamepad", kActionNames[i], bindings[i].padButton);
    }

    write("Gameplay", "FreeRides", freeRides);
    write("UI", "MenuFontStyle", menuFontStyle);
    writeFloat("Camera", "FOV", cameraFov);
    WritePrivateProfileStringA(nullptr, nullptr, nullptr, iniPath.c_str());
}

const ActionBinding &TaxiConfig::Binding(TaxiAction action) const {
    return bindings.at(static_cast<std::size_t>(action));
}
