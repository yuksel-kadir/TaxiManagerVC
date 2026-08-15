#pragma once

#include "config/TaxiConfig.h"
#include "core/TaxiTypes.h"

class CPlayerPed;
class CVehicle;

class TaxiOperations final {
  public:
    static bool IsUsableTaxi(const CVehicle *vehicle);
    static CVehicle *FindNearest(const CVector &playerPosition, float radius);
    static void PrepareForPassenger(CPlayerPed *player, CVehicle *taxi);
    // Returns false when Vice City's road search cannot produce a usable route.
    static bool DriveTo(CVehicle *taxi, const Destination &destination, const TaxiConfig &config);
    static void MaintainDriving(CVehicle *taxi);
    static void Stop(CVehicle *taxi);
    static void Release(CVehicle *taxi, const TaxiConfig &config);
    static void Teleport(CVehicle *taxi, const Destination &destination);
    static int Fare(const CVector &from, const CVector &to, float multiplier);
};
