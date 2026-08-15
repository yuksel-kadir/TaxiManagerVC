#include "input/InputManager.h"

#include <CPad.h>
#include <Windows.h>

short InputManager::GamepadValue(int button) {
    if (button < 0) {
        return 0;
    }

    const CPad *pad = CPad::GetPad(0);

    if (!pad) {
        return 0;
    }

    // NewState/OldState contain GTA's combined keyboard and controller mapping.
    // PCTempJoyState is the physical controller state, so a keyboard driving key
    // cannot accidentally trigger a TaxiManager gamepad action too.
    const CControllerState &state = pad->PCTempJoyState;
    const short *values = &state.LeftStickX;
    return button <= 19 ? values[button] : 0;
}

bool InputManager::JustPressed(TaxiAction action, const TaxiConfig &config) {
    const auto &binding = config.Binding(action);
    const auto keyboardJustPressed = [this](int virtualKey) {
        if (virtualKey <= 0 || virtualKey >= static_cast<int>(keyboardPrevious_.size())) {
            return false;
        }

        const bool down = (GetAsyncKeyState(virtualKey) & 0x8000) != 0;
        const bool pressed = down && !keyboardPrevious_[virtualKey];
        keyboardPrevious_[virtualKey] = down;
        return pressed;
    };

    const bool keyboardPressed = keyboardJustPressed(binding.virtualKey);
    const bool alternateKeyboardPressed = keyboardJustPressed(binding.alternateVirtualKey);
    bool padPressed = false;

    if (binding.padButton >= 0 && binding.padButton < static_cast<int>(gamepadPrevious_.size())) {
        const bool down = GamepadValue(binding.padButton) != 0;
        padPressed = down && !gamepadPrevious_[binding.padButton];
        gamepadPrevious_[binding.padButton] = down;
    }

    return keyboardPressed || alternateKeyboardPressed || padPressed;
}

void InputManager::Reset() {
    keyboardPrevious_.fill(false);
    gamepadPrevious_.fill(false);
}
