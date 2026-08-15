# TaxiManagerVC

TaxiManagerVC is a Plugin-SDK ASI plugin that lets the player use ambient taxis as a passenger in
Grand Theft Auto: Vice City Classic.

## Compatibility

- Vice City Classic PC 1.0 US
- Win32/x86 ASI loader
- Visual Studio 2022 with the v143 toolset when building
- Current Plugin-SDK VC headers and libraries

Vice City 1.1, the original Steam executable, modified or localized executables, and Definitive
Edition are not supported.

## Features

- Hail supported ambient taxi vehicles
- Enter and ride as a passenger
- 26 configurable destinations grouped by city area
- Destination browser with radar icons
- Distance-based fares and optional free rides
- Redirect, stop, lock, and unlock controls
- Skip travel with fade and safe teleport
- Passenger-seat first-person camera with mouse look
- Editable INI controls and gameplay settings
- Editable CSV destinations
- Optional Debug Menu and Menu Map integrations

## Opening the Destination Browser

After the player enters a taxi as a passenger, press **Tab** on the keyboard or **Select** on a
gamepad to open the destination browser. Press the same control again, or use the close control
listed below, to leave the browser.

## Default Controls

### Taxi controls

| Action | Keyboard | Gamepad |
|---|---|---|
| Hail taxi | E | Disabled |
| Show or hide quick destinations | Up Arrow or W | D-pad Up |
| Previous destination | Left Arrow or A | D-pad Left |
| Next destination | Right Arrow or D | D-pad Right |
| Confirm destination | Down Arrow or S | D-pad Down |
| Open destination browser | Tab | Select |
| Skip travel | G | Start |
| Stop taxi | Space | Square |
| Lock doors | 1 | Left Stick Click |
| Unlock doors | 2 | Right Stick Click |
| Toggle passenger camera | F6 | Disabled |

### Destination browser

| Action | Keyboard | Gamepad |
|---|---|---|
| Previous or next group | Left/Right Arrow or A/D | L1/R1 |
| Previous or next destination | Up/Down Arrow or W/S | D-pad Up/Down |
| Select destination | Shift | Cross |
| Close browser | Backspace | Circle |

All bindings can be changed in `TaxiManagerVC.ini` or through the optional Debug Menu plugin.

## Installation

Copy these files into Vice City's `scripts` directory:

```text
TaxiManagerVC.VC.asi
TaxiManagerVC.ini
TaxiManagerVC.destinations.csv
northpointmallradaricon.txd
```

## Building

Set `PLUGIN_SDK_DIR` to the Plugin-SDK root, open `TaxiManagerVC.sln`, and build `Debug|Win32` or
`Release|Win32`. Release output is written to `bin/VC/Release`.
