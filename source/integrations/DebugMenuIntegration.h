#pragma once

#include <array>
#include <filesystem>
#include <string>

class TaxiConfig;

class DebugMenuIntegration final {
  public:
    void TryRegister(TaxiConfig &config, const std::filesystem::path &configPath);

  private:
    std::array<std::string, 256> keyboardLabels_{};
    std::array<const char *, 256> keyboardLabelPointers_{};
    bool registered_{};

    void BuildKeyboardLabels();
};
