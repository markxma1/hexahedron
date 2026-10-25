#include "rsx_renderer.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <malloc.h>
#include <sys/memory.h>
#include <sysutil/video.h>
#include <unistd.h>

// compiled shaders (cgcomp + bin2o, see Makefile)
#include "bright_fp_fpo.h"
#include "composite_fp_fpo.h"
#include "down_fp_fpo.h"
#include "full_vp_vpo.h"
#include "mesh_fp_fpo.h"
#include "mesh_vp_vpo.h"
#include "sprite_fp_fpo.h"
#include "sprite_vp_vpo.h"
#include "up_fp_fpo.h"

// Segment switch of the default command buffer (exported by librsx).
extern "C" s32 rsxContextCallback(gcmContextData* context, u32 count);

namespace hx {

// The prebuilt librsx caches context->current across its own segment-switch call
// (inline asm without a "memory" clobber), so a switch triggered INSIDE a librsx
// command overwrites the jump and the RSX reads garbage ("dead FIFO").
// We therefore switch segments ourselves before every group of commands, with
// enough room that no single draw/pass can reach the segment end.
static void ensureFifo(gcmContextData* ctx, u32 words = 1024) {
    if (ctx->current + words > ctx->end) rsxContextCallback(ctx, words);
}

namespace {
const u32 kCmdBufferSize = 0x400000;        // 4 MB command buffer
const u32 kHostSize = 8 * 1024 * 1024;      // main memory mapped for the RSX (holds the command buffer)
const u32 kLabel = 255;
u32 sLabelValue = 1;
// Same winding as OpenGL: the RSX decides front/back before the viewport flip.
const u32 kFrontFace = GCM_FRONTFACE_CCW;

void mul4(const float* a, const float* b, float* out) {
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r) {
            float s = 0;
            for (int k = 0; k < 4; ++k) s += a[k * 4 + r] * b[c * 4 + k];
            out[c * 4 + r] = s;
        }
}

u32 align(u32 v, u32 a) { return (v + a - 1) & ~(a - 1); }

const u32 kRemapIdentity =
    (GCM_TEXTURE_REMAP_TYPE_REMAP << GCM_TEXTURE_REMAP_TYPE_B_SHIFT) |
    (GCM_TEXTURE_REMAP_TYPE_REMAP << GCM_TEXTURE_REMAP_TYPE_G_SHIFT) |
    (GCM_TEXTURE_REMAP_TYPE_REMAP << GCM_TEXTURE_REMAP_TYPE_R_SHIFT) |
    (GCM_TEXTURE_REMAP_TYPE_REMAP << GCM_TEXTURE_REMAP_TYPE_A_SHIFT) |
    (GCM_TEXTURE_REMAP_COLOR_B << GCM_TEXTURE_REMAP_COLOR_B_SHIFT) |
    (GCM_TEXTURE_REMAP_COLOR_G << GCM_TEXTURE_REMAP_COLOR_G_SHIFT) |
    (GCM_TEXTURE_REMAP_COLOR_R << GCM_TEXTURE_REMAP_COLOR_R_SHIFT) |
    (GCM_TEXTURE_REMAP_COLOR_A << GCM_TEXTURE_REMAP_COLOR_A_SHIFT);
} // namespace

// Waits until the RSX has executed everything queued so far.
static void waitIdle(gcmContextData* ctx) {
    ensureFifo(ctx);
    rsxSetWriteBackendLabel(ctx, kLabel, sLabelValue);
    rsxFlushBuffer(ctx);
    while (*(vu32*)gcmGetLabelAddress(kLabel) != sLabelValue) usleep(30);
    ++sLabelValue;
}

