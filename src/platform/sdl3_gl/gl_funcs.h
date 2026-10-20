// OpenGL function loading through SDL (no GLEW/glad dependency).
#pragma once
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#define HX_GL_FUNCS(X)                                                        \
    X(PFNGLGENBUFFERSPROC, glGenBuffers)                                      \
    X(PFNGLBINDBUFFERPROC, glBindBuffer)                                      \
    X(PFNGLBUFFERDATAPROC, glBufferData)                                      \
    X(PFNGLGENVERTEXARRAYSPROC, glGenVertexArrays)                            \
    X(PFNGLBINDVERTEXARRAYPROC, glBindVertexArray)                            \
    X(PFNGLVERTEXATTRIBPOINTERPROC, glVertexAttribPointer)                    \
    X(PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray)            \
    X(PFNGLCREATESHADERPROC, glCreateShader)                                  \
    X(PFNGLSHADERSOURCEPROC, glShaderSource)                                  \
    X(PFNGLCOMPILESHADERPROC, glCompileShader)                                \
    X(PFNGLGETSHADERIVPROC, glGetShaderiv)                                    \
    X(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog)                          \
    X(PFNGLCREATEPROGRAMPROC, glCreateProgram)                                \
    X(PFNGLATTACHSHADERPROC, glAttachShader)                                  \
    X(PFNGLLINKPROGRAMPROC, glLinkProgram)                                    \
    X(PFNGLGETPROGRAMIVPROC, glGetProgramiv)                                  \
    X(PFNGLGETPROGRAMINFOLOGPROC, glGetProgramInfoLog)                        \
    X(PFNGLUSEPROGRAMPROC, glUseProgram)                                      \
    X(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation)                      \
    X(PFNGLUNIFORM1IPROC, glUniform1i)                                        \
    X(PFNGLUNIFORM1FPROC, glUniform1f)                                        \
    X(PFNGLUNIFORM2FPROC, glUniform2f)                                        \
    X(PFNGLUNIFORM3FVPROC, glUniform3fv)                                      \
    X(PFNGLUNIFORM4FVPROC, glUniform4fv)                                      \
    X(PFNGLUNIFORMMATRIX4FVPROC, glUniformMatrix4fv)                          \
    X(PFNGLGENERATEMIPMAPPROC, glGenerateMipmap)                              \
    X(PFNGLGENFRAMEBUFFERSPROC, glGenFramebuffers)                            \
    X(PFNGLDELETEFRAMEBUFFERSPROC, glDeleteFramebuffers)                      \
    X(PFNGLBINDFRAMEBUFFERPROC, glBindFramebuffer)                            \
    X(PFNGLFRAMEBUFFERTEXTURE2DPROC, glFramebufferTexture2D)                  \
    X(PFNGLFRAMEBUFFERRENDERBUFFERPROC, glFramebufferRenderbuffer)            \
    X(PFNGLGENRENDERBUFFERSPROC, glGenRenderbuffers)                          \
    X(PFNGLDELETERENDERBUFFERSPROC, glDeleteRenderbuffers)                    \
    X(PFNGLBINDRENDERBUFFERPROC, glBindRenderbuffer)                          \
    X(PFNGLRENDERBUFFERSTORAGEMULTISAMPLEPROC, glRenderbufferStorageMultisample) \
    X(PFNGLCHECKFRAMEBUFFERSTATUSPROC, glCheckFramebufferStatus)              \
    X(PFNGLBLITFRAMEBUFFERPROC, glBlitFramebuffer)                            \
    X(PFNGLACTIVETEXTUREPROC, glActiveTexture)                                \
    X(PFNGLBLENDFUNCSEPARATEPROC, glBlendFuncSeparate)

#define HX_DECLARE(type, name) extern type hx_##name;
HX_GL_FUNCS(HX_DECLARE)
#undef HX_DECLARE

#define glGenBuffers hx_glGenBuffers
#define glBindBuffer hx_glBindBuffer
#define glBufferData hx_glBufferData
#define glGenVertexArrays hx_glGenVertexArrays
#define glBindVertexArray hx_glBindVertexArray
#define glVertexAttribPointer hx_glVertexAttribPointer
#define glEnableVertexAttribArray hx_glEnableVertexAttribArray
#define glCreateShader hx_glCreateShader
#define glShaderSource hx_glShaderSource
#define glCompileShader hx_glCompileShader
#define glGetShaderiv hx_glGetShaderiv
#define glGetShaderInfoLog hx_glGetShaderInfoLog
#define glCreateProgram hx_glCreateProgram
#define glAttachShader hx_glAttachShader
#define glLinkProgram hx_glLinkProgram
#define glGetProgramiv hx_glGetProgramiv
#define glGetProgramInfoLog hx_glGetProgramInfoLog
#define glUseProgram hx_glUseProgram
#define glGetUniformLocation hx_glGetUniformLocation
#define glUniform1i hx_glUniform1i
#define glUniform1f hx_glUniform1f
#define glUniform2f hx_glUniform2f
#define glUniform3fv hx_glUniform3fv
#define glUniform4fv hx_glUniform4fv
#define glUniformMatrix4fv hx_glUniformMatrix4fv
#define glGenerateMipmap hx_glGenerateMipmap
#define glGenFramebuffers hx_glGenFramebuffers
#define glDeleteFramebuffers hx_glDeleteFramebuffers
#define glBindFramebuffer hx_glBindFramebuffer
#define glFramebufferTexture2D hx_glFramebufferTexture2D
#define glFramebufferRenderbuffer hx_glFramebufferRenderbuffer
#define glGenRenderbuffers hx_glGenRenderbuffers
#define glDeleteRenderbuffers hx_glDeleteRenderbuffers
#define glBindRenderbuffer hx_glBindRenderbuffer
#define glRenderbufferStorageMultisample hx_glRenderbufferStorageMultisample
#define glCheckFramebufferStatus hx_glCheckFramebufferStatus
#define glBlitFramebuffer hx_glBlitFramebuffer
#define glActiveTexture hx_glActiveTexture
#define glBlendFuncSeparate hx_glBlendFuncSeparate

bool hxLoadGL();
