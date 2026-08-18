#include "core/TaxiManager.h"

#include "gameplay/TaxiOperations.h"
#include "integrations/MenuMapIntegration.h"

#include <CCamera.h>
#include <CMenuManager.h>
#include <CPathFind.h>
#include <CPlayerInfo.h>
#include <CPlayerPed.h>
#include <CPools.h>
#include <CTheZones.h>
#include <CTimer.h>
#include <CWanted.h>
#include <Windows.h>
#include <cDMAudio.h>
#include <extensions/ScriptCommands.h>
#include <extensions/scripting/ScriptCommandNames.h>
#include <plugin.h>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>

using namespace plugin;

namespace {
const CRGBA kOrange{232, 156, 38, 255};
const CRGBA kConfirmed{187, 100, 52, 255};
constexpr unsigned short kHelpSound = 196;
constexpr float kArrivalDistance = 12.0F;
constexpr unsigned int kBrowseTextDurationMs = 10000U;
constexpr unsigned int kConfirmedTextDurationMs = 5000U;
constexpr unsigned int kDestinationSyncIntervalMs = 250U;

// VC 1.0 US per-frame CGame::Process call to CCamera::Process. Hooking the call site preserves
// Plugin-SDK's original-function trampoline; hooking the function entry with
// H_JUMP leaves that trampoline null and crashes when the event calls through.
// This call runs after vehicle matrices advance and immediately before the
// fixed camera consumes its world-space source.
plugin::ThiscallEvent<plugin::AddressList<0x4A4608, plugin::H_CALL>, plugin::PRIORITY_BEFORE,
                      plugin::ArgPickN<CCamera *, 0>, void(CCamera *)>
    beforeCameraProcess;

float Distance(const CVector &left, const CVector &right) {
    const float x = left.x - right.x;
    const float y = left.y - right.y;
    const float z = left.z - right.z;
    return std::sqrt(x * x + y * y + z * z);
}

float HorizontalDistance(const CVector &left, const CVector &right) {
    const float x = left.x - right.x;
    const float y = left.y - right.y;
    return std::sqrt(x * x + y * y);
}

bool CanEnterTaxi(CPlayerPed *player) {
    return player && !player->m_bInVehicle && player->bIsStanding && player->IsPedInControl();
}

DestinationRegion RegionForPosition(const CVector &position) {
    // Vice City's map levels distinguish only Beach and Mainland. Starfish Island is a
    // navigation territory spanning the STARISL and MANSION zones in the original map data.
    constexpr float starfishWest = -724.0F;
    constexpr float starfishEast = -40.0F;
    constexpr float starfishSouth = -670.0F;
    constexpr float starfishNorth = -320.0F;

    if (position.x >= starfishWest && position.x <= starfishEast &&
        position.y >= starfishSouth && position.y <= starfishNorth) {
        return DestinationRegion::StarfishIsland;
    }

    switch (CTheZones::GetLevelFromPosition(&position)) {
    case LEVEL_MAINLAND:
        return DestinationRegion::Mainland;
    case LEVEL_BEACH:
    default:
        return DestinationRegion::Beach;
    }
}

} // namespace

TaxiManager::TaxiManager() {
    Events::initScriptsEvent += [this] { Initialize(); };
    Events::initRwEvent += [this] { hud_.InitializeTextures(ModuleDirectory()); };
    Events::shutdownRwEvent += [this] { hud_.ShutdownTextures(); };
    Events::gameProcessEvent += [this] { Process(); };
    Events::drawHudEvent += [this] { DrawHud(); };
    beforeCameraProcess += [this](CCamera *) { UpdateBeforeCameraProcess(); };
}

void TaxiManager::UpdateBeforeCameraProcess() {
    camera_.UpdateBeforeCameraProcess(FindPlayerPed(), ResolveTaxi(), config_);
}

void TaxiManager::Initialize() {
    Shutdown();
    const auto directory = ModuleDirectory();
    const auto configPath = directory / "TaxiManagerVC.ini";
    config_ = TaxiConfig::Load(configPath);
    debugMenu_.TryRegister(config_, configPath);
    destinations_.Load(directory / "TaxiManagerVC.destinations.csv");
    input_.Reset();
}