bool RSXRenderer::init() {
    // The RSX reads the command buffer from this main-memory block through its IO mapping.
    // Take it straight from the system (1 MB pages): a malloc'd block can be remapped when
    // the C heap grows, and the RSX would then read garbage ("dead FIFO").
    sys_mem_addr_t hostAddr = 0;
    if (sysMemoryAllocate(kHostSize, SYS_MEMORY_PAGE_SIZE_1M, &hostAddr) != 0) {
        std::printf("rsx: cannot allocate %u bytes of IO memory\n", kHostSize);
        return false;
    }
    void* host = (void*)(u64)hostAddr;
    if (rsxInit(&ctx_, kCmdBufferSize, kHostSize, host) != 0) {
        std::printf("rsx: init failed\n");
        return false;
    }

    // Prefer 720p: FP16 + bloom at 1080p is too much fill rate for the RSX.
    static const u32 resIds[] = {VIDEO_RESOLUTION_720, VIDEO_RESOLUTION_1080, VIDEO_RESOLUTION_576, VIDEO_RESOLUTION_480};
    videoResolution res;
    s32 resId = -1;
    for (u32 id : resIds) {
        if (videoGetResolutionAvailability(VIDEO_PRIMARY, id, VIDEO_ASPECT_AUTO, 0) != 1) continue;
        if (videoGetResolution(id, &res) == 0) { resId = (s32)id; break; }
    }
    if (resId < 0) { std::printf("rsx: no usable resolution\n"); return false; }
    videoConfiguration cfg;
    std::memset(&cfg, 0, sizeof cfg);
    cfg.resolution = (u8)resId;
    cfg.format = VIDEO_BUFFER_FORMAT_XRGB;
    cfg.aspect = VIDEO_ASPECT_AUTO;
    cfg.pitch = res.width * 4;
    if (videoConfigure(VIDEO_PRIMARY, &cfg, NULL, 0) != 0) { std::printf("rsx: videoConfigure failed\n"); return false; }
    dispW_ = res.width;
    dispH_ = res.height;
    waitIdle(ctx_);
    gcmSetFlipMode(GCM_FLIP_VSYNC);

    colorPitch_ = dispW_ * 4;
    for (int i = 0; i < 2; ++i) {
        colorBuf_[i] = (u32*)rsxMemalign(64, dispH_ * colorPitch_);
        rsxAddressToOffset(colorBuf_[i], &colorOffset_[i]);
        gcmSetDisplayBuffer(i, colorOffset_[i], colorPitch_, dispW_, dispH_);
    }
    depthPitch_ = dispW_ * 4;
    void* depth = rsxMemalign(64, dispH_ * depthPitch_);
    rsxAddressToOffset(depth, &depthOffset_);

    ring_ = (uint8_t*)rsxMemalign(128, kRingHalf * 2);
    if (!ring_ || !colorBuf_[0] || !colorBuf_[1] || !depth) { std::printf("rsx: out of video memory\n"); return false; }
    rsxAddressToOffset(ring_, &ringOffset_);

    loadVP(vpMesh_, mesh_vp_vpo);
    loadVP(vpSprite_, sprite_vp_vpo);
    loadVP(vpFull_, full_vp_vpo);
    loadFP(fpMesh_, mesh_fp_fpo);
    loadFP(fpSprite_, sprite_fp_fpo);
    loadFP(fpBright_, bright_fp_fpo);
    loadFP(fpDown_, down_fp_fpo);
    loadFP(fpUp_, up_fp_fpo);
    loadFP(fpComposite_, composite_fp_fpo);

    float* tri = (float*)rsxMemalign(128, 6 * sizeof(float));
    const float pts[6] = {-1, -1, 3, -1, -1, 3};
    std::memcpy(tri, pts, sizeof pts);
    rsxAddressToOffset(tri, &fullVB_);

    hdr_ = makeTarget(dispW_, dispH_);
    for (int i = 0; i < kBloomLevels; ++i) bloom_[i] = makeTarget(std::max(1u, dispW_ >> (i + 1)), std::max(1u, dispH_ >> (i + 1)));

    const uint8_t white[4 * 4 * 4] = {255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
                                       255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
                                       255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
                                       255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255};
    whiteTex_ = createTexture(4, 4, white, false, false);

    rsxSetColorMask(ctx_, GCM_COLOR_MASK_B | GCM_COLOR_MASK_G | GCM_COLOR_MASK_R | GCM_COLOR_MASK_A);
    rsxSetColorMaskMrt(ctx_, 0);
    rsxSetShadeModel(ctx_, GCM_SHADE_MODEL_SMOOTH);
    rsxSetZMinMaxControl(ctx_, GCM_FALSE, GCM_TRUE, GCM_FALSE);
    for (int i = 0; i < 8; ++i) rsxSetViewportClip(ctx_, i, dispW_, dispH_);
    rsxSetUserClipPlaneControl(ctx_, GCM_USER_CLIP_PLANE_DISABLE, GCM_USER_CLIP_PLANE_DISABLE, GCM_USER_CLIP_PLANE_DISABLE,
                               GCM_USER_CLIP_PLANE_DISABLE, GCM_USER_CLIP_PLANE_DISABLE, GCM_USER_CLIP_PLANE_DISABLE);
    std::printf("rsx: %ux%u, FP16 HDR + bloom\n", dispW_, dispH_);
    return true;
}

