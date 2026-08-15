#include "camera/TaxiCameraController.h"
#include "config/TaxiConfig.h"

#include <algorithm>
#include <cmath>

#include <CCamera.h>
#include <CCutsceneMgr.h>
#include <CDraw.h>
#include <CMenuManager.h>
#include <CPad.h>
#include <CPedIK.h>
#include <CPlayerPed.h>
#include <CPools.h>
#include <CReplay.h>
#include <CTimer.h>
#include <CVehicle.h>
#include <eModelID.h>
#include <plugin.h>

namespace {
constexpr short kTaxiModels[] = {MODEL_TAXI, MODEL_CABBIE, MODEL_KAUFMAN};
constexpr int CAMCONTROL_GAME = 0;
constexpr int CAMCONTROL_SCRIPT = 1;
constexpr short kJumpCut = 2;
constexpr unsigned int kRequestTimeoutMs = 1000U;
constexpr unsigned int kPedHeadNode = 2U;
constexpr float kLookAheadDistance = 10.0F;
constexpr float kDegreesToRadians = 0.01745329252F;

bool IsTaxiModel(short model) {
    return model == kTaxiModels[0] || model == kTaxiModels[1] || model == kTaxiModels[2];
}

bool IsNormalVehicleCamera(short mode) {
    return mode == MODE_CAM_ON_A_STRING || mode == MODE_TOP_DOWN || mode == MODE_1ST_PERSON;
}

} // namespace

void TaxiCameraController::Process(CPlayerPed *player, CVehicle *taxi, bool sessionEligible,
                                   bool togglePressed, const TaxiConfig &config) {
    if (state_ == TaxiCameraState::Inactive) {
        if (togglePressed && sessionEligible && CanActivate(player, taxi)) {
            Activate(player, taxi, config);
            ApplyFov(config);
        }

        return;
    }

    if (!taxi || !CPools::ms_pVehiclePool || CPools::GetVehicle(taxiHandle_) != taxi) {
        if (StillOwnsVectorCamera()) {
            RestoreFovIfApplied();
            TheCamera.RestoreWithJumpCut();
        }

        Reset();
        return;
    }

    const CCam &activeCam = TheCamera.m_asCams[TheCamera.m_nActiveCam];
    CPad *pad = CPad::GetPad(0);
    const bool cameraCyclePressed =
        pad && (pad->CycleCameraModeUpJustDown() || pad->CycleCameraModeDownJustDown());

    if (cameraCyclePressed && StillOwnsVectorCamera()) {
        if (OwnsRequest(taxi)) {
            RestoreIfOwned(player, taxi);
        } else {
            RestoreFovIfApplied();
            TheCamera.RestoreWithJumpCut();
            Reset();
        }

        return;
    }

    if (TheCamera.m_nTransitionState != 0 && state_ == TaxiCameraState::Active) {
        // A transition after activation belongs to another camera decision.
        Reset();
        return;
    }

    if (!OwnsRequest(taxi)) {
        // Another camera system made a decision. Relinquish our bookkeeping
        // without restoring over the new owner, mode, or target.
        Reset();
        return;
    }

    if (!sessionEligible || !EnvironmentIsSafe()) {
        RestoreIfOwned(player, taxi);
        return;
    }

    if (togglePressed) {
        RestoreIfOwned(player, taxi);
        return;
    }

    if (state_ == TaxiCameraState::Requested) {
        if (OwnsPassengerCamera()) {
            state_ = TaxiCameraState::Active;
        } else if (CTimer::m_snTimeInMilliseconds - requestedAt_ >= kRequestTimeoutMs) {
            RestoreIfOwned(player, taxi);
            return;
        }
    }

    ApplyFov(config);
}

