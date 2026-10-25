#include <algorithm>
#include "gl_renderer.h"
#include "../../game/log.h"
#include <cstdio>
#include <cstring>

#define HX_DEFINE(type, name) type hx_##name = nullptr;
HX_GL_FUNCS(HX_DEFINE)
#undef HX_DEFINE

bool hxLoadGL() {
    bool ok = true;
#define HX_LOAD(type, name)                                                  \
    hx_##name = (type)SDL_GL_GetProcAddress(#name);                          \
    if (!hx_##name) { hx::logMsg("GL: missing %s", #name); ok = false; }
    HX_GL_FUNCS(HX_LOAD)
#undef HX_LOAD
    return ok;
}

#ifndef GL_TEXTURE_MAX_ANISOTROPY
#define GL_TEXTURE_MAX_ANISOTROPY 0x84FE
#endif

namespace hx {

namespace {

// Same shaders for desktop GL 3.3 and OpenGL ES 3.0 (Android); only the header differs.
const char* kHeaderGL = "#version 330 core\n";
const char* kHeaderES = "#version 300 es\nprecision highp float;\nprecision highp int;\n";

const char* kMeshVS = R"(
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNrm;
layout(location=2) in vec2 aUV;
uniform mat4 uModel;
uniform mat4 uViewProj;
out vec3 vWorld;
out vec3 vN;
out vec2 vUV;
void main() {
    vec4 w = uModel * vec4(aPos, 1.0);
    vWorld = w.xyz;
    vN = mat3(uModel) * aNrm;
    vUV = aUV;
    gl_Position = uViewProj * w;
}
)";

const char* kMeshFS = R"(
in vec3 vWorld;
in vec3 vN;
in vec2 vUV;
uniform sampler2D uTex;
uniform vec4 uTint;
uniform vec3 uEmissive;
uniform float uSpec;
uniform float uShin;
uniform vec3 uCam;
uniform vec3 uLightDir;
uniform vec3 uLightCol;
uniform vec3 uSky;
uniform vec3 uGround;
out vec4 frag;
void main() {
    vec4 t = texture(uTex, vUV);
    vec3 alb = pow(t.rgb, vec3(2.2)) * uTint.rgb;
    vec3 N = normalize(vN);
    vec3 V = normalize(uCam - vWorld);
    vec3 L = normalize(uLightDir);
    float nl = max(dot(N, L), 0.0);
    // fill light from the viewer side keeps the front faces readable
    vec3 F = normalize(vec3(0.35, -0.15, 1.0));
    float nf = max(dot(N, F), 0.0) * 0.45;
    vec3 amb = mix(uGround, uSky, N.y * 0.5 + 0.5);
    vec3 H = normalize(L + V);
    float sp = pow(max(dot(N, H), 0.0), uShin) * uSpec * step(0.0, nl);
    float fres = pow(1.0 - max(dot(N, V), 0.0), 5.0);
    float lum = dot(alb, vec3(0.3, 0.55, 0.15));
    vec3 col = alb * (amb + nl * uLightCol + nf * uLightCol) + sp * uLightCol + fres * uSky * 0.5
             + uEmissive * smoothstep(0.02, 0.35, lum);
    frag = vec4(col, uTint.a);
}
)";

const char* kSpriteVS = R"(
layout(location=0) in vec3 aPos;
layout(location=1) in vec2 aUV;
layout(location=2) in vec4 aCol;
uniform mat4 uMVP;
out vec2 vUV;
out vec4 vCol;
void main() {
    vUV = aUV;
    vCol = aCol;
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)";

const char* kSpriteFS = R"(
in vec2 vUV;
in vec4 vCol;
uniform sampler2D uTex;
uniform float uIntensity;
uniform float uLinear;
out vec4 frag;
void main() {
    vec4 c = texture(uTex, vUV) * vCol;
    if (uLinear > 0.5) c.rgb = pow(c.rgb, vec3(2.2));
    c.rgb *= uIntensity;
    frag = c;
}
)";

const char* kFullVS = R"(
out vec2 vUV;
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vUV = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

