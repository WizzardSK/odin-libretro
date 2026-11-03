# Odin libretro core

The libretro core of [Odin](https://github.com/Tovarichtch/Odin), the SEGA Hikaru emulator: the board and its OpenGL and Vulkan renderers as `odin_libretro`, without the program's window, menu and SDL.

Odin's own README is in [README](README), how to build and run the desktop program in [INSTALL](INSTALL).

## Installing

The core is on its way to the libretro buildbot (Linux x86_64 and aarch64, Windows x64; the recipe is `.gitlab-ci.yml`). Once it is there: in RetroArch, Main Menu > Online Updater > Core Downloader > Odin; until its core info is part of RetroArch's (libretro/libretro-super#2183), also put [`odin_libretro.info`](odin_libretro.info) in RetroArch's `info/` folder. Or by hand from the [buildbot's nightlies](https://buildbot.libretro.com/nightly/): `linux/x86_64/latest/odin_libretro.so.zip`, `windows/x86_64/latest/odin_libretro.dll.zip`, unzipped into RetroArch's `cores/`.

Until then, the cores are the artifacts of this repository's [CI runs](https://github.com/WizzardSK/odin-libretro/actions/workflows/libretro.yml) on the `libretro` branch (signed in to GitHub). The Linux aarch64 core is tested in RetroArch with both renderers (Planet Harriers at full speed with Threaded Board); Windows testers run the Windows one. Reports go to the Odin thread in #support-forum on the libretro Discord, or to this repository's issues.

## Building

```
git submodule update --init 3rdparty/glew 3rdparty/jansson
cmake -B build -DODIN_LIBRETRO=ON -DODIN_NATIVE=OFF
cmake --build build
```

The core is `build/odin_libretro.so` (`.dll` on Windows); `odin_libretro.info` goes in RetroArch's `info/` folder. On Linux it links only `libOpenGL.so.0` (glvnd), no X11. The CI builds it for Linux x86_64 and aarch64 and for Windows x86_64 with MSYS2 UCRT64 (`.github/workflows/libretro.yml`).

## Playing

- Load a game's MAME set as content: `airtrix`, `airtrixo`, `braveff`, `pharrier`, `sgnascar`, `sgnascaro`, `swracer` or `von4` (`.zip`, `.7z` or a folder).
- The shared sets `hikaru` and `mie` (and the optional `jvs13551`, `18wheelr`, `srally2`) go beside the game or in `system/odin`. A missing file is named in RetroArch's log with the set it belongs to.
- RetroArch's video driver has to be `vulkan` (Vulkan 1.3, or 1.1 with dynamic rendering, synchronization2 and the extended dynamic state) or `glcore` (OpenGL 4.3).
- The mesh programs (shaders) of a game are made ready before its first frame from the list Odin comes with (`system/odin/shaders/<game>.txt`) and the one kept as the game draws (`system/odin/cache/shaders/<game>.txt`, with the driver's programs in `system/odin/cache/programs`), as the desktop Odin keeps them, so the game stutters less the next time.
- What the board keeps without power (its EEPROMs, backup RAM, Virtual-On's card) goes to `saves/odin/<game>`.
- Save states work; a state loads only into the build of the core that made it.

## Controls

The controls of each game come from its list (`odin-games.json`), by their roles; RetroArch's Controls menu shows each input with the name the game's INPUT TEST gives it:

| Role | RetroPad |
|---|---|
| Start | Start |
| Coin | Select (player 1 and 2) |
| Service | L3 |
| Test | R3 (player 1) |
| Directions | D-pad |
| Buttons 1-8 | B, A, Y, X, L, R, L2, R2 |
| Stick, stick 2 | left stick (and D-pad), right stick |
| Steering | left stick |
| Accelerator, brake | R2, L2 (analog) |
| Gear up, gear down, gear lever | R, L |
| Sight (Brave Firefighters' hose) | left stick; with the port's device set to Light Gun (Controls > Port N Controls > Device Type), RetroArch's light gun, reload off-screen |

A control the game list gives inputs of the gamepad takes those, as the desktop Odin does: Brave Firefighters' Water on R2 and Fog on L2.

## Core options

- **Renderer** (Restart): Automatic (default), OpenGL where RetroArch's video driver is an OpenGL one and Vulkan for any other; OpenGL; Vulkan. When RetroArch cannot give the one asked for, the core runs on the other with a notice.
- **Internal Resolution**: 1x (640x480) to 6x (3840x2880), on the fly.
- **HUD at Higher Resolutions**: Snap (default), Native or Raw.
- **Threaded Board** (Restart): Enabled (default), the board runs the next frame on a thread of its own while the frame before is drawn, as the desktop Odin does; faster, one frame more of latency. Disabled: each frame run and drawn in turn.
- **Rumble**: Enabled (default), the pads shake as the cabinet does: its rumble outputs, the vibration device the optional sound board drives (Star Wars Racer Arcade) and the jolts of the steering wheel (NASCAR Arcade).
- **Sound Mode** and **Optional Board Level**: shown only for the three games with an optional sound board, each with its own: Main, Both (the optional board mixed in at Optional Board Level: -12 dB to +12 dB, for NASCAR Arcade -24 dB to 0 dB), 4.0 (the desktop's Quad: four channels, the main board in front, the optional one behind at Optional Board Level, through RetroArch's multi-channel sound, which folds them for stereo; a RetroArch without it gets Both) or Optional Only. Defaults: Planet Harriers and Star Wars Racer Arcade Both at 0 dB, NASCAR Arcade Both at -12 dB (its optional board has the engines).
- **Anisotropic Filtering**: Off (default) to 16x.

- **Audio Thread** (Restart): Disabled (default) or Enabled, the sound handed to RetroArch's audio thread, which plays what is buffered while a frame takes long.

## Not there yet

A macOS build, network links between cabinets, force feedback on a wheel.