void RSXRenderer::loadVP(VProg& p, const void* data) {
    p.prog = (rsxVertexProgram*)data;
    u32 size = 0;
    rsxVertexProgramGetUCode(p.prog, &p.ucode, &size);
}

void RSXRenderer::loadFP(FProg& p, const void* data) {
    p.prog = (rsxFragmentProgram*)data;
    rsxFragmentProgramGetUCode(p.prog, &p.ucode, &p.size);
}

RSXRenderer::Target RSXRenderer::makeTarget(int w, int h) {
    Target t;
    t.w = w; t.h = h;
    t.pitch = align((u32)w * 8, 64);                       // RGBA16F
    t.mem = rsxMemalign(128, t.pitch * h);
    rsxAddressToOffset(t.mem, &t.offset);
    return t;
}

RSXRenderer::RingPtr RSXRenderer::ringAlloc(u32 size, u32 al) {
    u32 pos = align(ringPos_, al);
    if (pos + size > kRingHalf) {
        if (!ringWarned_) { std::printf("rsx: frame ring full\n"); ringWarned_ = true; }
        pos = 0; // overwrite the start of this frame's half: glitches, but no crash
    }
    ringPos_ = pos + size;
    u32 base = ringHalf_ * kRingHalf + pos;
    return RingPtr{ring_ + base, ringOffset_ + base};
}

RSXRenderer::RingPtr RSXRenderer::bindFP(FProg& p) {
    RingPtr r = ringAlloc(p.size, 64);
    std::memcpy(r.ptr, p.ucode, p.size);
    return r;
}

// Uploading a vertex program costs FIFO space, so only do it when it changes.
void RSXRenderer::useVP(VProg& p) {
    ensureFifo(ctx_);
    if (curVP_ == &p) return;
    rsxLoadVertexProgram(ctx_, p.prog, p.ucode);
    curVP_ = &p;
}

// Fragment program constants live inside the program code. Every draw has its own copy
// (bindFP), so we patch the values straight into it from the CPU.
void RSXRenderer::fpConst(FProg& p, const RingPtr& copy, const char* name, const float* v) {
    rsxProgramConst* c = rsxFragmentProgramGetConst(p.prog, name);
    if (!c || c->index == 0xffffffff) return;
    int n = 4;
    switch (c->type) {
    case PARAM_FLOAT: case PARAM_FLOAT1: n = 1; break;
    case PARAM_FLOAT2: n = 2; break;
    case PARAM_FLOAT3: n = 3; break;
    default: break;
    }
    u32 words[4];
    for (int i = 0; i < n; ++i) {
        u32 bits;
        std::memcpy(&bits, &v[i], 4);
        words[i] = (bits >> 16) | (bits << 16);      // the RSX stores constants with swapped 16-bit halves
    }
    rsxConstOffsetTable* t = rsxFragmentProgramGetConstOffsetTable(p.prog, c->index);
    for (u32 i = 0; i < t->num; ++i) std::memcpy((uint8_t*)copy.ptr + t->offset[i], words, n * 4);
}

void RSXRenderer::vpConst(VProg& p, const char* name, const float* v) {
    ensureFifo(ctx_);
    rsxProgramConst* c = rsxVertexProgramGetConst(p.prog, name);
    if (c) rsxSetVertexProgramParameter(ctx_, p.prog, c, v);
}

void RSXRenderer::vpMatrix(VProg& p, const char* name, const float* m) {
    float t[16]; // our matrices are column-major (GL); the RSX wants rows
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) t[r * 4 + c] = m[c * 4 + r];
    vpConst(p, name, t);
}

