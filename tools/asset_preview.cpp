// Dumps the procedural textures as PAM images (convert with ImageMagick).
#include "../src/game/assets.h"
#include <cstdio>
#include <string>
using namespace hx;
static void save(const Image& im, const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "wb");
    std::fprintf(f, "P7\nWIDTH %d\nHEIGHT %d\nDEPTH 4\nMAXVAL 255\nTUPLTYPE RGB_ALPHA\nENDHDR\n", im.w, im.h);
    std::fwrite(im.px.data(), 1, im.px.size(), f);
    std::fclose(f);
}
int main(int argc, char** argv) {
    std::string out = argc > 1 ? argv[1] : ".";
    save(makeDiceAtlas(256), out + "/dice.pam");
    save(makeSpriteAtlas(), out + "/sprites.pam");
    save(makeGridCell(128), out + "/grid.pam");
    for (int t = 0; t < 7; ++t) save(makeBackground(t, 960, 540), out + "/bg" + std::to_string(t) + ".pam");
}
