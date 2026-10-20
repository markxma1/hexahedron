# Android port (planned)

The PC backend in `../sdl3_gl/` already runs on Android through SDL3. This folder
will hold only what Android needs on top of it:

* the Gradle project (SDL3's `android-project` template) that builds
  `src/core`, `src/game` and `src/platform/sdl3_gl` with `-DHX_GLES` (GLES 3.0 shaders),
* touch controls mapped to the existing actions:
  swipe left/right = roll, swipe down = faster, tap = drop, two-finger drag = look around,
  pinch = zoom (`InputState::zoomSteps`),
* save files through `SDL_GetPrefPath` (already used by the backend).