void RSXRenderer::bindTexture(FProg& p, const char* sampler, const Tex& t, bool linearFilter) {
    ensureFifo(ctx_);
    rsxProgramAttrib* a = rsxFragmentProgramGetAttrib(p.prog, sampler);
    if (!a) return;
    u8 unit = (u8)a->index;
    gcmTexture tex;
    std::memset(&tex, 0, sizeof tex);
    tex.format = (u8)t.format;
    tex.mipmap = 1;
    tex.dimension = GCM_TEXTURE_DIMS_2D;
    tex.cubemap = GCM_FALSE;
    tex.remap = kRemapIdentity;
    tex.width = t.w;
    tex.height = t.h;
    tex.depth = 1;
    tex.location = GCM_LOCATION_RSX;
    tex.pitch = t.pitch;
    tex.offset = t.offset;
    rsxLoadTexture(ctx_, unit, &tex);
    // Only level 0 exists (linear textures): clamp the LOD there, or minified
    // sprites would sample a missing mip level and come out black.
    rsxTextureControl(ctx_, unit, GCM_TRUE, 0 << 8, 0 << 8, GCM_TEXTURE_MAX_ANISO_1);
    u8 f = linearFilter ? GCM_TEXTURE_LINEAR : GCM_TEXTURE_NEAREST;
    rsxTextureFilter(ctx_, unit, 0, f, f, GCM_TEXTURE_CONVOLUTION_QUINCUNX);
    u8 wrap = t.repeat ? GCM_TEXTURE_REPEAT : GCM_TEXTURE_CLAMP_TO_EDGE;
    rsxTextureWrapMode(ctx_, unit, wrap, wrap, GCM_TEXTURE_CLAMP_TO_EDGE, 0, GCM_TEXTURE_ZFUNC_LESS, 0);
}

RSXRenderer::Tex RSXRenderer::targetTex(const Target& t) const {
    Tex x;
    x.offset = t.offset;
    x.pitch = t.pitch;
    x.w = (u16)t.w;
    x.h = (u16)t.h;
    x.format = GCM_TEXTURE_FORMAT_W16_Z16_Y16_X16_FLOAT | GCM_TEXTURE_FORMAT_LIN;
    return x;
}

TexId RSXRenderer::createTexture(int w, int h, const uint8_t* rgba, bool mipmaps, bool repeat) {
    (void)mipmaps; // linear RSX textures have no mip levels
    Tex t;
    t.w = (u16)w; t.h = (u16)h;
    t.pitch = (u32)w * 4;
    t.repeat = repeat;
    t.format = GCM_TEXTURE_FORMAT_A8R8G8B8 | GCM_TEXTURE_FORMAT_LIN;
    t.mem = rsxMemalign(128, t.pitch * h);
    if (!t.mem) { std::printf("rsx: no memory for %dx%d texture\n", w, h); return 0; }
    uint8_t* dst = (uint8_t*)t.mem;
    for (int i = 0; i < w * h; ++i) {                      // RGBA bytes -> ARGB bytes
        dst[i * 4 + 0] = rgba[i * 4 + 3];
        dst[i * 4 + 1] = rgba[i * 4 + 0];
        dst[i * 4 + 2] = rgba[i * 4 + 1];
        dst[i * 4 + 3] = rgba[i * 4 + 2];
    }
    rsxAddressToOffset(t.mem, &t.offset);
    for (size_t i = 0; i < texs_.size(); ++i)
        if (!texs_[i].mem) { texs_[i] = t; return (TexId)(i + 1); }
    texs_.push_back(t);
    return (TexId)texs_.size(); // 1-based
}

void RSXRenderer::destroyTexture(TexId id) {
    if (id == 0 || id > texs_.size()) return;
    waitIdle(ctx_); // the GPU may still sample it
    rsxFree(texs_[id - 1].mem);
    texs_[id - 1] = Tex();
}

MeshId RSXRenderer::createMesh(const MeshVertex* v, int nv, const uint16_t* idx, int ni) {
    Mesh m;
    void* vb = rsxMemalign(128, nv * sizeof(MeshVertex));
    void* ib = rsxMemalign(128, ni * sizeof(uint16_t));
    if (!vb || !ib) return 0;
    std::memcpy(vb, v, nv * sizeof(MeshVertex));
    std::memcpy(ib, idx, ni * sizeof(uint16_t));
    rsxAddressToOffset(vb, &m.vb);
    rsxAddressToOffset(ib, &m.ib);
    m.count = ni;
    meshes_.push_back(m);
    return (MeshId)meshes_.size();
}

