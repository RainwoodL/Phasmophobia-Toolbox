# Phasmophobia Toolbox

[中文（主文档）](README.md) · [Development guide](docs/README.md) · [Changelog](CHANGELOG.md)

An open source Windows x64 toolbox for Phasmophobia, built with C++17, IL2CPP, Direct3D 11, ImGui and Detours.
The interface and primary documentation are in Chinese. First-party code is MIT licensed.

Strings are plain text and the standalone executable embeds raw DLL bytes.
String obfuscation, payload encryption, split keys and runtime payload authentication have been removed.
The Windows x64 release includes a standalone injector and a game launcher with the toolbox built in.

Checked Steam build **26893244** on **2026-11-25**. Of the 100 historical checks, 80 match the previous dump. Teammate nameplates, hunt distance and ghost speed estimation now use Unity’s public coordinate APIs. The host continues to share hunt speed with teammates even when their own speed display is off. The HUD also labels Dowsing Rods and skips an invalid head-position fallback. In-game multiplayer verification is pending. [Compatibility record](docs/game-update-2026-11-25.md)

## Features

- Smudge, crucifix, hunt and investigation timers.
- Ghost status, hunting distance, movement speed and multiplayer speed synchronization.
- Player sanity, stamina, nicknames and Ouija board translation.
- Crosshair interaction information, inventory bar and screen crosshair.
- Configurable shortcuts, positions, fonts, colors and persistent settings.

Press **U** to toggle the menu. Hold **Alt** to move widgets.
Defaults: **I** starts the smudge timer, **O** clears it, and **P** clears the crucifix timer.
Settings are stored in `%LOCALAPPDATA%/GhostRoomTools/settings.bin`.

## Build and run

Download and extract `Phasmophobia-Toolbox-v2026.11.25-win-x64.zip`. The release page lists the archive’s SHA-256. Choose one of the two executables. [Releases](https://github.com/RainwoodL/Phasmophobia-Toolbox/releases/latest)

- **Standalone injector.** Start the original game from Steam, enter the main menu, then run the extracted `Toolsbox.exe`. Run it again after each game restart. [Injector guide](docs/release-injector.md)
- **Embedded launcher.** Close the game and choose **Manage → Browse local files** in Steam. Back up the original `Phasmophobia.exe` outside the game directory, copy the extracted file of the same name into its place, then start the game from Steam. This project-built launcher uses the Unity runtime, game assemblies and data in your installed copy. To restore the original, verify the game files in Steam or use a backup matching your current game version. [Installation and recovery guide](docs/release-embedded.md)

Press **U** in game after the toolbox loads.

### Build from source

Requires Windows 10/11 x64, Visual Studio 2022 or newer with Desktop development with C++, Windows SDK, CMake 3.15+ and Ninja.
PowerShell scripts locate and load the MSVC x64 environment automatically.

```powershell
./scripts/check-all.ps1 -Configuration Release
```

This checks repository hygiene and secrets, builds the project and runs **7 CTest tests**, including the embedded launcher test, and **9 local embedding script regression scenarios**.
Release outputs are placed in `release/`. Debug outputs and build intermediates are in `out/`.

```powershell
./scripts/test.ps1 -Configuration Debug
./scripts/test.ps1 -Configuration Release -StaticAnalysis
```

GitHub Actions runs repository checks, Debug build/tests and MSVC native static analysis.

After committing source changes, run `./scripts/package-release.ps1 -Version v2026.11.25`. The ZIP has two entries at its root, `Toolsbox.exe` and `Phasmophobia.exe`. The build directory `out/release-packages/v2026.11.25/` also holds `build-manifest.json` with the source commit and executable hashes, and `SHA256SUMS.txt` with the archive hash. Publish the archive hash on the release page.

## Game updates

Keep the previous dump before comparing a new one. Python 3.10+ is required; no third-party Python packages are needed.

```powershell
python ./tools/dump_compatibility.py 'D:/dumps/previous.cs' 'D:/dumps/latest.cs' --output './out/dump-audit.json'
python ./tools/test_dump_compatibility.py 'D:/dumps/previous.cs' 'D:/dumps/latest.cs'
```

Exit codes: `0` for matching checked contracts, `1` for required review, `2` for input/parse errors.
Obfuscated identifiers are normalized, so matching types and offsets alone cannot prove field semantics.
Validate hunts, scene transitions, RPCs and multiplayer synchronization in the game after updating.
Game dumps, assemblies, local reverse engineering tools and generated binaries are excluded from source control.

## Optional local embedding workflow for developers

The repository also includes a script for testing an embedded copy of the locally installed game EXE. Close the game and use the original x64 EXE to generate a candidate.

```powershell
./scripts/embed-game.ps1 -GameExecutable 'D:/Steam/steamapps/common/Phasmophobia/Phasmophobia.exe' -DumpPath 'D:/dumps/latest.cs' -PackageOnly
```

Remove `-PackageOnly` to back up and deploy the embedded EXE.
The script rejects repeated `.tbx` embedding, validates candidates, uses atomic replacement and rolls back failed deployments.
Artifacts include the standalone injector, `release/embedded/Phasmophobia.embedded.exe`, `payload.dll`, `release/tools/pe_embed.exe` and the SHA-256 manifest.
Original game backups are stored in `release/backups/`.
The bootstrap waits five seconds, checks required modules and writes `.toolsbox.payload.dll` with normal file attributes.
Regenerate after Steam updates/file verification or moving the game installation.

## License and support

First-party code is [MIT licensed](LICENSE). Dependency and image notices are in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
Use [GitHub Issues](https://github.com/RainwoodL/Phasmophobia-Toolbox/issues) for bugs and suggestions, and [SECURITY.md](SECURITY.md) for security reports.
Author: 霖木 / RainwoodL. Chinese community QQ group: 304393689.
