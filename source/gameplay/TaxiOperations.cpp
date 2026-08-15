#include "gameplay/TaxiOperations.h"

#include <CAutomobile.h>
#include <CCarCtrl.h>
#include <CPools.h>
#include <CStreaming.h>
#include <CTheScripts.h>
#include <CTimer.h>
#include <CWorld.h>
#include <eModelID.h>
#include <extensions/ScriptCommands.h>
#include <extensions/scripting/ScriptCommandNames.h>
#include <plugin.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
constexpr short kTaxiModels[] = {MODEL_TAXI, MODEL_CABBIE, MODEL_KAUFMAN};
constexpr short kTaxiDriverModels[] = {28, 74};

bool IsModel(short value, const short *values, std::size_t count) {
    return std::find(values, values + count, value) != values + count;
}

float Distance(const CVector &left, const CVector &right) {
    const float x = left.x - right.x;
    const float y = left.y - right.y;
    const float z = left.z - right.z;
    return std::sqrt(x * x + y * y + z * z);
}

} // namespace

bool TaxiOperations::IsUsableTaxi(const CVehicle *vehicle) {
    return vehicle && vehicle->m_nCreatedBy == RANDOM_VEHICLE && !vehicle->bIsLocked &&
           IsModel(vehicle->m_nModelIndex, kTaxiModels, 3) && vehicle->m_pDriver &&
           IsModel(vehicle->m_pDriver->m_nModelIndex, kTaxiDriverModels, 2) &&
           vehicle->m_pDriver->m_fHealth > 1.0F;
}

CVehicle *TaxiOperations::FindNearest(const CVector &playerPosition, float radius) {
    CVehicle *nearest{};
    float nearestDistance = radius;

    if (!CPools::ms_pVehiclePool) {
        return nullptr;
    }

    for (CVehicle *vehicle : CPools::ms_pVehiclePool) {
        if (!IsUsableTaxi(vehicle)) {
            continue;
        }

        const float distance = Distance(playerPosition, vehicle->GetPosition());

        if (distance < nearestDistance) {
            nearest = vehicle;
            nearestDistance = distance;
        }
    }

    return nearest;
}

void TaxiOperations::PrepareForPassenger(CPlayerPed *player, CVehicle *taxi) {
    Stop(taxi);
    for (CPed *passenger : taxi->m_passengers) {
        if (passenger) {
            passenger->SetObjective(eObjective::OBJECTIVE_LEAVE_CAR, taxi);
        }
    }

    player->ClearObjective();
    player->SetObjective(eObjective::OBJECTIVE_ENTER_CAR_AS_PASSENGER, taxi);
}

bool TaxiOperations::DriveTo(CVehicle *taxi, const Destination &destination,
                             const TaxiConfig &config) {
    if (!taxi) {
        return false;
    }

    // Stop() applies a temporary handbrake. It must be cleared before the new
    // mission is assigned or the taxi can remain stationary.
    taxi->m_autoPilot.m_nAnimationId = eCarTempAction::TEMPACT_NONE;
    taxi->m_autoPilot.m_nAnimationTime = 0;
    CVector routePosition = destination.position;
    routePosition.z += taxi->GetDistanceFromCentreOfMassToBaseOfModel();

    // JoinCarWithRoadSystemGotoCoors has counter-intuitive VC semantics: true
    // means that fewer than two road nodes were found and the game must use a
    // direct approach. That is safe only when already close to the target. For
    // a longer trip it normally indicates a disabled bridge, another island,
    // or a destination that is not connected to the active car-path graph.
    constexpr float kMaximumDirectApproachDistance = 30.0F;
    const CVector delta = routePosition - taxi->GetPosition();
    const float horizontalDistance = std::sqrt(delta.x * delta.x + delta.y * delta.y);
    const bool requiresDirectApproach =
        CCarCtrl::JoinCarWithRoadSystemGotoCoors(taxi, routePosition, false);

    if (requiresDirectApproach && horizontalDistance > kMaximumDirectApproachDistance) {
        taxi->m_autoPilot.m_nCarMission = eCarMission::MISSION_STOP_FOREVER;
        taxi->m_autoPilot.m_nCruiseSpeed = 0;
        return false;
    }

    // Use VC's script command rather than partially reproducing it. Besides
    // assigning the accurate mission, it switches an ambient car from SIMPLE
    // to PHYSICS status and initializes VC's private anti-reverse state.
    plugin::Command<plugin::Commands::SET_CAR_CHANGE_LANE>(taxi, true);
    plugin::Command<plugin::Commands::CAR_GOTO_COORDINATES_ACCURATE>(
        taxi, destination.position.x, destination.position.y, destination.position.z);
    taxi->m_autoPilot.m_nCruiseSpeed = static_cast<unsigned char>(config.travelCruiseSpeed);
    taxi->m_autoPilot.m_fMaxTrafficSpeed = static_cast<float>(config.travelCruiseSpeed);
    taxi->m_autoPilot.m_nDrivingStyle = eCarDrivingStyle::DRIVINGSTYLE_AVOID_CARS;
    taxi->m_autoPilot.m_nTimeToStartMission = CTimer::m_snTimeInMilliseconds;
    MaintainDriving(taxi);
    plugin::Command<plugin::Commands::SET_CAR_CAN_BE_DAMAGED>(taxi, false);
    static_cast<CAutomobile *>(taxi)->SetTaxiLight(true);
    return true;
}

