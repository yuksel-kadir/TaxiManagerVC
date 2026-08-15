#pragma once

#include "core/TaxiTypes.h"

#include <array>
#include <cstdint>
#include <filesystem>

class TaxiConfig final {
  public:
    float fareMultiplier{0.1F};
    float hailRadius{10.0F};
    float minimumTripDistance{20.0F};
    int passengerWaitMs{8000};
    int travelCruiseSpeed{25};
    int wanderCruiseSpeed{10};
    int fadeOutMs{1500};
    int fadeInMs{2500};
    float cameraForwardOffset{0.25F};
    float cameraUpOffset{0.06F};
    float cameraNearClip{0.05F};
    float cameraFov{70.0F};
    float cameraMouseSensitivity{0.1F};
    float cameraMaxYawDegrees{110.0F};
    int menuFontStyle{};
    float waypointMaximumRoadSnapDistance{30.0F};
    std::int32_t freeRides{};
    std::array<ActionBinding, 17> bindings{};

    TaxiConfig();
    static TaxiConfig Load(const std::filesystem::path &path);
    void SaveDebugSettings(const std::filesystem::path &path) const;
    const ActionBinding &Binding(TaxiAction action) const;
};