void TaxiManager::Process() {
    CPlayerPed *player = FindPlayerPed();
    CVehicle *taxi = ResolveTaxi();

    if (FrontEndMenuManager.m_bMenuActive) {
        camera_.Process(player, taxi, false, false, config_);
        return;
    }

    // The passenger-controls hint is queued once, after main.scm has had time
    // to consume its built-in taxi-driver hint.
    hud_.UpdateHelp();

    if (!player || player->m_fHealth <= 0.0F || !player->m_pWanted) {
        camera_.Process(player, taxi, false, false, config_);

        if (state_ != TaxiSessionState::Idle) {
            Cleanup(player, taxi, false);
        }

        return;
    }

    if (state_ == TaxiSessionState::Idle) {
        camera_.Process(player, taxi, false, false, config_);

        if (input_.JustPressed(TaxiAction::Hail, config_) &&
            player->m_pWanted->m_nWantedLevel == 0) {
            BeginHail(player);
        }

        return;
    }

    if (state_ == TaxiSessionState::ReleasingTaxi) {
        camera_.Process(player, taxi, false, false, config_);

        if (!taxi) {
            Cleanup(player, nullptr, false);
            return;
        }

        // Vice City may retain m_pVehicle as the last-used vehicle after the ped
        // is already on foot. m_bInVehicle is the authoritative occupancy flag;
        // the ped states keep the taxi stopped until the exit animation ends.
        const bool playerStillExiting = (player->m_bInVehicle && player->m_pVehicle == taxi) ||
                                        player->m_ePedState == ePedState::PEDSTATE_EXIT_CAR;

        if (!playerStillExiting) {
            Cleanup(player, taxi, false);
        }

        return;
    }

    if (!taxi || !TaxiOperations::IsUsableTaxi(taxi)) {
        camera_.Process(player, taxi, false, false, config_);
        Cleanup(player, taxi, tripActive_);
        return;
    }

    const bool cameraEligible = (state_ == TaxiSessionState::SelectingDestination ||
                                 state_ == TaxiSessionState::Travelling) &&
                                player->m_bInVehicle && player->m_pVehicle == taxi &&
                                player->m_ePedState == ePedState::PEDSTATE_DRIVING &&
                                taxi->m_pDriver != player;
    const bool cameraTogglePressed =
        cameraEligible && input_.JustPressed(TaxiAction::ToggleFirstPerson, config_);
    camera_.Process(player, taxi, cameraEligible, cameraTogglePressed, config_);

    if (state_ == TaxiSessionState::AwaitingEntry) {
        ProcessAwaitingEntry(player, taxi);
    } else if (state_ == TaxiSessionState::FadingOut || state_ == TaxiSessionState::FadingIn) {
        ProcessFade(player, taxi);
    } else {
        ProcessPassenger(player, taxi);
    }
}

void TaxiManager::BeginHail(CPlayerPed *player) {
    // SetObjective cannot start a vehicle entry while Tommy is jumping,
    // falling, landing, or otherwise outside normal on-foot control.
    if (!CanEnterTaxi(player)) {
        return;
    }

    CVehicle *taxi = TaxiOperations::FindNearest(player->GetPosition(), config_.hailRadius);

    if (!taxi) {
        return;
    }

    // Vice City can report a scripted passenger entry as vehicle theft while
    // Tommy is still playing the enter-car animation. Take ownership of the
    // wanted limit before assigning that objective, not after he is seated.
    savedWantedLimit_ = CWanted::MaximumWantedLevel;
    wantedLimitSaved_ = true;
    CWanted::SetMaximumWantedLevel(0);
    taxiHandle_ = CPools::GetVehicleRef(taxi);
    state_ = TaxiSessionState::AwaitingEntry;
    stateStartedAt_ = CTimer::m_snTimeInMilliseconds;
    TaxiOperations::PrepareForPassenger(player, taxi);
}

void TaxiManager::ProcessAwaitingEntry(CPlayerPed *player, CVehicle *taxi) {
    if (player->m_pVehicle == taxi && player->m_ePedState == ePedState::PEDSTATE_DRIVING) {
        RefreshAvailableDestinations();

        if (availableDestinations_.empty()) {
            Hud::Help("No destinations are currently available.");
            Cleanup(player, taxi, false);
            return;
        }

        selectionCursor_ = 0;
        state_ = TaxiSessionState::SelectingDestination;
        stateStartedAt_ = CTimer::m_snTimeInMilliseconds;
        BuildDestinationCategories();
        SelectPlayerRegionCategory(player, taxi);
        RefreshSelection();
        hud_.ShowHelp("Taxi passenger: ~h~~k~~GO_FORWARD~~w~ show destinations; "
                      "~h~~k~~GO_LEFT~~w~ / ~h~~k~~GO_RIGHT~~w~ browse; "
                      "~h~~k~~GO_BACK~~w~ confirm; ~h~Tab / Select~w~ destination browser; "
                      "~h~G / Start~w~ skip; ~h~F6~w~ first person.");
        return;
    }

    // A collision or another transient ped state can make GTA discard the
    // passenger objective. Once Tommy is safely controllable on foot again,
    // reissue it instead of leaving the taxi stopped until the timeout.
    if (taxi && CanEnterTaxi(player) &&
        (player->m_nObjective != eObjective::OBJECTIVE_ENTER_CAR_AS_PASSENGER ||
         player->m_pObjectiveVehicle != taxi)) {
        TaxiOperations::PrepareForPassenger(player, taxi);
    }

    if (CTimer::m_snTimeInMilliseconds - stateStartedAt_ >=
        static_cast<unsigned int>(config_.passengerWaitMs)) {
        Cleanup(player, taxi, false);
    }
}

