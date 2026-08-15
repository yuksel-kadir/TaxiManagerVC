#include "integrations/MenuMapIntegration.h"

#include <Windows.h>

bool MenuMapIntegration::TryGetWaypoint(CVector &position) {
    const HMODULE module = GetModuleHandleA("MenuMapVC.asi");

    if (!module) {
        return false;
    }

    using GetWaypoint = bool(__cdecl *)(float *, float *, float *);
    const auto getWaypoint =
        reinterpret_cast<GetWaypoint>(GetProcAddress(module, "MenuMapVC_GetWaypoint"));

    if (!getWaypoint) {
        return false;
    }

    return getWaypoint(&position.x, &position.y, &position.z);
}