const char* kBrightFS = R"(
in vec2 vUV;
uniform sampler2D uSrc;
uniform vec2 uHalfPixel;
uniform float uThreshold;
out vec4 frag;
void main() {
    vec3 c = texture(uSrc, vUV + vec2(-uHalfPixel.x, -uHalfPixel.y)).rgb
           + texture(uSrc, vUV + vec2( uHalfPixel.x, -uHalfPixel.y)).rgb
           + texture(uSrc, vUV + vec2(-uHalfPixel.x,  uHalfPixel.y)).rgb
           + texture(uSrc, vUV + vec2( uHalfPixel.x,  uHalfPixel.y)).rgb;
    c *= 0.25;
    c = min(c, vec3(40.0));
    float br = max(c.r, max(c.g, c.b));
    float soft = clamp(br - uThreshold + 0.5, 0.0, 1.0);
    soft = soft * soft * 0.5;
    float contrib = max(soft, br - uThreshold) / max(br, 1e-4);
    frag = vec4(c * contrib, 1.0);
}
)";

const char* kDownFS = R"(
in vec2 vUV;
uniform sampler2D uSrc;
uniform vec2 uHalfPixel;
out vec4 frag;
void main() {
    vec3 s = texture(uSrc, vUV).rgb * 4.0;
    s += texture(uSrc, vUV - uHalfPixel).rgb;
    s += texture(uSrc, vUV + uHalfPixel).rgb;
    s += texture(uSrc, vUV + vec2(uHalfPixel.x, -uHalfPixel.y)).rgb;
    s += texture(uSrc, vUV - vec2(uHalfPixel.x, -uHalfPixel.y)).rgb;
    frag = vec4(s / 8.0, 1.0);
}
)";

const char* kUpFS = R"(
in vec2 vUV;
uniform sampler2D uSrc;
uniform vec2 uHalfPixel;
out vec4 frag;
void main() {
    vec2 h = uHalfPixel;
    vec3 s = texture(uSrc, vUV + vec2(-h.x * 2.0, 0.0)).rgb;
    s += texture(uSrc, vUV + vec2(-h.x, h.y)).rgb * 2.0;
    s += texture(uSrc, vUV + vec2(0.0, h.y * 2.0)).rgb;
    s += texture(uSrc, vUV + vec2(h.x, h.y)).rgb * 2.0;
    s += texture(uSrc, vUV + vec2(h.x * 2.0, 0.0)).rgb;
    s += texture(uSrc, vUV + vec2(h.x, -h.y)).rgb * 2.0;
    s += texture(uSrc, vUV + vec2(0.0, -h.y * 2.0)).rgb;
    s += texture(uSrc, vUV + vec2(-h.x, -h.y)).rgb * 2.0;
    frag = vec4(s / 12.0, 1.0);
}
)";

const char* kCompositeFS = R"(
in vec2 vUV;
uniform sampler2D uHdr;
uniform sampler2D uBloom;
uniform float uBloomStrength;
uniform float uExposure;
uniform float uVignette;
out vec4 frag;
vec3 aces(vec3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}
void main() {
    vec3 c = texture(uHdr, vUV).rgb + texture(uBloom, vUV).rgb * uBloomStrength;
    c = aces(c * uExposure);
    c = pow(c, vec3(1.0 / 2.2));
    vec2 d = vUV - 0.5;
    c *= 1.0 - uVignette * pow(dot(d, d) * 2.0, 1.3);
    float n = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453);
    c += (n - 0.5) / 255.0;
    frag = vec4(c, 1.0);
}
)";

void mul4(const float* a, const float* b, float* out) { // out = a * b (column-major)
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r) {
            float s = 0;
            for (int k = 0; k < 4; ++k) s += a[k * 4 + r] * b[c * 4 + k];
            out[c * 4 + r] = s;
        }
}

} // namespace

GLuint GLRenderer::compile(const char* vs, const char* fs) {
    const char* header = gles_ ? kHeaderES : kHeaderGL;
    auto sh = [header](GLenum type, const char* src) {
        GLuint s = glCreateShader(type);
        const char* parts[2] = {header, src};
        glShaderSource(s, 2, parts, nullptr);
        glCompileShader(s);
        GLint ok = 0;
        glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[2048];
            glGetShaderInfoLog(s, sizeof log, nullptr, log);
            logMsg("shader error: %s", log);
        }
        return s;
    };
    GLuint p = glCreateProgram();
    glAttachShader(p, sh(GL_VERTEX_SHADER, vs));
    glAttachShader(p, sh(GL_FRAGMENT_SHADER, fs));
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetProgramInfoLog(p, sizeof log, nullptr, log);
        logMsg("link error: %s", log);
    }
    return p;
}