void TaxiManager::ProcessPassenger(CPlayerPed *player, CVehicle *taxi) {
    if (player->m_pVehicle != taxi || player->m_ePedState != ePedState::PEDSTATE_DRIVING) {
        Cleanup(player, taxi, tripActive_, true);
        return;
    }

    const unsigned int now = CTimer::m_snTimeInMilliseconds;
    SynchronizeDestinations(now);
    ProcessDestinationMenus(player, taxi);
    ProcessPassengerControls(taxi);
    UpdateTripProgress(player, taxi);
}

void TaxiManager::SynchronizeDestinations(unsigned int now) {
    if ((destinationMenuVisible_ || destinationBrowserVisible_) && now >= nextDestinationSyncAt_) {
        nextDestinationSyncAt_ = now + kDestinationSyncIntervalMs;

        if (RefreshAvailableDestinations()) {
            if (destinationBrowserVisible_) {
                BuildDestinationBrowser();
            } else {
                BuildDestinationCategories();
                RefreshSelection();
            }
        }
    }
}

void TaxiManager::ProcessDestinationMenus(CPlayerPed *player, CVehicle *taxi) {
    if (input_.JustPressed(TaxiAction::OpenDestinationBrowser, config_)) {
        if (destinationBrowserVisible_) {
            CloseDestinationBrowser();
        } else {
            OpenDestinationBrowser(player, taxi);
        }
    }

    if (destinationBrowserVisible_) {
        ProcessDestinationBrowser(player, taxi);
        return;
    }

    ProcessCompactDestinationMenu(player, taxi);
}

void TaxiManager::ProcessCompactDestinationMenu(CPlayerPed *player, CVehicle *taxi) {
    const bool previousPressed = input_.JustPressed(TaxiAction::PreviousDestination, config_);
    const bool nextPressed = input_.JustPressed(TaxiAction::NextDestination, config_);
    const bool confirmPressed = input_.JustPressed(TaxiAction::ConfirmDestination, config_);
    const bool previousCategoryPressed =
        input_.JustPressed(TaxiAction::PreviousDestinationCategory, config_);
    const bool nextCategoryPressed =
        input_.JustPressed(TaxiAction::NextDestinationCategory, config_);

    if (input_.JustPressed(TaxiAction::ToggleDestinations, config_)) {
        if (destinationMenuVisible_) {
            destinationMenuVisible_ = false;

            if (tripActive_) {
                state_ = TaxiSessionState::Travelling;
            }

            hud_.HideSelection();
        } else {
            state_ = TaxiSessionState::SelectingDestination;
            destinationMenuVisible_ = true;
            RefreshAvailableDestinations();
            BuildDestinationCategories();
            SelectPlayerRegionCategory(player, taxi);
            RefreshSelection();
        }
    }

    if (previousCategoryPressed || nextCategoryPressed) {
        if (!destinationMenuVisible_) {
            state_ = TaxiSessionState::SelectingDestination;
            destinationMenuVisible_ = true;
            RefreshAvailableDestinations();
            BuildDestinationCategories();
            SelectPlayerRegionCategory(player, taxi);
        }

        SelectCompactCategory(previousCategoryPressed ? -1 : 1);
    }

    // Previous/Next are useful shortcuts as well as browsing controls. During
    // a trip they immediately reopen the list and move from the currently
    // highlighted destination, even while the confirmation text is visible.
    if (previousPressed || nextPressed) {
        if (!destinationMenuVisible_) {
            state_ = TaxiSessionState::SelectingDestination;
            destinationMenuVisible_ = true;
            RefreshAvailableDestinations();
            BuildDestinationCategories();
            SelectPlayerRegionCategory(player, taxi);
        }

        SelectCompactDestination(previousPressed ? -1 : 1);
    }

    if (state_ == TaxiSessionState::SelectingDestination && destinationMenuVisible_ &&
        confirmPressed) {
        ConfirmDestination(player, taxi);
    }
}