void RSXRenderer::setSurface(u32 colorOffset, u32 colorPitch, u32 colorFormat, int w, int h) {
    ensureFifo(ctx_);
    gcmSurface sf;
    std::memset(&sf, 0, sizeof sf);
    sf.colorFormat = (u8)colorFormat;
    sf.colorTarget = GCM_SURFACE_TARGET_0;
    sf.colorLocation[0] = GCM_LOCATION_RSX;
    sf.colorOffset[0] = colorOffset;
    sf.colorPitch[0] = colorPitch;
    for (int i = 1; i < 4; ++i) { sf.colorLocation[i] = GCM_LOCATION_RSX; sf.colorOffset[i] = 0; sf.colorPitch[i] = 64; }
    sf.depthFormat = GCM_SURFACE_ZETA_Z24S8;
    sf.depthLocation = GCM_LOCATION_RSX;
    sf.depthOffset = depthOffset_;
    sf.depthPitch = depthPitch_;
    sf.type = GCM_SURFACE_TYPE_LINEAR;
    sf.antiAlias = GCM_SURFACE_CENTER_1;
    sf.width = (u16)w;
    sf.height = (u16)h;
    sf.x = 0;
    sf.y = 0;
    rsxSetSurface(ctx_, &sf);
    setViewport(w, h);
}

void RSXRenderer::setViewport(int w, int h) {
    float scale[4] = {w * 0.5f, h * -0.5f, 0.5f, 0.0f};   // y flipped: NDC +y is the top row
    float offset[4] = {w * 0.5f, h * 0.5f, 0.5f, 0.0f};
    rsxSetViewport(ctx_, 0, 0, (u16)w, (u16)h, 0.0f, 1.0f, scale, offset);
    rsxSetScissor(ctx_, 0, 0, (u16)w, (u16)h);
}

static void unbindAttribs(gcmContextData* ctx) {
    const u8 used[] = {GCM_VERTEX_ATTRIB_POS, GCM_VERTEX_ATTRIB_NORMAL, GCM_VERTEX_ATTRIB_TEX0, GCM_VERTEX_ATTRIB_COLOR0};
    for (u8 a : used) rsxBindVertexArrayAttrib(ctx, a, 0, 0, 0, 0, GCM_VERTEX_DATA_TYPE_F32, GCM_LOCATION_RSX);
}

void RSXRenderer::beginFrame(const FrameParams& fp) {
    ensureFifo(ctx_);
    fp_ = fp;
    mul4(fp.proj, fp.view, viewProj_);
    ringPos_ = 0;
    rsxInvalidateVertexCache(ctx_);
    setSurface(hdr_.offset, hdr_.pitch, GCM_SURFACE_F_W16Z16Y16X16, hdr_.w, hdr_.h);
    rsxSetClearDepthStencil(ctx_, 0xffffff00);
    rsxClearSurface(ctx_, GCM_CLEAR_Z | GCM_CLEAR_S);
    // Clear the FP16 color with a fullscreen triangle in the clear color
    // (works the same on every RSX revision and in emulators).
    QuadVertex q[3];
    const float pos[3][2] = {{0, 0}, {2.0f * hdr_.w, 0}, {0, 2.0f * hdr_.h}};
    uint8_t c[3];
    for (int i = 0; i < 3; ++i) { float v = fp.clearColor[i]; c[i] = (uint8_t)(v <= 0 ? 0 : v >= 1 ? 255 : v * 255.0f + 0.5f); }
    for (int i = 0; i < 3; ++i) q[i] = QuadVertex{pos[i][0], pos[i][1], 0, 0.5f, 0.5f, c[0], c[1], c[2], 255};
    inOverlay_ = true;                                     // pixel coordinates, no sRGB decode
    noBlend_ = true;
    drawTriangles(q, 3, whiteTex_, Blend::Alpha, false, false, 1.0f);
    noBlend_ = false;
    inOverlay_ = false;
    rsxSetDepthTestEnable(ctx_, GCM_TRUE);
    rsxSetDepthFunc(ctx_, GCM_LEQUAL);
}