bool GLRenderer::init(int msaa, bool gles) {
    gles_ = gles;
    GLint maxSamples = 0;
    glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
    msaa_ = std::max(0, std::min(msaa, (int)maxSamples));
    // HDR targets need float render support on GLES (desktop GL 3.3 always has it).
    hdrFormat_ = GL_RGBA16F;
    if (gles_) {
        bool half = false;
        GLint n = 0;
        glGetIntegerv(GL_NUM_EXTENSIONS, &n);
        for (GLint i = 0; i < n; ++i) {
            const char* e = (const char*)glGetStringi(GL_EXTENSIONS, (GLuint)i);
            if (e && (!std::strcmp(e, "GL_EXT_color_buffer_half_float") || !std::strcmp(e, "GL_EXT_color_buffer_float"))) half = true;
        }
        if (!half) hdrFormat_ = GL_RGBA8;
    }
    logMsg("GL: %s, MSAA %dx, %s targets", gles_ ? "ES 3.0" : "3.3 core", msaa_,
                 hdrFormat_ == GL_RGBA16F ? "half float" : "8-bit");
    progMesh_ = compile(kMeshVS, kMeshFS);
    progSprite_ = compile(kSpriteVS, kSpriteFS);
    progBright_ = compile(kFullVS, kBrightFS);
    progDown_ = compile(kFullVS, kDownFS);
    progUp_ = compile(kFullVS, kUpFS);
    progComposite_ = compile(kFullVS, kCompositeFS);

    glGenVertexArrays(1, &spriteVao_);
    glBindVertexArray(spriteVao_);
    glGenBuffers(1, &spriteVbo_);
    glBindBuffer(GL_ARRAY_BUFFER, spriteVbo_);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(QuadVertex), (void*)0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(QuadVertex), (void*)12);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(QuadVertex), (void*)20);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glGenVertexArrays(1, &quadVao_);
    glBindVertexArray(0);
    return progMesh_ && progSprite_;
}

TexId GLRenderer::createTexture(int w, int h, const uint8_t* rgba, bool mipmaps, bool repeat) {
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    GLint wrap = repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    if (mipmaps) {
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY, 8.0f);
        glGetError(); // anisotropy may be unsupported; ignore
    } else {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    }
    return t;
}

void GLRenderer::destroyTexture(TexId t) {
    GLuint id = t;
    glDeleteTextures(1, &id);
}

MeshId GLRenderer::createMesh(const MeshVertex* v, int nv, const uint16_t* idx, int ni) {
    Mesh m;
    glGenVertexArrays(1, &m.vao);
    glBindVertexArray(m.vao);
    glGenBuffers(1, &m.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    glBufferData(GL_ARRAY_BUFFER, nv * sizeof(MeshVertex), v, GL_STATIC_DRAW);
    glGenBuffers(1, &m.ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, ni * sizeof(uint16_t), idx, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), (void*)0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), (void*)12);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), (void*)24);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glBindVertexArray(0);
    m.count = ni;
    meshes_.push_back(m);
    return (MeshId)meshes_.size(); // 1-based
}

void GLRenderer::makeTarget(Target& t, int w, int h) {
    t.w = w; t.h = h;
    glGenTextures(1, &t.tex);
    glBindTexture(GL_TEXTURE_2D, t.tex);
    if (hdrFormat_ == GL_RGBA16F) glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
    else glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, &t.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.tex, 0);
}

void GLRenderer::freeTarget(Target& t) {
    if (t.fbo) glDeleteFramebuffers(1, &t.fbo);
    if (t.tex) glDeleteTextures(1, &t.tex);
    t = Target{};
}

