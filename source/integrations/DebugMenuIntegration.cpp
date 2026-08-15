#include "integrations/DebugMenuIntegration.h"

#include "config/TaxiConfig.h"

#include <Windows.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>

namespace {
struct DebugMenuEntry;
using AddInt32 = DebugMenuEntry *(__cdecl *)(const char *, const char *, std::int32_t *,
                                             void(__cdecl *)(), std::int32_t, std::int32_t,
                                             std::int32_t, const char **);
using AddFloat32 = DebugMenuEntry *(__cdecl *)(const char *, const char *, float *,
                                               void(__cdecl *)(), float, float, float);
using SetWrap = void(__cdecl *)(DebugMenuEntry *, bool);

constexpr std::array<const char *, 17> kActionNames{"Hail",
                                                    "Toggle destinations",
                                                    "Previous destination",
                                                    "Next destination",
                                                    "Confirm destination",
                                                    "Skip travel",
                                                    "Stop taxi",
                                                    "Lock doors",
                                                    "Unlock doors",
                                                    "Toggle first person",
                                                    "Open destination browser",
                                                    "Browser previous category",
                                                    "Browser next category",
                                                    "Browser previous destination",
                                                    "Browser next destination",
                                                    "Browser confirm",
                                                    "Browser cancel"};

std::array<const char *, 21> kControllerLabels{
    "Disabled",         "Left Stick X",    "Left Stick Y",    "Right Stick X",
    "Right Stick Y",    "Left Shoulder 1", "Left Shoulder 2", "Right Shoulder 1",
    "Right Shoulder 2", "D-pad Up",        "D-pad Down",      "D-pad Left",
    "D-pad Right",      "Start",           "Select",          "Square",
    "Triangle",         "Cross",           "Circle",          "Left Stick Click",
    "Right Stick Click"};

TaxiConfig *activeConfig{};
std::filesystem::path activeConfigPath;

void __cdecl SaveSettings() {
    if (activeConfig && !activeConfigPath.empty()) {
        activeConfig->SaveDebugSettings(activeConfigPath);
    }
}

void SetLabel(std::array<std::string, 256> &labels, int key, const char *name) {
    labels.at(static_cast<std::size_t>(key)) = name;
}

} // namespace

void DebugMenuIntegration::TryRegister(TaxiConfig &config,
                                       const std::filesystem::path &configPath) {
    activeConfig = &config;
    activeConfigPath = configPath;

    if (registered_) {
        return;
    }

    HMODULE module = GetModuleHandleA("debugmenu.dll");

    if (!module) {
        module = LoadLibraryA("debugmenu.dll");
    }

    if (!module) {
        return;
    }

    const auto addInt32 = reinterpret_cast<AddInt32>(GetProcAddress(module, "DebugMenuAddInt32"));
    const auto addFloat32 =
        reinterpret_cast<AddFloat32>(GetProcAddress(module, "DebugMenuAddFloat32"));
    const auto setWrap = reinterpret_cast<SetWrap>(GetProcAddress(module, "DebugMenuEntrySetWrap"));

    if (!addInt32 || !addFloat32 || !setWrap) {
        return;
    }

    BuildKeyboardLabels();
    for (std::size_t i = 0; i < config.bindings.size(); ++i) {
        ActionBinding &binding = config.bindings[i];
        DebugMenuEntry *entry =
            addInt32("TaxiManager|Controls|Keyboard", kActionNames[i], &binding.virtualKey,
                     SaveSettings, 1, 0, 255, keyboardLabelPointers_.data());
        setWrap(entry, true);

        entry = addInt32("TaxiManager|Controls|Keyboard Alternate", kActionNames[i],
                         &binding.alternateVirtualKey, SaveSettings, 1, 0, 255,
                         keyboardLabelPointers_.data());
        setWrap(entry, true);

        entry = addInt32("TaxiManager|Controls|Gamepad", kActionNames[i], &binding.padButton,
                         SaveSettings, 1, -1, 19, kControllerLabels.data());
        setWrap(entry, true);
    }

    static const char *booleanLabels[]{"Off", "On"};
    DebugMenuEntry *freeRides = addInt32("TaxiManager|Gameplay", "Free rides", &config.freeRides,
                                         SaveSettings, 1, 0, 1, booleanLabels);
    setWrap(freeRides, true);
    addFloat32("TaxiManager|Camera", "Passenger FOV", &config.cameraFov, SaveSettings, 1.0F, 30.0F,
               120.0F);
    static const char *menuFontLabels[]{"Gothic", "Subtitles", "Pricedown"};
    DebugMenuEntry *menuFont = addInt32("TaxiManager|UI", "Menu font style", &config.menuFontStyle,
                                        SaveSettings, 1, 0, 2, menuFontLabels);
    setWrap(menuFont, true);
    registered_ = true;
}

void DebugMenuIntegration::BuildKeyboardLabels() {
    for (std::size_t i = 0; i < keyboardLabels_.size(); ++i) {
        keyboardLabels_[i] = "VK " + std::to_string(i);
    }

    SetLabel(keyboardLabels_, 0, "Disabled");
    SetLabel(keyboardLabels_, VK_LBUTTON, "Left Mouse");
    SetLabel(keyboardLabels_, VK_RBUTTON, "Right Mouse");
    SetLabel(keyboardLabels_, VK_MBUTTON, "Middle Mouse");
    SetLabel(keyboardLabels_, VK_BACK, "Backspace");
    SetLabel(keyboardLabels_, VK_TAB, "Tab");
    SetLabel(keyboardLabels_, VK_RETURN, "Enter");
    SetLabel(keyboardLabels_, VK_SHIFT, "Shift");
    SetLabel(keyboardLabels_, VK_CONTROL, "Control");
    SetLabel(keyboardLabels_, VK_MENU, "Alt");
    SetLabel(keyboardLabels_, VK_PAUSE, "Pause");
    SetLabel(keyboardLabels_, VK_CAPITAL, "Caps Lock");
    SetLabel(keyboardLabels_, VK_ESCAPE, "Escape");
    SetLabel(keyboardLabels_, VK_SPACE, "Space");
    SetLabel(keyboardLabels_, VK_PRIOR, "Page Up");
    SetLabel(keyboardLabels_, VK_NEXT, "Page Down");
    SetLabel(keyboardLabels_, VK_END, "End");
    SetLabel(keyboardLabels_, VK_HOME, "Home");
    SetLabel(keyboardLabels_, VK_LEFT, "Left Arrow");
    SetLabel(keyboardLabels_, VK_UP, "Up Arrow");
    SetLabel(keyboardLabels_, VK_RIGHT, "Right Arrow");
    SetLabel(keyboardLabels_, VK_DOWN, "Down Arrow");
    SetLabel(keyboardLabels_, VK_INSERT, "Insert");
    SetLabel(keyboardLabels_, VK_DELETE, "Delete");

    for (int key = '0'; key <= '9'; ++key) {
        keyboardLabels_[key] = static_cast<char>(key);
    }

    for (int key = 'A'; key <= 'Z'; ++key) {
        keyboardLabels_[key] = static_cast<char>(key);
    }

    for (int key = VK_NUMPAD0; key <= VK_NUMPAD9; ++key) {
        keyboardLabels_[key] = "Numpad " + std::to_string(key - VK_NUMPAD0);
    }

    for (int key = VK_F1; key <= VK_F24; ++key) {
        keyboardLabels_[key] = "F" + std::to_string(key - VK_F1 + 1);
    }

    for (std::size_t i = 0; i < keyboardLabels_.size(); ++i) {
        keyboardLabelPointers_[i] = keyboardLabels_[i].c_str();
    }
}