void TaxiManager::ProcessPassengerControls(CVehicle *taxi) {
    if (input_.JustPressed(TaxiAction::StopTaxi, config_)) {
        TaxiOperations::Stop(taxi);
    }

    if (input_.JustPressed(TaxiAction::LockDoors, config_)) {
        taxi->m_eDoorLock = DOORLOCK_LOCKED;
    }

    if (input_.JustPressed(TaxiAction::UnlockDoors, config_)) {
        taxi->m_eDoorLock = DOORLOCK_UNLOCKED;
    }

    if (tripActive_ && input_.JustPressed(TaxiAction::SkipTravel, config_)) {
        BeginSkip(taxi);
    }
}

void TaxiManager::UpdateTripProgress(CPlayerPed *player, CVehicle *taxi) {
    const Destination *destination = tripDestination_ ? &*tripDestination_ : nullptr;

    if (tripActive_ && state_ != TaxiSessionState::FadingOut &&
        state_ != TaxiSessionState::FadingIn) {
        TaxiOperations::MaintainDriving(taxi);
    }

    if (tripActive_ && destination &&
        (destination->resolveGroundZ
             ? HorizontalDistance(taxi->GetPosition(), destination->position)
             : Distance(taxi->GetPosition(), destination->position)) <= kArrivalDistance) {
        CompleteTrip(player, taxi);
    }
}

void TaxiManager::ConfirmDestination(CPlayerPed *player, CVehicle *taxi) {
    const bool confirmedFromBrowser = destinationBrowserVisible_;
    const Destination *destination = SelectedDestination();

    if (!destination) {
        return;
    }

    if (!destination->accessible) {
        Hud::Help("That map waypoint has no accessible road nearby.");
        return;
    }

    if (destination->roadNodeId >= 0) {
        const int taxiNodeId = ThePaths.FindNodeClosestToCoors(taxi->GetPosition(), 0, 60.0F, false,
                                                               false, false, false);

        if (taxiNodeId < 0) {
            Hud::Help("The taxi cannot find a road route to that map waypoint.");
            return;
        }
    }

    const float distance = Distance(taxi->GetPosition(), destination->position);

    if (distance <= config_.minimumTripDistance) {
        Hud::Help("You are too close to that destination.");
        return;
    }

    int baseFare = config_.freeRides ? 0 : accumulatedFare_;

    if (!config_.freeRides && tripActive_) {
        baseFare +=
            TaxiOperations::Fare(segmentStart_, taxi->GetPosition(), config_.fareMultiplier);
    }

    const int nextFare = config_.freeRides
                             ? 0
                             : TaxiOperations::Fare(taxi->GetPosition(), destination->position,
                                                    config_.fareMultiplier);
    const long long projected = static_cast<long long>(baseFare) + nextFare;
    CPlayerInfo *info = player->GetPlayerInfoForThisPlayerPed();

    if (!info || projected > std::max(0, info->m_nMoney) ||
        projected > std::numeric_limits<int>::max()) {
        Hud::Help("You don't have enough money for that destination.");
        return;
    }

    // Do not enter the travelling state unless VC produced a real road route.
    // Otherwise its accurate command falls back to driving straight at a far
    // target, which is especially visible around locked island bridges.
    if (!TaxiOperations::DriveTo(taxi, *destination, config_)) {
        Hud::Help("The taxi cannot find a road route to that destination.");
        return;
    }

    accumulatedFare_ = baseFare;
    segmentStart_ = taxi->GetPosition();
    tripDestination_ = *destination;
    tripActive_ = true;
    state_ = TaxiSessionState::Travelling;
    destinationMenuVisible_ = false;
    destinationBrowserVisible_ = false;
    hud_.HideBrowser();
    stateStartedAt_ = CTimer::m_snTimeInMilliseconds;

    if (confirmedFromBrowser) {
        input_.Consume(TaxiAction::ToggleDestinations, config_);
        input_.Consume(TaxiAction::PreviousDestination, config_);
        input_.Consume(TaxiAction::NextDestination, config_);
        input_.Consume(TaxiAction::ConfirmDestination, config_);
        input_.Consume(TaxiAction::PreviousDestinationCategory, config_);
        input_.Consume(TaxiAction::NextDestinationCategory, config_);
        hud_.HideSelection();
    } else {
        hud_.ShowSelection(destination->name, kConfirmed, kConfirmedTextDurationMs);
    }

    DMAudio.PlayFrontEndSound(kHelpSound, 0);
}