void TaxiCameraController::UpdateBeforeCameraProcess(CPlayerPed *player, CVehicle *taxi,
                                                     const TaxiConfig &config) {
    if (state_ == TaxiCameraState::Inactive || !player || !taxi || !CPools::ms_pVehiclePool ||
        CPools::GetVehicle(taxiHandle_) != taxi) {
        return;
    }

    // This callback runs after CWorld has moved the taxi and immediately before
    // CCamera::Process consumes the fixed-camera source. Do not update if the
    // passenger relationship or camera ownership changed since gameplay logic
    // last ran; the normal Process path will perform the appropriate cleanup.
    if (!player->m_bInVehicle || player->m_pVehicle != taxi || taxi->m_pDriver == player ||
        player->m_ePedState != ePedState::PEDSTATE_DRIVING || !OwnsRequest(taxi) ||
        !EnvironmentIsSafe()) {
        return;
    }

    UpdatePassengerView(player, taxi, config);
}

void TaxiCameraController::Cancel(CPlayerPed *player, CVehicle *taxi) {
    if (state_ == TaxiCameraState::Inactive) {
        return;
    }

    if (OwnsRequest(taxi)) {
        RestoreIfOwned(player, taxi);
    } else {
        if (StillOwnsVectorCamera()) {
            RestoreFovIfApplied();
            TheCamera.RestoreWithJumpCut();
        }

        Reset();
    }
}

void TaxiCameraController::Reset() noexcept {
    if (nearClipOwned_ && TheCamera.m_bUseNearClipScript &&
        std::abs(TheCamera.m_fNearClipScript - appliedNearClip_) < 0.0001F) {
        TheCamera.m_bUseNearClipScript = savedUseNearClip_;
        TheCamera.m_fNearClipScript = savedNearClip_;
    }

    if (playerRenderOwned_) {
        if (CPlayerPed *player = FindPlayerPed(); player && !player->bRenderPedInCar) {
            player->bRenderPedInCar = savedRenderPedInCar_;
        }
    }

    state_ = TaxiCameraState::Inactive;
    taxiHandle_ = -1;
    savedTargetHandle_ = -1;
    savedMode_ = MODE_NONE;
    requestedAt_ = 0;
    viewYaw_ = 0.0F;
    nearClipOwned_ = false;
    appliedNearClip_ = 0.0F;
    fovOwned_ = false;
    savedFov_ = 0.0F;
    appliedFov_ = 0.0F;
    localEyeAnchor_ = CVector{};
    hasLocalEyeAnchor_ = false;
    playerRenderOwned_ = false;
    savedRenderPedInCar_ = true;
}

bool TaxiCameraController::CanActivate(CPlayerPed *player, CVehicle *taxi) const {
    if (!player || !taxi || !IsTaxiModel(taxi->m_nModelIndex) || !CPools::ms_pVehiclePool) {
        return false;
    }

    if (!player->m_bInVehicle || player->m_pVehicle != taxi || taxi->m_pDriver == player) {
        return false;
    }

    if (!EnvironmentIsSafe() || TheCamera.m_nTransitionState != 0) {
        return false;
    }

    const CCam &activeCam = TheCamera.m_asCams[TheCamera.m_nActiveCam];
    return TheCamera.m_nWhoIsInControlOfTheCamera == CAMCONTROL_GAME &&
           IsNormalVehicleCamera(activeCam.m_nCamMode) && activeCam.m_pCamTargetEntity == taxi &&
           TheCamera.m_pTargetEntity == taxi;
}

bool TaxiCameraController::EnvironmentIsSafe() const {
    const CPad *pad = CPad::GetPad(0);
    return pad && !pad->DisablePlayerControls && !FrontEndMenuManager.m_bMenuActive &&
           !TheCamera.m_bFading && !TheCamera.m_bWideScreenOn && !CCutsceneMgr::ms_running &&
           CReplay::Mode == 0;
}

