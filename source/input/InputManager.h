#pragma once

#include "config/TaxiConfig.h"

#include <array>

class InputManager final {
  public:
    bool JustPressed(TaxiAction action, const TaxiConfig &config);
    void Consume(TaxiAction action, const TaxiConfig &config);
    void Reset();

  private:
    std::array<bool, 256> keyboardPrevious_{};
    std::array<bool, 20> gamepadPrevious_{};
    static short GamepadValue(int button);
};