void TaxiManager::BeginSkip(CVehicle *taxi) {
    const Destination *destination = tripDestination_ ? &*tripDestination_ : nullptr;

    if (!destination) {
        return;
    }

    camera_.Cancel(FindPlayerPed(), taxi);
    SetPlayerControl(false);
    TheCamera.SetFadeColour(0, 0, 0);
    TheCamera.Fade(static_cast<float>(config_.fadeOutMs) / 1000.0F, 0);
    state_ = TaxiSessionState::FadingOut;
    destinationMenuVisible_ = false;
    destinationBrowserVisible_ = false;
    stateStartedAt_ = CTimer::m_snTimeInMilliseconds;
    hud_.HideSelection();
    hud_.HideBrowser();
}

void TaxiManager::ProcessFade(CPlayerPed *player, CVehicle *taxi) {
    if (player->m_pVehicle != taxi) {
        Cleanup(player, taxi, true);
        return;
    }

    const Destination *destination = tripDestination_ ? &*tripDestination_ : nullptr;

    if (!destination) {
        Cleanup(player, taxi, true);
        return;
    }

    const unsigned int elapsed = CTimer::m_snTimeInMilliseconds - stateStartedAt_;

    if (state_ == TaxiSessionState::FadingOut &&
        elapsed >= static_cast<unsigned int>(config_.fadeOutMs)) {
        TaxiOperations::Stop(taxi);
        TaxiOperations::Teleport(taxi, *destination);
        TheCamera.SetFadeColour(0, 0, 0);
        TheCamera.Fade(static_cast<float>(config_.fadeInMs) / 1000.0F, 1);
        state_ = TaxiSessionState::FadingIn;
        stateStartedAt_ = CTimer::m_snTimeInMilliseconds;
    } else if (state_ == TaxiSessionState::FadingIn &&
               elapsed >= static_cast<unsigned int>(config_.fadeInMs)) {
        SetPlayerControl(true);
        CompleteTrip(player, taxi);
    }
}

void TaxiManager::CompleteTrip(CPlayerPed *player, CVehicle *taxi) {
    int fare = config_.freeRides
                   ? 0
                   : accumulatedFare_ + TaxiOperations::Fare(segmentStart_, taxi->GetPosition(),
                                                             config_.fareMultiplier);
    CPlayerInfo *info = player ? player->GetPlayerInfoForThisPlayerPed() : nullptr;

    if (info) {
        fare = std::clamp(fare, 0, std::max(0, info->m_nMoney));
        info->m_nMoney -= fare;
    }

    if (fare > 0) {
        hud_.ShowFare(fare);
    }

    accumulatedFare_ = 0;
    tripActive_ = false;
    tripDestination_.reset();
    state_ = TaxiSessionState::SelectingDestination;
    destinationMenuVisible_ = true;
    destinationBrowserVisible_ = false;
    stateStartedAt_ = CTimer::m_snTimeInMilliseconds;
    TaxiOperations::Stop(taxi);
    RefreshSelection();
}

void TaxiManager::Cleanup(CPlayerPed *player, CVehicle *taxi, bool chargeFare,
                          bool delayTaxiRelease) {
    camera_.Cancel(player, taxi);

    if (chargeFare && tripActive_ && player && taxi) {
        int fare = config_.freeRides
                       ? 0
                       : accumulatedFare_ + TaxiOperations::Fare(segmentStart_, taxi->GetPosition(),
                                                                 config_.fareMultiplier);

        if (CPlayerInfo *info = player->GetPlayerInfoForThisPlayerPed()) {
            fare = std::clamp(fare, 0, std::max(0, info->m_nMoney));
            info->m_nMoney -= fare;

            if (fare > 0) {
                hud_.ShowFare(fare);
            }
        }
    }

    SetPlayerControl(true);

    if (wantedLimitSaved_) {
        CWanted::SetMaximumWantedLevel(savedWantedLimit_);
    }

    if (taxi && delayTaxiRelease) {
        TaxiOperations::Stop(taxi);
        state_ = TaxiSessionState::ReleasingTaxi;
        stateStartedAt_ = CTimer::m_snTimeInMilliseconds;
    } else {
        if (taxi) {
            TaxiOperations::Release(taxi, config_);
        }

        state_ = TaxiSessionState::Idle;
        taxiHandle_ = -1;
    }

    availableDestinations_.clear();
    selectionCursor_ = 0;
    accumulatedFare_ = 0;
    tripActive_ = false;
    tripDestination_.reset();
    destinationMenuVisible_ = false;
    destinationBrowserVisible_ = false;
    wantedLimitSaved_ = false;
    hud_.HideSelection();
    hud_.HideBrowser();
}