void RSXRenderer::drawMesh(MeshId id, const float model[16], const MeshMaterial& m) {
    ensureFifo(ctx_);
    if (id == 0 || id > meshes_.size() || m.tex == 0 || m.tex > texs_.size()) return;
    const Mesh& me = meshes_[id - 1];
    useVP(vpMesh_);
    vpMatrix(vpMesh_, "uModel", model);
    vpMatrix(vpMesh_, "uViewProj", viewProj_);

    RingPtr fpc = bindFP(fpMesh_);
    fpConst(fpMesh_, fpc, "uTint", m.tint);
    fpConst(fpMesh_, fpc, "uEmissive", m.emissive);
    fpConst(fpMesh_, fpc, "uSpec", &m.specular);
    fpConst(fpMesh_, fpc, "uShin", &m.shininess);
    fpConst(fpMesh_, fpc, "uCam", fp_.camPos);
    fpConst(fpMesh_, fpc, "uLightDir", fp_.lightDir);
    fpConst(fpMesh_, fpc, "uLightCol", fp_.lightColor);
    fpConst(fpMesh_, fpc, "uSky", fp_.skyColor);
    fpConst(fpMesh_, fpc, "uGround", fp_.groundColor);
    bindTexture(fpMesh_, "uTex", texs_[m.tex - 1], true);
    rsxLoadFragmentProgramLocation(ctx_, fpMesh_.prog, fpc.offset, GCM_LOCATION_RSX);

    rsxSetCullFaceEnable(ctx_, GCM_TRUE);
    rsxSetCullFace(ctx_, GCM_CULL_BACK);
    rsxSetFrontFace(ctx_, kFrontFace);
    rsxSetDepthTestEnable(ctx_, GCM_TRUE);
    if (m.tint[3] < 1) {
        rsxSetBlendEnable(ctx_, GCM_TRUE);
        rsxSetBlendFunc(ctx_, GCM_SRC_ALPHA, GCM_ONE_MINUS_SRC_ALPHA, GCM_SRC_ALPHA, GCM_ONE_MINUS_SRC_ALPHA);
        rsxSetBlendEquation(ctx_, GCM_FUNC_ADD, GCM_FUNC_ADD);
        rsxSetDepthWriteEnable(ctx_, GCM_FALSE);
    } else {
        rsxSetBlendEnable(ctx_, GCM_FALSE);
        rsxSetDepthWriteEnable(ctx_, GCM_TRUE);
    }
    unbindAttribs(ctx_);
    rsxBindVertexArrayAttrib(ctx_, GCM_VERTEX_ATTRIB_POS, 0, me.vb, sizeof(MeshVertex), 3, GCM_VERTEX_DATA_TYPE_F32, GCM_LOCATION_RSX);
    rsxBindVertexArrayAttrib(ctx_, GCM_VERTEX_ATTRIB_NORMAL, 0, me.vb + 12, sizeof(MeshVertex), 3, GCM_VERTEX_DATA_TYPE_F32, GCM_LOCATION_RSX);
    rsxBindVertexArrayAttrib(ctx_, GCM_VERTEX_ATTRIB_TEX0, 0, me.vb + 24, sizeof(MeshVertex), 2, GCM_VERTEX_DATA_TYPE_F32, GCM_LOCATION_RSX);
    rsxDrawIndexArray(ctx_, GCM_TYPE_TRIANGLES, me.ib, me.count, GCM_INDEX_TYPE_16B, GCM_LOCATION_RSX);
    rsxSetCullFaceEnable(ctx_, GCM_FALSE);
}

