# Omoti Music Client

Omoti Music Client is a custom Minecraft Bedrock DLL fork focused on an in-game music experience.

It keeps the injected client lightweight and offloads audio playback to an embedded helper runtime that is extracted and launched on demand, reducing in-process audio conflicts while keeping the UI responsive.

## What This Branch Adds

- `O` key music menu
- Scrollable track list
- Play, pause, resume, stop, previous, next, shuffle, seek, and volume control
- In-game mini "Now Playing" HUD
- `HUD` quick panel for moving the mini HUD
- `Keys` quick panel for rebinding menu-related keys
- Embedded `OmotiMusicHelper` runtime package inside the DLL
- Custom UI font support through `assets/fonts/omoti_ui.ttf`

## Project Layout

- [`src/`](./src): injected client source
- [`assets/`](./assets): embedded textures, icons, language files, and UI font
- [`tools/OmotiMusicHelper/`](./tools/OmotiMusicHelper): helper source used for the embedded playback runtime
- [`tools/OmotiManager/`](./tools/OmotiManager): desktop-side utility sources

## Requirements

### DLL build

- Visual Studio 2022 or Build Tools with MSVC
- CMake 3.22+
- Ninja
- MinGW-w64 `ld.exe` for embedding binary resources
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

## Embedded Music Helper

The normal DLL build publishes the helper and embeds these runtime files into the DLL:

- `OmotiMusicHelper.exe`
- `D3DCompiler_47_cor3.dll`
- `PenImc_cor3.dll`
- `PresentationNative_cor3.dll`
- `vcruntime140_cor3.dll`
- `wpfgfx_cor3.dll`

If you want to debug the helper separately, you can still publish it directly:

```bat
dotnet publish tools/OmotiMusicHelper/OmotiMusicHelper.csproj ^
  -c Release ^
  -r win-x64 ^
  --self-contained true ^
  /p:PublishSingleFile=true
```

## Runtime Setup

For normal use, you only need:

- `Omoti.dll` or the copied `latite.dll`

The DLL extracts the embedded helper package into the Omoti runtime folder and launches it automatically when music playback is needed.

The extracted helper shuts itself down if:

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
- The music playback path is helper-based by design. The DLL handles UI and control flow; the extracted helper handles audio playback.
- This branch is tuned for the Omoti Music Client workflow rather than the stock upstream Latite experience.

## License

See [`LICENSE`](./LICENSE).
