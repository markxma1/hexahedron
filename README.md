# HEXAHEDRON

A 3D dice-rolling puzzle game: dice fall into a well, **roll** one face with every
move, and clear in groups of matching numbers. Modern graphics at any resolution,
procedural music and sound, written in portable C++ so it can run on PC, Android and PS3.

<sub>Inspired by **Hexaxis XXI** (PSP homebrew, 2007) by Tim Hackett / Darksoft.
This is an independent fan project with its own code, graphics, sounds and texts;
nothing from the original game is included. It is not affiliated with or endorsed by the original author.</sub>

Made by Xma1, 2026, developed with the help of AI (Claude by Anthropic).

## Download

Ready-made builds for every platform are on the
[releases page](https://github.com/markxma1/hexahedron/releases):

| File | Platform | How to start |
|---|---|---|
| `hexahedron_<version>_amd64.deb` | Ubuntu / Debian (22.04 or newer) | double-click to install, then HEXAHEDRON in the app menu |
| `HEXAHEDRON-linux-x86_64.tar.gz` | any Linux (glibc 2.35+) | unpack, run `HEXAHEDRON/hexahedron` |
| `HEXAHEDRON-windows-x64.zip` | Windows 10/11 | unpack, run `hexahedron.exe` |
| `HEXAHEDRON-macos-universal.zip` | macOS 11+ (Apple Silicon and Intel) | unpack; first start: right-click → Open (not signed) |
| `HEXAHEDRON-android.apk` | Android 7+ | open the file on the phone, allow installing from this source |
| `HEXAHEDRON-ps3.pkg` | PS3 with custom firmware | copy to `/dev_hdd0/packages/`, install with Package Manager |

## Playing

Build and run from source on PC (Linux, Windows with MSYS2, macOS) with SDL3 installed:

```
cmake -S . -B build -G Ninja && cmake --build build
./build/hexahedron
```

| Action | Keyboard / mouse | Gamepad |
|---|---|---|
| Roll the die left / right | ← → / A D | D-pad / left stick |
| Roll down faster | ↓ / S | D-pad ↓ |
| Drop | ↑ / W / Space | A |
| Pause | Esc / P | Start |
| Help (14 animated pages) | F1 / H | Select |
| Zoom | Q / E / mouse wheel | LB / RB |
| Look around (a little) | drag with the mouse / Z C | right stick |
| Reset the view | X / middle click / double click | right stick click |
| Change theme | [ / ] | (pause menu) |
| Fullscreen | F11 / Alt+Enter | |

Command line: `--play` (skip the title), `--mode standard|advanced|quick`,
`--theme 0-6`, `--size 1920x1080`, `--mute`, `--autoplay` (the computer plays).

## Rules

* Dice fall from the top and **roll** one face per cell, downward as well as sideways.
  The face pointing at you is what counts.
* N or more connected N's (2–6) form a combo. Ones never combo.
* A new combo is **blue**. If no die is added in time it turns **red**.
  The **wipe line** sweeps top to bottom and clears red dice.
* Random dice rise from the bottom; an arrow shows the next column. When the grid is full, the game is over.
* Multiplier = **chain** (+1 per die in a combo) + **wipes** (+1 when the line reaches
  the bottom while combos are present, otherwise everything resets to 0) + **drops**
  (+1 per die of a combo formed by dice falling after a clear).
* Modes: Standard (with preview line), Advanced (no preview), Quick Run (3 minutes).
* 7 themes: Deep Space, Vortex, Firefly Night, Card Storm, Kaleidoscope, Quicksilver,
  Gold Rush. Each has its own procedural music, and the wipe line runs in time with
  it (2 bars per sweep).

### Speed and levels (Tetris style)

Before each game you pick a **start level (1–15)**. The first levels are extra gentle
so you can get used to how the dice roll: level 1 runs at half speed, levels 2–4 get
a little faster each, and from level 5 on the normal curve applies.
Every 25 cleared dice the level goes up.
Labels: 1–2 Relaxed, 3–5 Normal, 6–8 Fast, 9–11 Expert, 12–13 Speedster, 14–15 Impossible.

| | Level 1 | Level 5 | Level 10 | Level 15 |
|---|---|---|---|---|
| Fall speed (s per cell) | 2.30 | 0.60 | 0.27 | 0.12 |
| Random dice | 1 every 32 s | 1 every 11 s | 2 every 5 s | 3 every 2.5 s |
| Blue → red | 4.0 s | 3.4 s | 2.65 s | 1.9 s |

## Visual helpers

* **Landing lane**: a glass tube from the falling die down to the cell where it will
  land, with light pulses running toward the target and a wireframe ghost cube there.
* The camera leans slightly toward the column you are playing in, and you can look
  at the board from the side with the mouse or the right stick.
* **How to play** shows a live example on the board for every page, played by the
  real rules engine (rolling, blue → red, wipe, preview, rising dice, multipliers, drop combos).

## Architecture: swappable layers

```
src/core/      Game rules. Plain C++17, no dependencies, unit tests in tests/.
               Input in (left/right/down/drop), state + events out.
src/game/      Presentation, also portable: camera, animation, particles, HUD, menus,
               help scenes, font rendering (stb_truetype), procedural textures,
               synthesizer for music and sound effects.
               Talks to the hardware ONLY through src/game/platform.h.
src/platform/  Backends. Only platform-specific code lives here:
  sdl3_gl/     PC (Linux/Windows/macOS): SDL3 + OpenGL 3.3, HDR + MSAA + bloom + tonemapping.
  android/     Gradle project; native code = sdl3_gl with GLES 3.0 + touch.
  ps3/         PSL1GHT backend: RSX renderer (FP16 HDR + bloom), ioPad, audio port.
```

`platform.h` is deliberately small:

* **Renderer**: upload RGBA8 textures and meshes, `drawMesh` (lit), `drawTriangles`
  (unlit, 2D or 3D), `beginOverlay` (post FX, then HUD).
* **Audio**: accepts finished stereo float samples. Synthesis runs portably in the game layer.
* **Files**: read assets, save small text files (settings, high scores).
* **Input**: held buttons as abstract actions, plus optional camera look / zoom values.

Besides `platform.h` there is one more tiny interface, `src/game/worker.h`: a single
background thread (std::thread by default, a native PPU thread on PS3). Fonts, textures
and the procedural backgrounds are built on it while the main thread shows a loading
screen, so the game never looks frozen on slow CPUs.

All saves are text files, so nothing depends on byte order (the PS3 is big-endian).

### Godot as a frontend

Wrap `src/core` in a GDExtension (a class around `hx::Game`). Godot handles graphics
and sound; the rules stay identical.

## Building for other platforms

### Releases (GitHub Actions)

`.github/workflows/release.yml` builds every platform on GitHub's machines. Push a
version tag and the files appear as a release a few minutes later:

```
git tag v0.2 && git push origin v0.2
```

Linux is built on Ubuntu 22.04 with SDL3 linked statically (plus a `.deb` from
`.github/scripts/make_deb.sh`), Windows with MSYS2/MinGW (static, no console window),
macOS as a universal binary. The PC builds use `.github/scripts/build_pc.sh`, which
also works locally. Started by hand from the Actions tab, the files stay as run artifacts.

### Shared code

All platforms share `src/core` (rules) and `src/game` (presentation, menus, audio synth, host loop, touch gestures). Every `.cpp` there is picked up automatically by CMake and by the PS3 Makefile. Only `src/platform/<backend>` is platform specific.

### Android (`src/platform/android`)

The backend code is shared with PC (`src/platform/sdl3_gl`), using OpenGL ES 3.0 and touch controls.

**Requirements:** JDK 17, Android SDK (platform `android-35`, build-tools `35.0.0`), NDK `27.3.13750724`, CMake `3.31.6`. SDL3 3.4.18 is fetched automatically by CMake.

**Build:**
```bash
cd src/platform/android
echo "sdk.dir=/path/to/android-sdk" > local.properties
./gradlew assembleDebug
```
Output: `app/build/outputs/apk/debug/app-debug.apk` (arm64-v8a + x86_64). Install with:
```bash
adb install -r app-debug.apk
```

**Touch Controls:**
- Swipe left/right: roll
- Drag down: faster; Fast flick down: drop
- Swipe up: drop / menu up
- Tap: menus and on-screen buttons (pause top centre, back top left)
- Two fingers: look around; Pinch: zoom; Two-finger tap: reset view
- Android Back button: back / pause

**PC Test:** `./build/hexahedron --gles`

### PS3 (`src/platform/ps3`, PSL1GHT homebrew)

**Requirements:** ps3dev toolchain with PSL1GHT (prebuilt nightly at github.com/ps3dev/ps3dev releases), NVIDIA Cg Toolkit 3.1 (`libCg.so` for shader compiler), and a PSL1GHT source checkout to build the patched shader compiler once.

**Build:**
```bash
export PS3DEV=/path/to/ps3dev PSL1GHT=$PS3DEV
export PATH=$PS3DEV/bin:$PS3DEV/ppu/bin:$PATH
export LD_LIBRARY_PATH=/path/to/Cg/usr/lib64
cd src/platform/ps3
tools/build_cgcomp.sh /path/to/PSL1GHT-source    # once
make          # hexahedron.self (run with ps3load or in RPCS3)
make pkg      # hexahedron.pkg (install via Package Manager on custom firmware console)
```

**Details:**
- Renders at 720p (fallbacks: 1080p/576p/480p) with FP16 HDR + bloom. Fonts are linked into the binary.
- Saves (`settings.txt`, `scores.txt`) go next to the program (USRDIR when installed).

**Controller:**
- D-pad/left stick: roll
- Cross: drop / confirm; Circle: back
- Start: pause; Select: help
- L1/R1: zoom; L2/R2: theme
- Right stick: look; R3: reset view

**Testing in RPCS3:** Place an optional `args.txt` next to the program with PC-style options (e.g., `--autoplay --play --mute`).

**Known PSL1GHT Pitfalls Handled:**
- Prebuilt `librsx` breaks on command-buffer segment switches inside calls; renderer switches segments itself first.
- `cgcomp`'s `-fastprecision` mixes half/full registers; patched `cgcomp` removes this.
- `float4 TEXCOORD` loses `w` under perspective; vertex color uses `COLOR0`.
- Fragment program constants are patched into a per-draw CPU copy.

## Tests / tools

* `build/core_tests`: rule tests (rolling, combos, blue → red, wipe, drop combos,
  multiplier reset, game over, speed levels, multi-column rising)
* `build/touch_tests`: touch gesture tests (swipe steps, flick drop, two-finger look/zoom/tap)
* `build/asset_preview <dir>`: all procedural textures as images
* `build/audio_preview <dir>`: music of every theme + sound effects as WAV
* `hexahedron --autoplay --screenshot out.ppm 600`: screenshot after 600 frames;
  `--press FRAME ACTION` scripts key presses, `--tap FRAME X Y` scripted taps,
  `--touch-ui` shows the touch buttons, `--gles` uses the OpenGL ES path like Android
  (`HX_SAVE_DIR=/tmp/x` keeps the real save files clean)

## How this was made

HEXAHEDRON is an independent, clean-room-style reimplementation:

* No source code of Hexaxis XXI was available or used, and its program code was never
  disassembled or decompiled.
* The game rules were derived from the original's observable behavior: its in-game help
  text, screenshots and public descriptions of the game.
* All code, graphics, music, sounds and texts were written from scratch for this project
  (help texts and theme names in its own words).

## Credits / licenses

* Code: MIT license, see `LICENSE`. Copyright (c) 2026 Xma1.
* Developed with the help of AI (Claude by Anthropic).
* Game idea inspired by **Hexaxis XXI** (PSP, 2007) by Tim Hackett / Darksoft.
* `third_party/stb/stb_truetype.h`: public domain / MIT (Sean Barrett).
* Fonts Orbitron and Exo 2: SIL Open Font License 1.1 (see `assets/fonts/*-OFL.txt`).
* The author name and year shown in the game can be changed at build time with
  `-DHX_REMAKE_AUTHOR=\"...\"` / `-DHX_REMAKE_YEAR=\"...\"`.
