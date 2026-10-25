# PS3 port (PSL1GHT)

See "Building for other platforms" in the main README.

- `rsx_renderer.*`: RSX implementation of `hx::Renderer` (FP16 HDR, bloom, ACES)
- `main.cpp`: pad, audio port, files, system menu
- `shaders/`: Cg versions of the PC shaders
- `tools/build_cgcomp.sh`: shader compiler without -fastprecision
