#pragma once

#include "camera/TaxiCameraController.h"
#include "config/TaxiConfig.h"
#include "core/TaxiTypes.h"
#include "destinations/DestinationRepository.h"
#include "input/InputManager.h"
#include "integrations/DebugMenuIntegration.h"
#include "ui/Hud.h"

#include <filesystem>
#include <optional>
#include <vector>

class CPlayerPed;
class CVehicle;

class TaxiManager final {
  public:
    TaxiManager();
    void UpdateBeforeCameraProcess();

  private:
    struct DestinationCategory final {
        std::string name;
        std::optional<DestinationRegion> region;
        std::vector<std::size_t> destinationIds;
    };

    TaxiConfig config_;
    DestinationRepository destinations_;
    InputManager input_;
    Hud hud_;
    TaxiCameraController camera_;
    DebugMenuIntegration debugMenu_;
    TaxiSessionState state_{TaxiSessionState::Idle};
    int taxiHandle_{-1};
    std::vector<std::size_t> availableDestinations_;
    std::optional<Destination> mapWaypoint_;
    std::optional<Destination> tripDestination_;
    std::vector<DestinationCategory> browserCategories_;
    std::size_t selectionCursor_{};
    std::size_t browserCategoryCursor_{};
    std::size_t browserItemCursor_{};
    CVector segmentStart_{};
    int accumulatedFare_{};
    unsigned int stateStartedAt_{};
    unsigned int nextDestinationSyncAt_{};
    bool tripActive_{};
    bool destinationMenuVisible_{};
    bool destinationBrowserVisible_{};
    bool playerControlDisabled_{};
    bool wantedLimitSaved_{};
    int savedWantedLimit_{6};

    void Initialize();
    void Process();
    void DrawHud();
    void Shutdown();
    void BeginHail(CPlayerPed *player);
    void ProcessAwaitingEntry(CPlayerPed *player, CVehicle *taxi);
    void ProcessPassenger(CPlayerPed *player, CVehicle *taxi);
    void SynchronizeDestinations(unsigned int now);
    void ProcessDestinationMenus(CPlayerPed *player, CVehicle *taxi);
    void ProcessCompactDestinationMenu(CPlayerPed *player, CVehicle *taxi);
    void ProcessPassengerControls(CVehicle *taxi);
    void UpdateTripProgress(CPlayerPed *player, CVehicle *taxi);
    void ConfirmDestination(CPlayerPed *player, CVehicle *taxi);
    void BeginSkip(CVehicle *taxi);
    void ProcessFade(CPlayerPed *player, CVehicle *taxi);
    void CompleteTrip(CPlayerPed *player, CVehicle *taxi);
    void Cleanup(CPlayerPed *player, CVehicle *taxi, bool chargeFare,
                 bool delayTaxiRelease = false);
    void SelectCompactCategory(int offset);
    void SelectCompactDestination(int offset);
    void RefreshSelection();
    bool RefreshAvailableDestinations();
    void OpenDestinationBrowser(CPlayerPed *player, CVehicle *taxi);
    void CloseDestinationBrowser();
    void ProcessDestinationBrowser(CPlayerPed *player, CVehicle *taxi);
    void BuildDestinationBrowser(CPlayerPed *player = nullptr, CVehicle *taxi = nullptr);
    void BuildDestinationCategories();
    void SelectPlayerRegionCategory(CPlayerPed *player, CVehicle *taxi);
    void ApplyCategorySelection();
    void SelectBrowserDestination();
    void UpdateDestinationBrowserHud();
    void SetPlayerControl(bool enabled);
    CVehicle *ResolveTaxi() const;
    const Destination *SelectedDestination() const;
    const Destination *DestinationById(std::size_t id) const;
    static std::filesystem::path ModuleDirectory();
};