void GLRenderer::ensureTargets() {
    if (tw_ == width_ && th_ == height_ && msFbo_) return;
    tw_ = width_; th_ = height_;
    if (msFbo_) {
        glDeleteFramebuffers(1, &msFbo_);
        glDeleteRenderbuffers(1, &msColor_);
        glDeleteRenderbuffers(1, &msDepth_);
    }
    glGenRenderbuffers(1, &msColor_);
    glBindRenderbuffer(GL_RENDERBUFFER, msColor_);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, msaa_, hdrFormat_, tw_, th_);
    glGenRenderbuffers(1, &msDepth_);
    glBindRenderbuffer(GL_RENDERBUFFER, msDepth_);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, msaa_, GL_DEPTH_COMPONENT24, tw_, th_);
    glGenFramebuffers(1, &msFbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, msFbo_);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, msColor_);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, msDepth_);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        logMsg("GL: MSAA framebuffer incomplete");

    freeTarget(hdr_);
    makeTarget(hdr_, tw_, th_);
    for (int i = 0; i < kBloomLevels; ++i) {
        freeTarget(bloom_[i]);
        int w = tw_ >> (i + 1), h = th_ >> (i + 1);
        makeTarget(bloom_[i], w < 1 ? 1 : w, h < 1 ? 1 : h);
    }
}

void GLRenderer::beginFrame(const FrameParams& fp) {
    fp_ = fp;
    ensureTargets();
    mul4(fp.proj, fp.view, viewProj_);
    glBindFramebuffer(GL_FRAMEBUFFER, msFbo_);
    glViewport(0, 0, tw_, th_);
    glClearColor(fp.clearColor[0], fp.clearColor[1], fp.clearColor[2], 1);
    glDepthMask(GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    inOverlay_ = false;
}

void GLRenderer::drawMesh(MeshId id, const float model[16], const MeshMaterial& m) {
    if (id == 0 || id > meshes_.size()) return;
    const Mesh& me = meshes_[id - 1];
    glUseProgram(progMesh_);
    glUniformMatrix4fv(glGetUniformLocation(progMesh_, "uModel"), 1, GL_FALSE, model);
    glUniformMatrix4fv(glGetUniformLocation(progMesh_, "uViewProj"), 1, GL_FALSE, viewProj_);
    glUniform4fv(glGetUniformLocation(progMesh_, "uTint"), 1, m.tint);
    glUniform3fv(glGetUniformLocation(progMesh_, "uEmissive"), 1, m.emissive);
    glUniform1f(glGetUniformLocation(progMesh_, "uSpec"), m.specular);
    glUniform1f(glGetUniformLocation(progMesh_, "uShin"), m.shininess);
    glUniform3fv(glGetUniformLocation(progMesh_, "uCam"), 1, fp_.camPos);
    glUniform3fv(glGetUniformLocation(progMesh_, "uLightDir"), 1, fp_.lightDir);
    glUniform3fv(glGetUniformLocation(progMesh_, "uLightCol"), 1, fp_.lightColor);
    glUniform3fv(glGetUniformLocation(progMesh_, "uSky"), 1, fp_.skyColor);
    glUniform3fv(glGetUniformLocation(progMesh_, "uGround"), 1, fp_.groundColor);
    glUniform1i(glGetUniformLocation(progMesh_, "uTex"), 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m.tex);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    glEnable(GL_DEPTH_TEST);
    if (m.tint[3] < 1) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
    } else {
        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
    }
    glBindVertexArray(me.vao);
    glDrawElements(GL_TRIANGLES, me.count, GL_UNSIGNED_SHORT, nullptr);
    glBindVertexArray(0);
    glDisable(GL_CULL_FACE);
}

void GLRenderer::drawTriangles(const QuadVertex* v, int count, TexId tex, Blend blend, bool world, bool depthTest, float intensity) {
    if (count <= 0) return;
    float mvp[16];
    if (world && !inOverlay_) {
        std::memcpy(mvp, viewProj_, sizeof mvp);
    } else {
        // pixel coordinates, y down
        std::memset(mvp, 0, sizeof mvp);
        mvp[0] = 2.0f / width_;
        mvp[5] = -2.0f / height_;
        mvp[10] = -1;
        mvp[12] = -1;
        mvp[13] = 1;
        mvp[15] = 1;
    }
    glUseProgram(progSprite_);
    glUniformMatrix4fv(glGetUniformLocation(progSprite_, "uMVP"), 1, GL_FALSE, mvp);
    glUniform1f(glGetUniformLocation(progSprite_, "uIntensity"), intensity);
    glUniform1f(glGetUniformLocation(progSprite_, "uLinear"), inOverlay_ ? 0.0f : 1.0f);
    glUniform1i(glGetUniformLocation(progSprite_, "uTex"), 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glEnable(GL_BLEND);
    if (blend == Blend::Additive) glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ZERO, GL_ONE);
    else glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    if (depthTest && world && !inOverlay_) glEnable(GL_DEPTH_TEST);
    else glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glBindVertexArray(spriteVao_);
    glBindBuffer(GL_ARRAY_BUFFER, spriteVbo_);
    glBufferData(GL_ARRAY_BUFFER, count * sizeof(QuadVertex), v, GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, count);
    glBindVertexArray(0);
}

