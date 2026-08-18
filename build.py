#!/usr/bin/env python3
"""Build TaxiManagerVC with Visual Studio 2022 MSBuild."""

from __future__ import annotations

import argparse
import csv
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys


PROJECT_DIR = Path(__file__).resolve().parent
SOLUTION = PROJECT_DIR / "TaxiManagerVC.sln"
DEFAULT_SDK_DIR = PROJECT_DIR.parent / "plugin-sdk"
VSWHERE = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / (
    r"Microsoft Visual Studio\Installer\vswhere.exe"
)
FALLBACK_MSBUILD = Path(
    r"C:\Program Files\Microsoft Visual Studio\2022\Community"
    r"\MSBuild\Current\Bin\MSBuild.exe"
)


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Build TaxiManagerVC Debug, Release, or both."
    )
    parser.add_argument(
        "configuration",
        nargs="?",
        choices=("debug", "release", "all"),
        default="all",
        help="configuration to build (default: all)",
    )
    parser.add_argument(
        "--sdk",
        type=Path,
        help="Plugin-SDK root (defaults to PLUGIN_SDK_DIR or ../plugin-sdk)",
    )
    parser.add_argument(
        "--rebuild",
        action="store_true",
        help="clean and rebuild instead of performing an incremental build",
    )
    parser.add_argument(
        "--deploy",
        action="store_true",
        help="copy the Release ASI and data files to GTA_VC_DIR",
    )
    parser.add_argument(
        "--vc-dir",
        type=Path,
        help="Vice City directory to use with --deploy (defaults to GTA_VC_DIR)",
    )
    return parser.parse_args()


def show_console_menu() -> argparse.Namespace | None:
    print("TaxiManagerVC Builder")
    print("=====================")
    print("1. Build Debug")
    print("2. Build Release")
    print("3. Build Debug and Release")
    print("4. Build Release and deploy")
    print("5. Rebuild Debug and Release")
    print("6. Exit")

    choices = {
        "1": ("debug", False, False),
        "2": ("release", False, False),
        "3": ("all", False, False),
        "4": ("release", False, True),
        "5": ("all", True, False),
    }

    while True:
        selection = input("\nSelect an option [1-6]: ").strip()

        if selection == "6":
            return None

        selected = choices.get(selection)

        if selected is None:
            print("Please enter a number from 1 to 6.")
            continue

        configuration, rebuild, deploy = selected
        return argparse.Namespace(
            configuration=configuration,
            sdk=None,
            rebuild=rebuild,
            deploy=deploy,
            vc_dir=None,
        )


def resolve_sdk_dir(requested: Path | None) -> Path:
    configured = requested

    if configured is None:
        environment_value = os.environ.get("PLUGIN_SDK_DIR")
        configured = Path(environment_value) if environment_value else DEFAULT_SDK_DIR

    sdk_dir = configured.expanduser().resolve()
    required_paths = (
        sdk_dir / "plugin_vc",
        sdk_dir / "shared",
        sdk_dir / "output" / "lib",
    )

    if not all(path.exists() for path in required_paths):
        raise RuntimeError(f"Plugin-SDK directory is not valid: {sdk_dir}")

    return sdk_dir


def find_msbuild() -> Path:
    if VSWHERE.is_file():
        result = subprocess.run(
            [
                str(VSWHERE),
                "-latest",
                "-products",
                "*",
                "-requires",
                "Microsoft.Component.MSBuild",
                "-find",
                r"MSBuild\**\Bin\MSBuild.exe",
            ],
            check=True,
            capture_output=True,
            text=True,
        )
        candidates = [Path(line.strip()) for line in result.stdout.splitlines() if line.strip()]

        if candidates:
            return candidates[0]

    if FALLBACK_MSBUILD.is_file():
        return FALLBACK_MSBUILD

    executable = shutil.which("MSBuild.exe") or shutil.which("msbuild")

    if executable:
        return Path(executable)

    raise RuntimeError("Visual Studio 2022 MSBuild could not be found.")


def resolve_vc_dir(arguments: argparse.Namespace) -> Path | None:
    if not arguments.deploy:
        return None

    configured = arguments.vc_dir

    if configured is None:
        environment_value = os.environ.get("GTA_VC_DIR")

        if environment_value:
            configured = Path(environment_value)

    if configured is None:
        raise RuntimeError("--deploy requires --vc-dir or a valid GTA_VC_DIR.")

    vc_dir = configured.expanduser().resolve()

    if not (vc_dir / "gta-vc.exe").is_file():
        raise RuntimeError(f"Vice City executable was not found in: {vc_dir}")

    return vc_dir