void TaxiManager::SelectCompactCategory(int offset) {
    if (browserCategories_.empty()) {
        return;
    }

    const int count = static_cast<int>(browserCategories_.size());
    browserCategoryCursor_ = static_cast<std::size_t>(
        (static_cast<int>(browserCategoryCursor_) + offset + count) % count);
    browserItemCursor_ = 0;
    ApplyCategorySelection();
    RefreshSelection();
    DMAudio.PlayFrontEndSound(kHelpSound, 0);
}

void TaxiManager::SelectCompactDestination(int offset) {
    if (browserCategoryCursor_ >= browserCategories_.size()) {
        return;
    }

    const auto &category = browserCategories_[browserCategoryCursor_];

    if (category.destinationIds.empty()) {
        return;
    }

    const int count = static_cast<int>(category.destinationIds.size());
    browserItemCursor_ = static_cast<std::size_t>(
        (static_cast<int>(browserItemCursor_) + offset + count) % count);
    ApplyCategorySelection();
    RefreshSelection();
    DMAudio.PlayFrontEndSound(kHelpSound, 0);
}

void TaxiManager::RefreshSelection() {
    destinationMenuVisible_ = true;

    const Destination *destination = SelectedDestination();

    if (!destination) {
        return;
    }

    const std::string category = browserCategoryCursor_ < browserCategories_.size()
                                     ? browserCategories_[browserCategoryCursor_].name
                                     : std::string{};
    const std::string currentDestination =
        tripActive_ && tripDestination_ ? tripDestination_->name : std::string{};
    hud_.ShowDestinationSelection(category, destination->name, destination->icon,
                                  currentDestination, kOrange, kBrowseTextDurationMs);
}

void TaxiManager::OpenDestinationBrowser(CPlayerPed *player, CVehicle *taxi) {
    RefreshAvailableDestinations();

    if (availableDestinations_.empty()) {
        Hud::Help("No destinations are currently available.");
        return;
    }

    state_ = TaxiSessionState::SelectingDestination;
    destinationMenuVisible_ = false;
    destinationBrowserVisible_ = true;
    hud_.HideHelp();
    hud_.HideSelection();
    BuildDestinationBrowser(player, taxi);
}

void TaxiManager::CloseDestinationBrowser() {
    destinationBrowserVisible_ = false;
    browserCategories_.clear();
    hud_.HideBrowser();

    if (tripActive_) {
        state_ = TaxiSessionState::Travelling;
    }
}

void TaxiManager::ProcessDestinationBrowser(CPlayerPed *player, CVehicle *taxi) {
    if (browserCategories_.empty()) {
        CloseDestinationBrowser();
        return;
    }

    int categoryOffset{};

    if (input_.JustPressed(TaxiAction::BrowserPreviousCategory, config_)) {
        categoryOffset = -1;
    }

    if (input_.JustPressed(TaxiAction::BrowserNextCategory, config_)) {
        categoryOffset = 1;
    }

    if (categoryOffset != 0) {
        const int count = static_cast<int>(browserCategories_.size());
        browserCategoryCursor_ = static_cast<std::size_t>(
            (static_cast<int>(browserCategoryCursor_) + categoryOffset + count) % count);
        browserItemCursor_ = 0;
        SelectBrowserDestination();
    }

    int itemOffset{};

    if (input_.JustPressed(TaxiAction::BrowserPreviousDestination, config_)) {
        itemOffset = -1;
    }

    if (input_.JustPressed(TaxiAction::BrowserNextDestination, config_)) {
        itemOffset = 1;
    }

    if (itemOffset != 0) {
        const int count =
            static_cast<int>(browserCategories_[browserCategoryCursor_].destinationIds.size());
        browserItemCursor_ = static_cast<std::size_t>(
            (static_cast<int>(browserItemCursor_) + itemOffset + count) % count);
        SelectBrowserDestination();
    }

    if (input_.JustPressed(TaxiAction::BrowserCancel, config_)) {
        CloseDestinationBrowser();
        return;
    }

    if (input_.JustPressed(TaxiAction::BrowserConfirm, config_)) {
        ConfirmDestination(player, taxi);
    }
}