bool TaxiCameraController::OwnsRequest(CVehicle *taxi) const {
    if (!taxi || TheCamera.m_nWhoIsInControlOfTheCamera != CAMCONTROL_SCRIPT ||
        !TheCamera.m_bLookingAtVector || TheCamera.m_pTargetEntity != taxi) {
        return false;
    }

    const CCam &activeCam = TheCamera.m_asCams[TheCamera.m_nActiveCam];
    return state_ == TaxiCameraState::Requested ||
           (activeCam.m_nCamMode == MODE_FIXED && activeCam.m_bCamLookingAtVector);
}

bool TaxiCameraController::OwnsPassengerCamera() const {
    const CCam &activeCam = TheCamera.m_asCams[TheCamera.m_nActiveCam];
    return StillOwnsVectorCamera() && activeCam.m_nCamMode == MODE_FIXED &&
           activeCam.m_bCamLookingAtVector && TheCamera.m_nTransitionState == 0;
}

bool TaxiCameraController::StillOwnsVectorCamera() const {
    if (state_ == TaxiCameraState::Inactive || !TheCamera.m_bLookingAtVector ||
        TheCamera.m_nWhoIsInControlOfTheCamera != CAMCONTROL_SCRIPT || !CPools::ms_pVehiclePool) {
        return false;
    }

    CVehicle *taxi = CPools::GetVehicle(taxiHandle_);
    return taxi && TheCamera.m_pTargetEntity == taxi;
}

void TaxiCameraController::CaptureEyeAnchor(CPlayerPed *player, CVehicle *taxi,
                                            const TaxiConfig &config) {
    RwV3d head{};
    player->m_PedIK.GetComponentPosition(head, kPedHeadNode);

    const CVector &forward = taxi->GetMatrix().GetForward();
    const CVector &right = taxi->GetMatrix().GetRight();
    const CVector &up = taxi->GetMatrix().GetUp();
    const CVector desiredSource{
        head.x + forward.x * config.cameraForwardOffset + up.x * config.cameraUpOffset,
        head.y + forward.y * config.cameraForwardOffset + up.y * config.cameraUpOffset,
        head.z + forward.z * config.cameraForwardOffset + up.z * config.cameraUpOffset};
    const CVector delta = desiredSource - taxi->GetPosition();

    localEyeAnchor_ = CVector{delta.x * right.x + delta.y * right.y + delta.z * right.z,
                              delta.x * forward.x + delta.y * forward.y + delta.z * forward.z,
                              delta.x * up.x + delta.y * up.y + delta.z * up.z};
    hasLocalEyeAnchor_ = true;
}

void TaxiCameraController::UpdatePassengerView(CPlayerPed *player, CVehicle *taxi,
                                               const TaxiConfig &config) {
    if (!hasLocalEyeAnchor_) {
        CaptureEyeAnchor(player, taxi, config);
    }

    const CVector &forward = taxi->GetMatrix().GetForward();
    const CVector &right = taxi->GetMatrix().GetRight();
    const CVector &up = taxi->GetMatrix().GetUp();
    // The per-frame hook runs after CWorld has committed the vehicle matrix,
    // so the cabin anchor must use that matrix directly. Velocity prediction
    // here would advance the camera beyond the rendered taxi and cause jitter.
    const CVector &position = taxi->GetPosition();
    const float maximumYaw = config.cameraMaxYawDegrees * kDegreesToRadians;

    // Follow the reference Taxi Ride CLEO mouse behavior, but apply the
    // configured passenger-safe limit so the view cannot enter Tommy's body.
    viewYaw_ = std::clamp(viewYaw_ + CPad::NewMouseControllerState.x *
                                         config.cameraMouseSensitivity * kDegreesToRadians,
                          -maximumYaw, maximumYaw);
    const float forwardWeight = std::cos(viewYaw_);
    const float rightWeight = std::sin(viewYaw_);
    const CVector viewDirection{forward.x * forwardWeight + right.x * rightWeight,
                                forward.y * forwardWeight + right.y * rightWeight,
                                forward.z * forwardWeight + right.z * rightWeight};
    const CVector source{position.x + right.x * localEyeAnchor_.x + forward.x * localEyeAnchor_.y +
                             up.x * localEyeAnchor_.z,
                         position.y + right.y * localEyeAnchor_.x + forward.y * localEyeAnchor_.y +
                             up.y * localEyeAnchor_.z,
                         position.z + right.z * localEyeAnchor_.x + forward.z * localEyeAnchor_.y +
                             up.z * localEyeAnchor_.z};
    const CVector target{source.x + viewDirection.x * kLookAheadDistance,
                         source.y + viewDirection.y * kLookAheadDistance,
                         source.z + viewDirection.z * kLookAheadDistance};

    TheCamera.SetCamPositionForFixedMode(source, CVector{});
    TheCamera.m_vecFixedModeVector = target;

    // Vice City copies the fixed-camera parameters into the active CCam when the
    // mode starts. Updating only CCamera's template values would leave the
    // live camera at its original world position, so keep that owned copy in
    // sync as well. Ownership and MODE_FIXED are validated before this method
    // is called during an active session.
    CCam &activeCam = TheCamera.m_asCams[TheCamera.m_nActiveCam];

    if (activeCam.m_nCamMode == MODE_FIXED && activeCam.m_bCamLookingAtVector) {
        activeCam.m_vecCamFixedModeSource = source;
        activeCam.m_vecCamFixedModeVector = target;
        activeCam.m_vecCamFixedModeUpOffSet = CVector{};
    }
}