def required_library(configuration: str) -> str:
    return "Plugin_VC_d.lib" if configuration == "Debug" else "Plugin_VC.lib"


def build_configuration(
    msbuild: Path,
    sdk_dir: Path,
    configuration: str,
    rebuild: bool,
) -> None:
    library = sdk_dir / "output" / "lib" / required_library(configuration)

    if not library.is_file():
        raise RuntimeError(f"Required SDK library is missing: {library}")

    environment = os.environ.copy()
    environment["PLUGIN_SDK_DIR"] = str(sdk_dir)
    environment.pop("GTA_VC_DIR", None)

    target = "Rebuild" if rebuild else "Build"
    command = [
        str(msbuild),
        str(SOLUTION),
        "/m",
        f"/t:{target}",
        f"/p:Configuration={configuration}",
        "/p:Platform=Win32",
        "/v:minimal",
    ]

    print(f"\n=== Building {configuration}|Win32 ===", flush=True)
    subprocess.run(command, cwd=PROJECT_DIR, env=environment, check=True)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()

    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)

    return digest.hexdigest().upper()


def output_path(configuration: str) -> Path:
    return PROJECT_DIR / "bin" / "VC" / configuration / "TaxiManagerVC.VC.asi"


def report_output(configuration: str) -> None:
    output = output_path(configuration)

    if not output.is_file():
        raise RuntimeError(f"Build completed, but the ASI was not found: {output}")

    print(f"Output: {output}")
    print(f"Size:   {output.stat().st_size} bytes")
    print(f"SHA-256: {sha256(output)}")


def is_game_running() -> bool:
    result = subprocess.run(
        ["tasklist", "/FI", "IMAGENAME eq gta-vc.exe", "/FO", "CSV", "/NH"],
        check=False,
        capture_output=True,
        text=True,
    )

    for row in csv.reader(result.stdout.splitlines()):
        if row and row[0].casefold() == "gta-vc.exe":
            return True

    return False


def wait_until_game_closes(interactive: bool) -> None:
    while is_game_running():
        if not interactive:
            raise RuntimeError("gta-vc.exe is running. Close the game before deploying.")

        print("\nVice City is running and the installed ASI is locked.")
        response = input("Close the game, then press Enter to retry (or type Q to cancel): ").strip()

        if response.casefold() == "q":
            raise RuntimeError("Deployment was cancelled.")


def deploy_release(vc_dir: Path, interactive: bool) -> None:
    wait_until_game_closes(interactive)

    scripts_dir = vc_dir / "scripts"
    scripts_dir.mkdir(parents=True, exist_ok=True)

    files = (
        output_path("Release"),
        PROJECT_DIR / "TaxiManagerVC.ini",
        PROJECT_DIR / "TaxiManagerVC.destinations.csv",
        PROJECT_DIR / "northpointmallradaricon.txd",
    )

    print("\n=== Deploying Release ===")

    for source in files:
        destination = scripts_dir / source.name

        try:
            shutil.copy2(source, destination)
        except PermissionError as error:
            raise RuntimeError(
                f"Could not replace {destination.name}. Close Vice City and try again."
            ) from error

        print(f"Copied: {destination}")

    installed_asi = scripts_dir / "TaxiManagerVC.VC.asi"
    built_hash = sha256(output_path("Release"))
    installed_hash = sha256(installed_asi)

    if installed_hash != built_hash:
        raise RuntimeError("The built and installed Release ASI hashes do not match.")

    print(f"SHA-256: {installed_hash}")
    print("Deployment hash matches the Release output.")


def main() -> int:
    interactive = len(sys.argv) == 1
    arguments = show_console_menu() if interactive else parse_arguments()

    if arguments is None:
        return 0

    try:
        sdk_dir = resolve_sdk_dir(arguments.sdk)
        msbuild = find_msbuild()
        vc_dir = resolve_vc_dir(arguments)
        configurations = (
            ("Debug", "Release") if arguments.configuration == "all"
            else (arguments.configuration.capitalize(),)
        )

        print(f"Plugin-SDK: {sdk_dir}")
        print(f"MSBuild:    {msbuild}")
        print(f"Deployment: {vc_dir if vc_dir else 'disabled'}")

        for configuration in configurations:
            build_configuration(
                msbuild,
                sdk_dir,
                configuration,
                arguments.rebuild,
            )
            report_output(configuration)

        if vc_dir is not None:
            deploy_release(vc_dir, interactive)

    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"\nBuild failed: {error}", file=sys.stderr)

        if interactive:
            input("\nPress Enter to close...")

        return 1

    print("\nBuild completed successfully.")

    if interactive:
        input("\nPress Enter to close...")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