void TaxiManager::BuildDestinationCategories() {
    const std::optional<std::size_t> selectedId =
        selectionCursor_ < availableDestinations_.size()
            ? std::optional<std::size_t>{availableDestinations_[selectionCursor_]}
            : std::nullopt;
    browserCategories_.clear();

    const auto addCategory = [this](const char *name, DestinationRegion island) {
        DestinationCategory category{name, island, {}};
        for (const std::size_t id : availableDestinations_) {
            const Destination *destination = DestinationById(id);

            if (id != destinations_.All().size() && destination && destination->island == island) {
                category.destinationIds.push_back(id);
            }
        }

        if (!category.destinationIds.empty()) {
            browserCategories_.push_back(std::move(category));
        }
    };

    addCategory("Vice City Beach", DestinationRegion::Beach);
    addCategory("Starfish Island", DestinationRegion::StarfishIsland);
    addCategory("Vice City Mainland", DestinationRegion::Mainland);

    const std::size_t waypointId = destinations_.All().size();

    if (mapWaypoint_ && std::find(availableDestinations_.begin(), availableDestinations_.end(),
                                  waypointId) != availableDestinations_.end()) {
        browserCategories_.push_back(
            DestinationCategory{"Custom Waypoint", std::nullopt, {waypointId}});
    }

    browserCategoryCursor_ = 0;
    browserItemCursor_ = 0;

    if (selectedId) {
        for (std::size_t category = 0; category < browserCategories_.size(); ++category) {
            const auto found =
                std::find(browserCategories_[category].destinationIds.begin(),
                          browserCategories_[category].destinationIds.end(), *selectedId);

            if (found != browserCategories_[category].destinationIds.end()) {
                browserCategoryCursor_ = category;
                browserItemCursor_ = static_cast<std::size_t>(
                    std::distance(browserCategories_[category].destinationIds.begin(), found));
                break;
            }
        }
    }
}

void TaxiManager::SelectPlayerRegionCategory(CPlayerPed *player, CVehicle *taxi) {
    if (!player) {
        return;
    }

    const bool useTaxiPosition = taxi && player->m_bInVehicle && player->m_pVehicle == taxi;
    const CVector &position = useTaxiPosition ? taxi->GetPosition() : player->GetPosition();
    const DestinationRegion playerRegion = RegionForPosition(position);

    for (std::size_t category = 0; category < browserCategories_.size(); ++category) {
        if (browserCategories_[category].region == playerRegion) {
            browserCategoryCursor_ = category;
            browserItemCursor_ = 0;
            ApplyCategorySelection();
            return;
        }
    }
}

void TaxiManager::BuildDestinationBrowser(CPlayerPed *player, CVehicle *taxi) {
    BuildDestinationCategories();
    SelectPlayerRegionCategory(player, taxi);
    SelectBrowserDestination();
}

void TaxiManager::ApplyCategorySelection() {
    if (browserCategoryCursor_ >= browserCategories_.size()) {
        return;
    }

    const auto &category = browserCategories_[browserCategoryCursor_];

    if (browserItemCursor_ >= category.destinationIds.size()) {
        return;
    }

    const std::size_t id = category.destinationIds[browserItemCursor_];
    const auto found = std::find(availableDestinations_.begin(), availableDestinations_.end(), id);

    if (found != availableDestinations_.end()) {
        selectionCursor_ =
            static_cast<std::size_t>(std::distance(availableDestinations_.begin(), found));
    }
}

void TaxiManager::SelectBrowserDestination() {
    ApplyCategorySelection();
    UpdateDestinationBrowserHud();
    DMAudio.PlayFrontEndSound(kHelpSound, 0);
}

void TaxiManager::UpdateDestinationBrowserHud() {
    if (browserCategoryCursor_ >= browserCategories_.size()) {
        return;
    }

    const auto &category = browserCategories_[browserCategoryCursor_];
    std::vector<DestinationBrowserRow> rows;
    rows.reserve(category.destinationIds.size());

    for (const std::size_t id : category.destinationIds) {
        if (const Destination *destination = DestinationById(id)) {
            rows.push_back({destination->name, destination->icon});
        }
    }

    const std::string currentDestination =
        tripActive_ && tripDestination_ ? tripDestination_->name : std::string{};
    hud_.ShowBrowser(category.name, std::move(rows), browserItemCursor_, currentDestination);
}

