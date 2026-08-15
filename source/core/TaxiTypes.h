#pragma once

#include <CVector.h>

#include <cstdint>
#include <string>

enum class TaxiSessionState : std::uint8_t {
    Idle,
    AwaitingEntry,
    SelectingDestination,
    Travelling,
    FadingOut,
    FadingIn,
    ReleasingTaxi
};

enum class DestinationRegion : std::uint8_t { Beach, StarfishIsland, Mainland };

enum class DestinationIcon : std::uint8_t {
    MapHere,
    Lawyer,
    Tshirt,
    Boatyard,
    Strip,
    Gun,
    Spray,
    Save,
    Club,
    Avery,
    DiazMansion,
    Mall,
    FilmStudio,
    Vrock,
    Bikers,
    Printworks,
    Icecream,
    Kcabs,
    Haitians,
    Phil,
    Sunyard,
    Cubans
};

struct Destination final {
    std::string name;
    DestinationRegion island{DestinationRegion::Beach};
    CVector position{};
    float heading{};
    DestinationIcon icon{DestinationIcon::MapHere};
    bool resolveGroundZ{};
    bool accessible{true};
    int roadNodeId{-1};
};

struct DestinationBrowserRow final {
    std::string name;
    DestinationIcon icon{DestinationIcon::MapHere};
};

enum class TaxiAction : std::uint8_t {
    Hail,
    ToggleDestinations,
    PreviousDestination,
    NextDestination,
    ConfirmDestination,
    PreviousDestinationCategory,
    NextDestinationCategory,
    SkipTravel,
    StopTaxi,
    LockDoors,
    UnlockDoors,
    ToggleFirstPerson,
    OpenDestinationBrowser,
    BrowserPreviousCategory,
    BrowserNextCategory,
    BrowserPreviousDestination,
    BrowserNextDestination,
    BrowserConfirm,
    BrowserCancel
};

struct ActionBinding final {
    int virtualKey{};
    int alternateVirtualKey{};
    int padButton{};
};