void GLRenderer::fullscreenPass(GLuint prog, GLuint tex, Target* dst, int w, int h) {
    glBindFramebuffer(GL_FRAMEBUFFER, dst ? dst->fbo : 0);
    glViewport(0, 0, w, h);
    glUseProgram(prog);
    glUniform1i(glGetUniformLocation(prog, "uSrc"), 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glBindVertexArray(quadVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}

void GLRenderer::beginOverlay() {
    // Resolve MSAA
    glBindFramebuffer(GL_READ_FRAMEBUFFER, msFbo_);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, hdr_.fbo);
    glBlitFramebuffer(0, 0, tw_, th_, 0, 0, tw_, th_, GL_COLOR_BUFFER_BIT, GL_NEAREST);

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);

    // Bloom: bright pass -> downsample chain -> additive upsample chain
    glUseProgram(progBright_);
    glUniform2f(glGetUniformLocation(progBright_, "uHalfPixel"), 0.5f / tw_, 0.5f / th_);
    glUniform1f(glGetUniformLocation(progBright_, "uThreshold"), fp_.bloomThreshold);
    fullscreenPass(progBright_, hdr_.tex, &bloom_[0], bloom_[0].w, bloom_[0].h);
    for (int i = 0; i + 1 < kBloomLevels; ++i) {
        glUseProgram(progDown_);
        glUniform2f(glGetUniformLocation(progDown_, "uHalfPixel"), 0.5f / bloom_[i].w, 0.5f / bloom_[i].h);
        fullscreenPass(progDown_, bloom_[i].tex, &bloom_[i + 1], bloom_[i + 1].w, bloom_[i + 1].h);
    }
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    for (int i = kBloomLevels - 2; i >= 0; --i) {
        glUseProgram(progUp_);
        glUniform2f(glGetUniformLocation(progUp_, "uHalfPixel"), 0.5f / bloom_[i + 1].w, 0.5f / bloom_[i + 1].h);
        fullscreenPass(progUp_, bloom_[i + 1].tex, &bloom_[i], bloom_[i].w, bloom_[i].h);
    }
    glDisable(GL_BLEND);

    // Composite to the window
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width_, height_);
    glUseProgram(progComposite_);
    glUniform1i(glGetUniformLocation(progComposite_, "uHdr"), 0);
    glUniform1i(glGetUniformLocation(progComposite_, "uBloom"), 1);
    glUniform1f(glGetUniformLocation(progComposite_, "uBloomStrength"), fp_.bloomStrength * 0.35f);
    glUniform1f(glGetUniformLocation(progComposite_, "uExposure"), fp_.exposure);
    glUniform1f(glGetUniformLocation(progComposite_, "uVignette"), fp_.vignette);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, bloom_[0].tex);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, hdr_.tex);
    glBindVertexArray(quadVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);

    inOverlay_ = true;
}

void GLRenderer::endFrame() {
    inOverlay_ = false;
}

bool GLRenderer::readPixels(std::vector<uint8_t>& rgb, int& w, int& h) {
    w = width_; h = height_;
    std::vector<uint8_t> tmp((size_t)w * h * 4);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glReadBuffer(GL_BACK);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, tmp.data()); // RGBA works on GL and GLES
    rgb.resize((size_t)w * h * 3);
    for (int y = 0; y < h; ++y) {
        const uint8_t* src = &tmp[(size_t)(h - 1 - y) * w * 4];
        uint8_t* dst = &rgb[(size_t)y * w * 3];
        for (int x = 0; x < w; ++x) { dst[x * 3] = src[x * 4]; dst[x * 3 + 1] = src[x * 4 + 1]; dst[x * 3 + 2] = src[x * 4 + 2]; }
    }
    return true;
}

} // namespace hx