bool TaxiManager::RefreshAvailableDestinations() {
    const std::size_t waypointSentinel = destinations_.All().size();
    const std::size_t previousCursor = selectionCursor_;
    const std::optional<std::size_t> previousSelection =
        selectionCursor_ < availableDestinations_.size()
            ? std::optional<std::size_t>{availableDestinations_[selectionCursor_]}
            : std::nullopt;

    std::vector<std::size_t> refreshed = destinations_.Available();
    std::optional<Destination> refreshedWaypoint;

    CVector position{};

    if (MenuMapIntegration::TryGetWaypoint(position)) {
        const DestinationRegion island =
            position.x > -350.0F ? DestinationRegion::Beach : DestinationRegion::Mainland;

        if (DestinationRepository::IsUnlocked(island)) {
            const int nodeId = ThePaths.FindNodeClosestToCoors(
                position, 0, config_.waypointMaximumRoadSnapDistance, false, false, false, false);
            const bool validNode = nodeId >= 0;

            if (validNode) {
                refreshedWaypoint = Destination{"Map Waypoint",
                                                island,
                                                ThePaths.nodes[nodeId].GetPosition(),
                                                0.0F,
                                                DestinationIcon::MapHere,
                                                true,
                                                true,
                                                nodeId};
            } else {
                refreshedWaypoint = Destination{"Map Waypoint (no accessible road)",
                                                island,
                                                position,
                                                0.0F,
                                                DestinationIcon::MapHere,
                                                true,
                                                false,
                                                -1};
            }

            // The index immediately after the repository is a sentinel for the
            // optional runtime destination and can never collide with a CSV entry.
            refreshed.push_back(waypointSentinel);
        }
    }

    const auto waypointChanged = [this, &refreshedWaypoint] {
        if (mapWaypoint_.has_value() != refreshedWaypoint.has_value()) {
            return true;
        }

        if (!mapWaypoint_) {
            return false;
        }

        return mapWaypoint_->island != refreshedWaypoint->island ||
               mapWaypoint_->accessible != refreshedWaypoint->accessible ||
               mapWaypoint_->roadNodeId != refreshedWaypoint->roadNodeId ||
               Distance(mapWaypoint_->position, refreshedWaypoint->position) > 0.01F;
    };

    if (availableDestinations_ == refreshed && !waypointChanged()) {
        return false;
    }

    availableDestinations_ = std::move(refreshed);
    mapWaypoint_ = std::move(refreshedWaypoint);

    if (availableDestinations_.empty()) {
        selectionCursor_ = 0;
        return true;
    }

    if (previousSelection) {
        const auto preserved = std::find(availableDestinations_.begin(),
                                         availableDestinations_.end(), *previousSelection);

        if (preserved != availableDestinations_.end()) {
            selectionCursor_ =
                static_cast<std::size_t>(std::distance(availableDestinations_.begin(), preserved));
            return true;
        }
    }

    selectionCursor_ = std::min(previousCursor, availableDestinations_.size() - 1);
    return true;
}

void TaxiManager::SetPlayerControl(bool enabled) {
    if (enabled && !playerControlDisabled_) {
        return;
    }

    if (!enabled && playerControlDisabled_) {
        return;
    }

    plugin::Command<plugin::Commands::SET_PLAYER_CONTROL>(0, enabled);
    playerControlDisabled_ = !enabled;
}

CVehicle *TaxiManager::ResolveTaxi() const {
    return taxiHandle_ >= 0 && CPools::ms_pVehiclePool ? CPools::GetVehicle(taxiHandle_) : nullptr;
}

const Destination *TaxiManager::SelectedDestination() const {
    if (selectionCursor_ >= availableDestinations_.size()) {
        return nullptr;
    }

    return DestinationById(availableDestinations_[selectionCursor_]);
}

const Destination *TaxiManager::DestinationById(std::size_t id) const {
    if (id == destinations_.All().size()) {
        return mapWaypoint_ ? &*mapWaypoint_ : nullptr;
    }

    return id < destinations_.All().size() ? &destinations_.All()[id] : nullptr;
}

void TaxiManager::DrawHud() {
    hud_.Draw(config_.menuFontStyle, config_.destinationBrowserWidth,
              config_.destinationBrowserHeight);
}

void TaxiManager::Shutdown() {
    CPlayerPed *player = FindPlayerPed();
    Cleanup(player, ResolveTaxi(), false);
    camera_.Reset();
    hud_.Reset();
}

std::filesystem::path TaxiManager::ModuleDirectory() {
    HMODULE module{};
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCSTR>(&TaxiManager::ModuleDirectory), &module);
    std::array<char, MAX_PATH> path{};
    const DWORD length = GetModuleFileNameA(module, path.data(), static_cast<DWORD>(path.size()));
    return std::filesystem::path(std::string(path.data(), length)).parent_path();
}