void RSXRenderer::drawTriangles(const QuadVertex* v, int count, TexId tex, Blend blend, bool world, bool depthTest, float intensity) {
    ensureFifo(ctx_);
    if (count <= 0 || tex == 0 || tex > texs_.size()) return;
    float mvp[16];
    // overlay = display buffer; during the clear we are still on the HDR target (same size)
    int w = (int)dispW_, h = (int)dispH_;
    if (world && !inOverlay_) {
        std::memcpy(mvp, viewProj_, sizeof mvp);
    } else {
        std::memset(mvp, 0, sizeof mvp); // pixel coordinates, y down
        mvp[0] = 2.0f / w;
        mvp[5] = -2.0f / h;
        mvp[10] = -1;
        mvp[12] = -1;
        mvp[13] = 1;
        mvp[15] = 1;
    }
    RingPtr vb = ringAlloc(count * sizeof(QuadVertex), 128);
    std::memcpy(vb.ptr, v, count * sizeof(QuadVertex));

    useVP(vpSprite_);
    vpMatrix(vpSprite_, "uMVP", mvp);
    RingPtr fpc = bindFP(fpSprite_);
    float lin = inOverlay_ ? 0.0f : 1.0f;
    fpConst(fpSprite_, fpc, "uIntensity", &intensity);
    fpConst(fpSprite_, fpc, "uLinear", &lin);
    bindTexture(fpSprite_, "uTex", texs_[tex - 1], true);
    rsxLoadFragmentProgramLocation(ctx_, fpSprite_.prog, fpc.offset, GCM_LOCATION_RSX);

    rsxSetBlendEnable(ctx_, noBlend_ ? GCM_FALSE : GCM_TRUE);
    if (blend == Blend::Additive) rsxSetBlendFunc(ctx_, GCM_SRC_ALPHA, GCM_ONE, GCM_ZERO, GCM_ONE);
    else rsxSetBlendFunc(ctx_, GCM_SRC_ALPHA, GCM_ONE_MINUS_SRC_ALPHA, GCM_ONE, GCM_ONE_MINUS_SRC_ALPHA);
    rsxSetBlendEquation(ctx_, GCM_FUNC_ADD, GCM_FUNC_ADD);
    rsxSetDepthTestEnable(ctx_, (depthTest && world && !inOverlay_) ? GCM_TRUE : GCM_FALSE);
    rsxSetDepthWriteEnable(ctx_, GCM_FALSE);
    rsxSetCullFaceEnable(ctx_, GCM_FALSE);
    unbindAttribs(ctx_);
    rsxBindVertexArrayAttrib(ctx_, GCM_VERTEX_ATTRIB_POS, 0, vb.offset, sizeof(QuadVertex), 3, GCM_VERTEX_DATA_TYPE_F32, GCM_LOCATION_RSX);
    rsxBindVertexArrayAttrib(ctx_, GCM_VERTEX_ATTRIB_TEX0, 0, vb.offset + 12, sizeof(QuadVertex), 2, GCM_VERTEX_DATA_TYPE_F32, GCM_LOCATION_RSX);
    rsxBindVertexArrayAttrib(ctx_, GCM_VERTEX_ATTRIB_COLOR0, 0, vb.offset + 20, sizeof(QuadVertex), 4, GCM_VERTEX_DATA_TYPE_U8, GCM_LOCATION_RSX);
    rsxDrawVertexArray(ctx_, GCM_TYPE_TRIANGLES, 0, count);
}

void RSXRenderer::fullscreen(FProg& fp, u32 fpOff) {
    ensureFifo(ctx_);
    useVP(vpFull_);
    rsxLoadFragmentProgramLocation(ctx_, fp.prog, fpOff, GCM_LOCATION_RSX);
    unbindAttribs(ctx_);
    rsxBindVertexArrayAttrib(ctx_, GCM_VERTEX_ATTRIB_POS, 0, fullVB_, 8, 2, GCM_VERTEX_DATA_TYPE_F32, GCM_LOCATION_RSX);
    rsxDrawVertexArray(ctx_, GCM_TYPE_TRIANGLES, 0, 3);
}