void TaxiOperations::MaintainDriving(CVehicle *taxi) {
    if (!taxi) {
        return;
    }

    taxi->bEngineOn = true;
}

void TaxiOperations::Stop(CVehicle *taxi) {
    if (!taxi) {
        return;
    }

    taxi->m_autoPilot.m_nAnimationId = eCarTempAction::TEMPACT_HANDBRAKESTRAIGHT;
    taxi->m_autoPilot.m_nAnimationTime = CTimer::m_snTimeInMilliseconds + 2500;
    taxi->m_autoPilot.m_nCarMission = eCarMission::MISSION_STOP_FOREVER;
    taxi->m_autoPilot.m_nTimeToStartMission = CTimer::m_snTimeInMilliseconds;
    taxi->m_autoPilot.m_nCruiseSpeed = 0;
}

void TaxiOperations::Release(CVehicle *taxi, const TaxiConfig &config) {
    if (!taxi) {
        return;
    }

    plugin::Command<plugin::Commands::SET_CAR_CAN_BE_DAMAGED>(taxi, true);
    taxi->m_eDoorLock = DOORLOCK_UNLOCKED;
    static_cast<CAutomobile *>(taxi)->SetTaxiLight(false);
    CCarCtrl::JoinCarWithRoadSystem(taxi);
    taxi->bEngineOn = true;
    taxi->m_autoPilot.m_nDrivingStyle = eCarDrivingStyle::DRIVINGSTYLE_STOP_FOR_CARS;
    taxi->m_autoPilot.m_nCarMission = eCarMission::MISSION_CRUISE;
    taxi->m_autoPilot.m_nAnimationId = eCarTempAction::TEMPACT_NONE;
    taxi->m_autoPilot.m_nAnimationTime = 0;
    taxi->m_autoPilot.m_nTimeToStartMission = CTimer::m_snTimeInMilliseconds;
    taxi->m_autoPilot.m_nCruiseSpeed = static_cast<unsigned char>(config.wanderCruiseSpeed);
    taxi->m_autoPilot.m_fMaxTrafficSpeed = static_cast<float>(config.wanderCruiseSpeed);
}

void TaxiOperations::Teleport(CVehicle *taxi, const Destination &destination) {
    CTimer::Suspend();
    CVector position = destination.position;
    plugin::Command<plugin::Commands::REQUEST_COLLISION>(position.x, position.y);
    CStreaming::LoadSceneCollision(&position);
    CStreaming::LoadScene(&position);

    if (destination.resolveGroundZ) {
        bool groundFound = false;
        const float groundZ =
            CWorld::FindGroundZFor3DCoord(position.x, position.y, 1000.0F, &groundFound);

        if (groundFound) {
            position.z = groundZ + taxi->GetDistanceFromCentreOfMassToBaseOfModel();
        }
    }

    CTheScripts::ClearSpaceForMissionEntity(position, taxi);
    taxi->Teleport(position);
    taxi->SetHeading(destination.heading);
    CTimer::Resume();
}

int TaxiOperations::Fare(const CVector &from, const CVector &to, float multiplier) {
    const double value = static_cast<double>(Distance(from, to)) * std::max(0.0F, multiplier);
    return static_cast<int>(
        std::clamp(value, 0.0, static_cast<double>(std::numeric_limits<int>::max())));
}
