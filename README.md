# Omoti Music Client

Omoti Music Client is a custom Minecraft Bedrock DLL fork focused on an in-game music experience.

It keeps the injected client lightweight and offloads audio playback to a separate helper process so the UI can stay responsive while reducing in-process audio conflicts.

## What This Branch Adds

- `O` key music menu
- Scrollable track list
- Play, pause, resume, stop, previous, next, shuffle, seek, and volume control
- In-game mini "Now Playing" HUD
- `HUD` quick panel for moving the mini HUD
- `Keys` quick panel for rebinding menu-related keys
- External `OmotiMusicHelper.exe` playback backend
- Custom UI font support through `assets/fonts/omoti_ui.ttf`

## Project Layout

- [`src/`](./src): injected client source
- [`assets/`](./assets): embedded textures, icons, language files, and UI font
- [`tools/OmotiMusicHelper/`](./tools/OmotiMusicHelper): external audio playback helper
- [`tools/OmotiManager/`](./tools/OmotiManager): desktop-side utility sources

## Requirements

### DLL build

- Visual Studio 2022 or Build Tools with MSVC
- CMake 3.22+
- Ninja
- MinGW-w64 `ld.exe` for embedding assets

### Helper build

- .NET 8 SDK

## Building The DLL

Open a Visual Studio developer shell and configure the project:

```bat
call C:\BuildTools\Common7\Tools\VsDevCmd.bat -arch=x64
cmake -S . -B out/build/x64-release-clean ^
  -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_C_COMPILER=cl.exe ^
  -DCMAKE_CXX_COMPILER=cl.exe ^
  -DCMAKE_CXX_FLAGS=/EHsc ^
  -DMINGW_LD="C:/path/to/mingw64/bin/ld.exe"
```

Then build:

```bat
call C:\BuildTools\Common7\Tools\VsDevCmd.bat -arch=x64
cmake --build out/build/x64-release-clean --config Release -j 8
```

Main output:

- `out/build/x64-release-clean/Omoti.dll`

This branch also includes a post-build copy step that writes versioned DLLs into `E:/latiteskid`.

## Building The Music Helper

Build the helper with .NET:

```bat
dotnet publish tools/OmotiMusicHelper/OmotiMusicHelper.csproj ^
  -c Release ^
  -r win-x64 ^
  --self-contained true ^
  /p:PublishSingleFile=true
```

Typical output:

- `tools/OmotiMusicHelper/bin/Release/net8.0-windows/win-x64/publish/OmotiMusicHelper.exe`

## Runtime Setup

Place these files together before loading the DLL:

- `Omoti.dll` or the copied `latite.dll`
- `OmotiMusicHelper.exe`

The helper is launched automatically on demand. It also shuts itself down if:

- the owner process exits
- the client stops talking to it for a short time
- the client explicitly sends a shutdown request on eject

## Music UI

- Press `O` to open the music menu
- Use the left panel to browse tracks
- Use the right panel for playback controls
- Use `HUD` to move the mini HUD
- Use `Keys` to rebind supported keys

The mini HUD shows:

- artwork
- track title
- artist
- elapsed time
- playback state

## Custom Font

To replace the UI font, swap this file before building:

- `assets/fonts/omoti_ui.ttf`

The renderer loads that TTF directly for the music UI.

## Notes

- This repository intentionally excludes large publish outputs and bundled vendor binaries from version control.
- The music playback path is helper-based by design. The DLL handles UI and control flow; the helper handles audio playback.
- This branch is tuned for the Omoti Music Client workflow rather than the stock upstream Latite experience.

## License

See [`LICENSE`](./LICENSE).
