# TaxiManagerVC for Vice City Classic

TaxiManagerVC lets Tommy use ambient Vice City taxis as a passenger. It ports the GTA III TaxiManager session model to verified Vice City Plugin-SDK APIs and supports hailing, passenger entry, destination browsing, fares, redirecting, stopping, door controls, skip travel, safe taxi release, and the passenger-seat sightseeing camera.

Source repository: https://github.com/yuksel-kadir/TaxiManagerVC

## Compatibility

- Grand Theft Auto: Vice City Classic PC 1.0 US
- Win32/x86 ASI loader
- Visual Studio 2022/v143 and the current local Plugin-SDK when building

Vice City 1.1, the original Steam executable, localized or modified executables, and Definitive Edition are not supported by this build.

## Installation

Copy these files to Vice City's `scripts` directory:

```text
TaxiManagerVC.VC.asi
TaxiManagerVC.ini
TaxiManagerVC.destinations.csv
northpointmallradaricon.txd
```

The configuration, destination, and North Point Mall TXD files must remain beside the ASI.

## Use

Stand near an ambient Taxi, Cabbie, or Kaufman Cab driven by taxi-driver model 28 or 74, make sure Tommy has no wanted level, and press E. Once seated, use the quick controls or press Tab to open the categorized destination browser. The default bindings and every configurable value are documented in `TaxiManagerVC.ini`.

The 26 compiled defaults and matching CSV are derived from the original Vice City Taxi Mod and are grouped into Vice City Beach, Starfish Island, and Mainland. Invalid or missing CSV data falls back to those compiled defaults.

Destination CSV rows use `name,island,x,y,z,heading,icon`. The seventh `icon` column is
optional, so existing six-column files remain valid. Supported icon keys are `lawyer`, `tshirt`,
`boatyard`, `strip`, `gun`, `spray`, `save`, `club`, `avery`, `diaz_mansion`, `mall`,
`filmstudio`, `vrock`, `bikers`, `printworks`, `icecream`, `kcabs`, `haitians`, `phil`,
`sunyard`, `cubans`, and `map_here`. Missing or unknown keys use Vice City's native map-here
marker. The stock radar sprites are game-owned, so replacements loaded through Mod Loader are
used automatically. The `mall` key loads the `radar_mall` texture from
`northpointmallradaricon.txd`; if that file or texture cannot be loaded, it also falls back to
map-here. Diaz Mansion uses Tommy's icon while an active Tommy radar marker exists and otherwise
uses Diaz's icon.

Menu Map integration is optional. TaxiManagerVC looks up `MenuMapVC_GetWaypoint` at runtime; the current MenuMapVC build does not export it, so normal CSV destinations continue to work without Menu Map.

## Build

Set `PLUGIN_SDK_DIR` explicitly, then build `Debug|Win32` or `Release|Win32` from
`TaxiManagerVC.sln`. Debug links `Plugin_VC_d.lib`; Release links `Plugin_VC.lib` and
conditionally installs the ASI, INI, CSV, and mall TXD when `GTA_VC_DIR` is defined.

Build outputs are written to `bin/VC/Debug` and `bin/VC/Release`. GitHub Actions builds both
configurations against the pinned Plugin-SDK revision and publishes the Release files as a workflow
artifact.