void TaxiCameraController::ApplyFov(const TaxiConfig &config) {
    if (!fovOwned_) {
        savedFov_ = CDraw::ms_fFOV;
        fovOwned_ = true;
    }

    appliedFov_ = config.cameraFov;
    TheCamera.m_asCams[TheCamera.m_nActiveCam].m_fFOV = appliedFov_;
    CDraw::SetFOV(appliedFov_);
}

void TaxiCameraController::RestoreFovIfApplied() {
    if (fovOwned_ && std::abs(CDraw::ms_fFOV - appliedFov_) < 0.0001F) {
        CDraw::SetFOV(savedFov_);
    }
}

void TaxiCameraController::Activate(CPlayerPed *player, CVehicle *taxi, const TaxiConfig &config) {
    const CCam &activeCam = TheCamera.m_asCams[TheCamera.m_nActiveCam];
    taxiHandle_ = CPools::GetVehicleRef(taxi);
    savedTargetHandle_ = CPools::GetVehicleRef(taxi);
    savedMode_ = activeCam.m_nCamMode;
    requestedAt_ = CTimer::m_snTimeInMilliseconds;
    viewYaw_ = 0.0F;
    CaptureEyeAnchor(player, taxi, config);
    savedUseNearClip_ = TheCamera.m_bUseNearClipScript;
    savedNearClip_ = TheCamera.m_fNearClipScript;
    appliedNearClip_ = config.cameraNearClip;
    nearClipOwned_ = true;
    TheCamera.SetNearClipScript(appliedNearClip_);
    savedRenderPedInCar_ = player->bRenderPedInCar;
    playerRenderOwned_ = true;
    player->bRenderPedInCar = false;
    state_ = TaxiCameraState::Requested;
    UpdatePassengerView(player, taxi, config);
    TheCamera.TakeControlNoEntity(TheCamera.m_vecFixedModeVector, kJumpCut, CAMCONTROL_SCRIPT);
}

void TaxiCameraController::RestoreIfOwned(CPlayerPed *player, CVehicle *taxi) {
    if (taxi && OwnsRequest(taxi) && savedTargetHandle_ == CPools::GetVehicleRef(taxi)) {
        RestoreFovIfApplied();
        TheCamera.RestoreWithJumpCut();

        if (savedMode_ != MODE_CAM_ON_A_STRING) {
            TheCamera.TakeControl(taxi, savedMode_, kJumpCut, CAMCONTROL_GAME);
        }
    }

    Reset();
}
