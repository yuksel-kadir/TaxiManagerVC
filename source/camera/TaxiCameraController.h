#pragma once

#include <CVector.h>
#include <cstdint>

class CPlayerPed;
class CVehicle;
class TaxiConfig;

enum class TaxiCameraState : std::uint8_t { Inactive, Requested, Active };

class TaxiCameraController final {
  public:
    void Process(CPlayerPed *player, CVehicle *taxi, bool sessionEligible, bool togglePressed,
                 const TaxiConfig &config);
    void UpdateBeforeCameraProcess(CPlayerPed *player, CVehicle *taxi, const TaxiConfig &config);
    void Cancel(CPlayerPed *player, CVehicle *taxi);
    void Reset() noexcept;

  private:
    TaxiCameraState state_{TaxiCameraState::Inactive};
    int taxiHandle_{-1};
    int savedTargetHandle_{-1};
    short savedMode_{};
    unsigned int requestedAt_{};
    float viewYaw_{};
    bool nearClipOwned_{};
    bool savedUseNearClip_{};
    float savedNearClip_{};
    float appliedNearClip_{};
    bool fovOwned_{};
    float savedFov_{};
    float appliedFov_{};
    CVector localEyeAnchor_{};
    bool hasLocalEyeAnchor_{};
    bool playerRenderOwned_{};
    bool savedRenderPedInCar_{};

    bool CanActivate(CPlayerPed *player, CVehicle *taxi) const;
    bool EnvironmentIsSafe() const;
    bool OwnsRequest(CVehicle *taxi) const;
    bool OwnsPassengerCamera() const;
    bool StillOwnsVectorCamera() const;
    void CaptureEyeAnchor(CPlayerPed *player, CVehicle *taxi, const TaxiConfig &config);
    void UpdatePassengerView(CPlayerPed *player, CVehicle *taxi, const TaxiConfig &config);
    void ApplyFov(const TaxiConfig &config);
    void RestoreFovIfApplied();
    void Activate(CPlayerPed *player, CVehicle *taxi, const TaxiConfig &config);
    void RestoreIfOwned(CPlayerPed *player, CVehicle *taxi);
};
