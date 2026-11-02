# Odin libretro core

The libretro core of [Odin](https://github.com/Tovarichtch/Odin), the SEGA Hikaru emulator: the board and its OpenGL renderer as `odin_libretro`, without the program's window, menu and SDL.

## Building

```
git submodule update --init 3rdparty/glew 3rdparty/jansson
cmake -B build -DODIN_LIBRETRO=ON -DODIN_NATIVE=OFF
cmake --build build
```

The core is `build/odin_libretro.so` (`.dll` on Windows); `odin_libretro.info` goes in RetroArch's `info/` folder. On Linux it links only `libOpenGL.so.0` (glvnd), no X11. The CI builds it for Linux x86_64 and aarch64 (`.github/workflows/libretro.yml`).

## Playing

- Load a game's MAME set as content: `airtrix`, `airtrixo`, `braveff`, `pharrier`, `sgnascar`, `sgnascaro`, `swracer` or `von4` (`.zip`, `.7z` or a folder).
- The shared sets `hikaru` and `mie` (and the optional `jvs13551`, `18wheelr`, `srally2`) go beside the game or in `system/odin`. A missing file is named in RetroArch's log with the set it belongs to.
- RetroArch's video driver has to be `glcore` (OpenGL 4.3).
- What the board keeps without power (its EEPROMs, backup RAM, Virtual-On's card) goes to `saves/odin/<game>`.

## Controls

The controls of each game come from its list (`odin-games.json`), by their roles:

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
| Light gun | RetroArch's light gun, reload off-screen |

## Core options

- **Internal Resolution**: 1x (640x480) to 6x (3840x2880), on the fly.
- **HUD at Higher Resolutions**: Snap (default), Native or Raw.
- **Anisotropic Filtering**: Off (default) to 16x.

## Not there yet

Save states, Vulkan, Windows and macOS builds, network links between cabinets, force feedback.
