# Phasmophobia Toolbox

[中文（主文档）](README.md) · [Development guide](docs/README.md) · [Changelog](CHANGELOG.md)

An open source Windows x64 toolbox for Phasmophobia, built with C++17, IL2CPP, Direct3D 11, ImGui and Detours.
The interface and primary documentation are in Chinese. First-party code is MIT licensed.

Strings are plain text and the standalone executable embeds raw DLL bytes.
String obfuscation, payload encryption, split keys and runtime payload authentication have been removed.
Both the standalone injector and game EXE embedding remain available.

The latest supplied dump was checked on **2026-10-11**: **100/100 existing metadata contracts** passed.
See the [compatibility record](docs/game-update-2026-10-11.md).
Static checks do not establish that every feature works in the running game.

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

Requires Windows 10/11 x64, Visual Studio 2022 or newer with Desktop development with C++, Windows SDK, CMake 3.15+ and Ninja.
PowerShell scripts locate and load the MSVC x64 environment automatically.

```powershell
./scripts/check-all.ps1 -Configuration Release
```

This checks repository hygiene and secrets, builds the project, runs **6 CTest tests** and **7 embedding regression scenarios**.
Release outputs are placed in `release/`. Debug outputs and build intermediates are in `out/`.

Start the game, enter the main menu, run `release/standalone/PhasmophobiaInjector.exe`, then press **U** in game.
The standalone EXE contains the payload; no separate DLL is required for distribution.
Run as administrator when the injector reports an access problem.

```powershell
./scripts/test.ps1 -Configuration Debug
./scripts/test.ps1 -Configuration Release -StaticAnalysis
```

GitHub Actions runs repository checks, Debug build/tests and MSVC native static analysis.

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

## Embed in the game EXE

Close the game and use the original x64 game EXE as input. Generate a candidate package first:

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
