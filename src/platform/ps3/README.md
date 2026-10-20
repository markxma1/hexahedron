# PS3 backend (planned)

Target: PS3 homebrew with the open-source PSL1GHT SDK.

What this folder will contain - the rest of the game (`src/core`, `src/game`) is reused unchanged:

| File | Implements (`src/game/platform.h`) | PS3 API |
|---|---|---|
| `main.cpp` | main loop, `InputState` | `ioPad` (right stick → `lookRateX/Y`) |
| `rsx_renderer.cpp` | `Renderer` | RSX via `rsxutil`/libgcm or tiny3d, 2 Cg shaders (lit cube, sprite) |
| `ps3_audio.cpp` | `Audio` | `audio` ring buffer, float samples |
| `ps3_files.cpp` | `Files` | `/dev_hdd0/game/HEXAHEDRON/USRDIR/` |

Notes:

* Build the portable libraries with `-DHX_NO_THREADS` (backgrounds are then generated
  step by step on the main thread).
* Save files are plain text, so the big-endian CPU needs no special handling.
* Bloom/tonemapping are optional: a plain `beginOverlay()` that just switches to 2D is enough.
* Packaging: `make pkg` → `HEXAHEDRON.pkg` (install via Package Manager / webMAN).