void RSXRenderer::beginOverlay() {
    ensureFifo(ctx_);
    rsxSetDepthTestEnable(ctx_, GCM_FALSE);
    rsxSetDepthWriteEnable(ctx_, GCM_FALSE);
    rsxSetBlendEnable(ctx_, GCM_FALSE);
    rsxSetCullFaceEnable(ctx_, GCM_FALSE);

    // Bloom: bright pass -> downsample chain -> additive upsample chain (same as the GL renderer)
    rsxInvalidateTextureCache(ctx_, GCM_INVALIDATE_TEXTURE);
    setSurface(bloom_[0].offset, bloom_[0].pitch, GCM_SURFACE_F_W16Z16Y16X16, bloom_[0].w, bloom_[0].h);
    {
        RingPtr fpc = bindFP(fpBright_);
        float hp[2] = {0.5f / hdr_.w, 0.5f / hdr_.h};
        fpConst(fpBright_, fpc, "uHalfPixel", hp);
        fpConst(fpBright_, fpc, "uThreshold", &fp_.bloomThreshold);
        bindTexture(fpBright_, "uSrc", targetTex(hdr_), true);
        fullscreen(fpBright_, fpc.offset);
    }
    for (int i = 0; i + 1 < kBloomLevels; ++i) {
        rsxInvalidateTextureCache(ctx_, GCM_INVALIDATE_TEXTURE);
        setSurface(bloom_[i + 1].offset, bloom_[i + 1].pitch, GCM_SURFACE_F_W16Z16Y16X16, bloom_[i + 1].w, bloom_[i + 1].h);
        RingPtr fpc = bindFP(fpDown_);
        float hp[2] = {0.5f / bloom_[i].w, 0.5f / bloom_[i].h};
        fpConst(fpDown_, fpc, "uHalfPixel", hp);
        bindTexture(fpDown_, "uSrc", targetTex(bloom_[i]), true);
        fullscreen(fpDown_, fpc.offset);
    }
    rsxSetBlendEnable(ctx_, GCM_TRUE);
    rsxSetBlendFunc(ctx_, GCM_ONE, GCM_ONE, GCM_ONE, GCM_ONE);
    rsxSetBlendEquation(ctx_, GCM_FUNC_ADD, GCM_FUNC_ADD);
    for (int i = kBloomLevels - 2; i >= 0; --i) {
        rsxInvalidateTextureCache(ctx_, GCM_INVALIDATE_TEXTURE);
        setSurface(bloom_[i].offset, bloom_[i].pitch, GCM_SURFACE_F_W16Z16Y16X16, bloom_[i].w, bloom_[i].h);
        RingPtr fpc = bindFP(fpUp_);
        float hp[2] = {0.5f / bloom_[i + 1].w, 0.5f / bloom_[i + 1].h};
        fpConst(fpUp_, fpc, "uHalfPixel", hp);
        bindTexture(fpUp_, "uSrc", targetTex(bloom_[i + 1]), true);
        fullscreen(fpUp_, fpc.offset);
    }
    rsxSetBlendEnable(ctx_, GCM_FALSE);

    // Composite into the display buffer
    rsxInvalidateTextureCache(ctx_, GCM_INVALIDATE_TEXTURE);
    setSurface(colorOffset_[curFb_], colorPitch_, GCM_SURFACE_X8R8G8B8, dispW_, dispH_);
    {
        RingPtr fpc = bindFP(fpComposite_);
        float strength = fp_.bloomStrength * 0.35f;
        fpConst(fpComposite_, fpc, "uBloomStrength", &strength);
        fpConst(fpComposite_, fpc, "uExposure", &fp_.exposure);
        fpConst(fpComposite_, fpc, "uVignette", &fp_.vignette);
        bindTexture(fpComposite_, "uHdr", targetTex(hdr_), true);
        bindTexture(fpComposite_, "uBloom", targetTex(bloom_[0]), true);
        fullscreen(fpComposite_, fpc.offset);
    }
    inOverlay_ = true;
}

void RSXRenderer::endFrame() {
    inOverlay_ = false;
    flip();
}

void RSXRenderer::flip() {
    ensureFifo(ctx_);
    if (!firstFlip_) {
        while (gcmGetFlipStatus() != 0) usleep(200); // previous flip done -> its frame is finished
    }
    gcmResetFlipStatus();
    gcmSetFlip(ctx_, curFb_);
    rsxFlushBuffer(ctx_);
    gcmSetWaitFlip(ctx_);
    curFb_ ^= 1;
    ringHalf_ ^= 1;
    firstFlip_ = false;
}

bool RSXRenderer::readPixels(std::vector<uint8_t>& rgb, int& w, int& h) {
    ensureFifo(ctx_);
    waitIdle(ctx_);
    w = (int)dispW_; h = (int)dispH_;
    const uint8_t* src = (const uint8_t*)colorBuf_[curFb_ ^ 1]; // the frame just flipped
    rgb.resize((size_t)w * h * 3);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const uint8_t* p = src + (size_t)y * colorPitch_ + x * 4; // X R G B
            uint8_t* d = &rgb[((size_t)y * w + x) * 3];
            d[0] = p[1]; d[1] = p[2]; d[2] = p[3];
        }
    return true;
}

} // namespace hx
