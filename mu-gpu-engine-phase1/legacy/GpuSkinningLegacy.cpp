#include "GpuSkinningLegacy.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>

#include <unordered_map>
#include <string>
#include <vector>
#include <cstdint>
#include <cstring>
#include <cmath>

#ifndef APIENTRY
#define APIENTRY __stdcall
#endif
#ifndef GL_ARRAY_BUFFER
#define GL_ARRAY_BUFFER 0x8892
#endif
#ifndef GL_STATIC_DRAW
#define GL_STATIC_DRAW 0x88E4
#endif
#ifndef GL_VERTEX_SHADER
#define GL_VERTEX_SHADER 0x8B31
#endif
#ifndef GL_FRAGMENT_SHADER
#define GL_FRAGMENT_SHADER 0x8B30
#endif
#ifndef GL_COMPILE_STATUS
#define GL_COMPILE_STATUS 0x8B81
#endif
#ifndef GL_LINK_STATUS
#define GL_LINK_STATUS 0x8B82
#endif
#ifndef GL_INFO_LOG_LENGTH
#define GL_INFO_LOG_LENGTH 0x8B84
#endif
#ifndef GL_TEXTURE0
#define GL_TEXTURE0 0x84C0
#endif
#ifndef GL_TEXTURE1
#define GL_TEXTURE1 0x84C1
#endif
#ifndef GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS
#define GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS 0x8B4C
#endif
#ifndef GL_RGBA32F_ARB
#define GL_RGBA32F_ARB 0x8814
#endif
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

namespace
{
    typedef GLuint (APIENTRY* PFNGLCREATESHADERPROC)(GLenum);
    typedef void (APIENTRY* PFNGLSHADERSOURCEPROC)(GLuint, GLsizei, const char* const*, const GLint*);
    typedef void (APIENTRY* PFNGLCOMPILESHADERPROC)(GLuint);
    typedef void (APIENTRY* PFNGLGETSHADERIVPROC)(GLuint, GLenum, GLint*);
    typedef void (APIENTRY* PFNGLGETSHADERINFOLOGPROC)(GLuint, GLsizei, GLsizei*, char*);
    typedef void (APIENTRY* PFNGLDELETESHADERPROC)(GLuint);
    typedef GLuint (APIENTRY* PFNGLCREATEPROGRAMPROC)();
    typedef void (APIENTRY* PFNGLATTACHSHADERPROC)(GLuint, GLuint);
    typedef void (APIENTRY* PFNGLBINDATTRIBLOCATIONPROC)(GLuint, GLuint, const char*);
    typedef void (APIENTRY* PFNGLLINKPROGRAMPROC)(GLuint);
    typedef void (APIENTRY* PFNGLGETPROGRAMIVPROC)(GLuint, GLenum, GLint*);
    typedef void (APIENTRY* PFNGLGETPROGRAMINFOLOGPROC)(GLuint, GLsizei, GLsizei*, char*);
    typedef void (APIENTRY* PFNGLUSEPROGRAMPROC)(GLuint);
    typedef void (APIENTRY* PFNGLDELETEPROGRAMPROC)(GLuint);
    typedef GLint (APIENTRY* PFNGLGETUNIFORMLOCATIONPROC)(GLuint, const char*);
    typedef void (APIENTRY* PFNGLUNIFORM1IPROC)(GLint, GLint);
    typedef void (APIENTRY* PFNGLUNIFORM1FPROC)(GLint, GLfloat);
    typedef void (APIENTRY* PFNGLUNIFORM2FPROC)(GLint, GLfloat, GLfloat);
    typedef void (APIENTRY* PFNGLUNIFORM3FPROC)(GLint, GLfloat, GLfloat, GLfloat);
    typedef void (APIENTRY* PFNGLGENBUFFERSPROC)(GLsizei, GLuint*);
    typedef void (APIENTRY* PFNGLBINDBUFFERPROC)(GLenum, GLuint);
    typedef void (APIENTRY* PFNGLBUFFERDATAPROC)(GLenum, std::ptrdiff_t, const void*, GLenum);
    typedef void (APIENTRY* PFNGLDELETEBUFFERSPROC)(GLsizei, const GLuint*);
    typedef void (APIENTRY* PFNGLENABLEVERTEXATTRIBARRAYPROC)(GLuint);
    typedef void (APIENTRY* PFNGLDISABLEVERTEXATTRIBARRAYPROC)(GLuint);
    typedef void (APIENTRY* PFNGLVERTEXATTRIBPOINTERPROC)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
    typedef void (APIENTRY* PFNGLACTIVETEXTUREPROC)(GLenum);

    PFNGLCREATESHADERPROC glCreateShader_ = nullptr;
    PFNGLSHADERSOURCEPROC glShaderSource_ = nullptr;
    PFNGLCOMPILESHADERPROC glCompileShader_ = nullptr;
    PFNGLGETSHADERIVPROC glGetShaderiv_ = nullptr;
    PFNGLGETSHADERINFOLOGPROC glGetShaderInfoLog_ = nullptr;
    PFNGLDELETESHADERPROC glDeleteShader_ = nullptr;
    PFNGLCREATEPROGRAMPROC glCreateProgram_ = nullptr;
    PFNGLATTACHSHADERPROC glAttachShader_ = nullptr;
    PFNGLBINDATTRIBLOCATIONPROC glBindAttribLocation_ = nullptr;
    PFNGLLINKPROGRAMPROC glLinkProgram_ = nullptr;
    PFNGLGETPROGRAMIVPROC glGetProgramiv_ = nullptr;
    PFNGLGETPROGRAMINFOLOGPROC glGetProgramInfoLog_ = nullptr;
    PFNGLUSEPROGRAMPROC glUseProgram_ = nullptr;
    PFNGLDELETEPROGRAMPROC glDeleteProgram_ = nullptr;
    PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation_ = nullptr;
    PFNGLUNIFORM1IPROC glUniform1i_ = nullptr;
    PFNGLUNIFORM1FPROC glUniform1f_ = nullptr;
    PFNGLUNIFORM2FPROC glUniform2f_ = nullptr;
    PFNGLUNIFORM3FPROC glUniform3f_ = nullptr;
    PFNGLGENBUFFERSPROC glGenBuffers_ = nullptr;
    PFNGLBINDBUFFERPROC glBindBuffer_ = nullptr;
    PFNGLBUFFERDATAPROC glBufferData_ = nullptr;
    PFNGLDELETEBUFFERSPROC glDeleteBuffers_ = nullptr;
    PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray_ = nullptr;
    PFNGLDISABLEVERTEXATTRIBARRAYPROC glDisableVertexAttribArray_ = nullptr;
    PFNGLVERTEXATTRIBPOINTERPROC glVertexAttribPointer_ = nullptr;
    PFNGLACTIVETEXTUREPROC glActiveTexture_ = nullptr;

    struct MeshBuffer
    {
        GLuint vbo = 0;
        GLsizei vertexCount = 0;
    };

    std::unordered_map<const void*, MeshBuffer> gMeshes;
    GLuint gProgram = 0;
    GLuint gBoneTexture = 0;
    bool gInitTried = false;
    bool gAvailable = false;
    std::string gError;

    GLint uBaseTexture = -1;
    GLint uBoneTexture = -1;
    GLint uBodyOrigin = -1;
    GLint uBodyScale = -1;
    GLint uBoneScale = -1;
    GLint uRestScale = -1;
    GLint uLightDir = -1;
    GLint uBodyLight = -1;
    GLint uAlpha = -1;
    GLint uTexOffset = -1;
    GLint uWave = -1;
    GLint uWave2 = -1;
    GLint uChromeLight = -1;
    GLint uTimeTerm = -1;
    GLint uTranslate = -1;
    GLint uLightEnabled = -1;
    GLint uTexMode = -1;

    void* GetGLProc(const char* name)
    {
        void* p = reinterpret_cast<void*>(wglGetProcAddress(name));
        if (p == nullptr || p == reinterpret_cast<void*>(1) || p == reinterpret_cast<void*>(2) ||
            p == reinterpret_cast<void*>(3) || p == reinterpret_cast<void*>(-1))
        {
            static HMODULE ogl = LoadLibraryA("opengl32.dll");
            p = ogl ? reinterpret_cast<void*>(GetProcAddress(ogl, name)) : nullptr;
        }
        return p;
    }

    template<class T>
    bool Load(T& fn, const char* name)
    {
        fn = reinterpret_cast<T>(GetGLProc(name));
        if (!fn)
        {
            gError = std::string("Missing OpenGL function: ") + name;
            return false;
        }
        return true;
    }

    bool LoadFunctions()
    {
        return Load(glCreateShader_, "glCreateShader") &&
               Load(glShaderSource_, "glShaderSource") &&
               Load(glCompileShader_, "glCompileShader") &&
               Load(glGetShaderiv_, "glGetShaderiv") &&
               Load(glGetShaderInfoLog_, "glGetShaderInfoLog") &&
               Load(glDeleteShader_, "glDeleteShader") &&
               Load(glCreateProgram_, "glCreateProgram") &&
               Load(glAttachShader_, "glAttachShader") &&
               Load(glBindAttribLocation_, "glBindAttribLocation") &&
               Load(glLinkProgram_, "glLinkProgram") &&
               Load(glGetProgramiv_, "glGetProgramiv") &&
               Load(glGetProgramInfoLog_, "glGetProgramInfoLog") &&
               Load(glUseProgram_, "glUseProgram") &&
               Load(glDeleteProgram_, "glDeleteProgram") &&
               Load(glGetUniformLocation_, "glGetUniformLocation") &&
               Load(glUniform1i_, "glUniform1i") &&
               Load(glUniform1f_, "glUniform1f") &&
               Load(glUniform2f_, "glUniform2f") &&
               Load(glUniform3f_, "glUniform3f") &&
               Load(glGenBuffers_, "glGenBuffers") &&
               Load(glBindBuffer_, "glBindBuffer") &&
               Load(glBufferData_, "glBufferData") &&
               Load(glDeleteBuffers_, "glDeleteBuffers") &&
               Load(glEnableVertexAttribArray_, "glEnableVertexAttribArray") &&
               Load(glDisableVertexAttribArray_, "glDisableVertexAttribArray") &&
               Load(glVertexAttribPointer_, "glVertexAttribPointer") &&
               Load(glActiveTexture_, "glActiveTexture");
    }

    const char* kVertexShader = R"GLSL(
#version 120
attribute vec3 aPosition;
attribute vec3 aNormal;
attribute vec2 aTexCoord;
attribute float aPositionBone;
attribute float aNormalBone;

uniform sampler2D uBoneTexture;
uniform vec3 uBodyOrigin;
uniform float uBodyScale;
uniform float uBoneScale;
uniform float uRestScale;
uniform vec3 uLightDir;
uniform vec3 uBodyLight;
uniform float uAlpha;
uniform vec2 uTexOffset;
uniform float uWave;
uniform float uWave2;
uniform vec2 uChromeLight;
uniform float uTimeTerm;
uniform int uTranslate;
uniform int uLightEnabled;
uniform int uTexMode;

varying vec2 vUV;
varying vec4 vColor;

vec4 boneRow(float bone, float row)
{
    // Texture is fixed at 3 x 200 RGBA32F. Each bone occupies one Y row,
    // with its affine 3x4 rows in X texels 0,1,2.
    return texture2D(uBoneTexture, vec2((row + 0.5) / 3.0, (bone + 0.5) / 200.0));
}

vec3 skinPosition(float bone)
{
    vec4 r0 = boneRow(bone, 0.0);
    vec4 r1 = boneRow(bone, 1.0);
    vec4 r2 = boneRow(bone, 2.0);
    vec3 p;
    if (abs(uBoneScale - 1.0) < 0.0001)
    {
        vec3 rest = aPosition * ((abs(uRestScale) > 0.00001) ? uRestScale : 1.0);
        vec4 hp = vec4(rest, 1.0);
        p = vec3(dot(r0, hp), dot(r1, hp), dot(r2, hp));
    }
    else
    {
        vec3 rotated = vec3(dot(r0.xyz, aPosition), dot(r1.xyz, aPosition), dot(r2.xyz, aPosition));
        p = rotated * uBoneScale + vec3(r0.w, r1.w, r2.w);
    }
    if (uTranslate != 0)
        p = p * uBodyScale + uBodyOrigin;
    return p;
}

vec3 skinNormal(float bone)
{
    vec4 r0 = boneRow(bone, 0.0);
    vec4 r1 = boneRow(bone, 1.0);
    vec4 r2 = boneRow(bone, 2.0);
    return vec3(dot(r0.xyz, aNormal), dot(r1.xyz, aNormal), dot(r2.xyz, aNormal));
}

vec2 chromeUV(vec3 n)
{
    if (uTexMode == 1) return vec2(n.z * 0.5 + uWave, n.y * 0.5 + uWave * 2.0);
    if (uTexMode == 2) return vec2((n.z+n.x)*0.8 + uWave2*2.0, (n.y+n.x) + uWave2*3.0);
    if (uTexMode == 3) { float d = dot(n, vec3(0.0,-0.1,-0.8)); return vec2(d, 1.0-d); }
    if (uTexMode == 4)
    {
        vec3 l = vec3(uChromeLight.x, uChromeLight.y, 1.0);
        float d = dot(n,l);
        return vec2(d + n.y*0.5 + l.y*3.0 + uTexOffset.x,
                    1.0-d - n.z*0.5 - uWave*3.0 + uTexOffset.y);
    }
    if (uTexMode == 5)
    {
        vec3 l = vec3(uChromeLight.x, uChromeLight.y, 1.0);
        float d = dot(n,l);
        return vec2(d + n.y*3.0 + l.y*5.0,
                    1.0-d - n.z*2.5 - uWave);
    }
    if (uTexMode == 6) { float q=(n.z+n.x)*0.8 + uWave2*2.0; return vec2(q,q); }
    if (uTexMode == 7) { float q=(n.z+n.x)*0.8 + uTimeTerm; return vec2(q,q); }
    if (uTexMode == 8) return n.xy * aTexCoord + uTexOffset;
    if (uTexMode == 9) return vec2(n.z*0.5+0.2, n.y*0.5+0.5);
    return aTexCoord + uTexOffset;
}

void main()
{
    vec3 p = skinPosition(aPositionBone);
    vec3 n = skinNormal(aNormalBone);
    gl_Position = gl_ModelViewProjectionMatrix * vec4(p, 1.0);

    if (uTexMode == 0)
        vUV = aTexCoord + uTexOffset;
    else
        vUV = chromeUV(n);

    float intensity = 1.0;
    if (uLightEnabled != 0)
        intensity = max(dot(n, uLightDir) * 0.8 + 0.4, 0.2);
    vColor = vec4(uBodyLight * intensity, uAlpha);
}
)GLSL";

    const char* kFragmentShader = R"GLSL(
#version 120
uniform sampler2D uBaseTexture;
varying vec2 vUV;
varying vec4 vColor;
void main()
{
    gl_FragColor = texture2D(uBaseTexture, vUV) * vColor;
}
)GLSL";

    GLuint Compile(GLenum type, const char* src)
    {
        GLuint sh = glCreateShader_(type);
        glShaderSource_(sh, 1, &src, nullptr);
        glCompileShader_(sh);
        GLint ok = 0;
        glGetShaderiv_(sh, GL_COMPILE_STATUS, &ok);
        if (!ok)
        {
            GLint len = 0;
            glGetShaderiv_(sh, GL_INFO_LOG_LENGTH, &len);
            std::vector<char> buf(static_cast<std::size_t>(len > 1 ? len : 2));
            GLsizei got = 0;
            glGetShaderInfoLog_(sh, static_cast<GLsizei>(buf.size()), &got, buf.data());
            gError.assign(buf.data(), got > 0 ? static_cast<std::size_t>(got) : std::strlen(buf.data()));
            glDeleteShader_(sh);
            return 0;
        }
        return sh;
    }

    bool BuildProgram()
    {
        GLuint vs = Compile(GL_VERTEX_SHADER, kVertexShader);
        if (!vs) return false;
        GLuint fs = Compile(GL_FRAGMENT_SHADER, kFragmentShader);
        if (!fs) { glDeleteShader_(vs); return false; }

        gProgram = glCreateProgram_();
        glAttachShader_(gProgram, vs);
        glAttachShader_(gProgram, fs);
        glBindAttribLocation_(gProgram, 0, "aPosition");
        glBindAttribLocation_(gProgram, 1, "aNormal");
        glBindAttribLocation_(gProgram, 2, "aTexCoord");
        glBindAttribLocation_(gProgram, 3, "aPositionBone");
        glBindAttribLocation_(gProgram, 4, "aNormalBone");
        glLinkProgram_(gProgram);

        GLint ok = 0;
        glGetProgramiv_(gProgram, GL_LINK_STATUS, &ok);
        glDeleteShader_(vs);
        glDeleteShader_(fs);
        if (!ok)
        {
            GLint len = 0;
            glGetProgramiv_(gProgram, GL_INFO_LOG_LENGTH, &len);
            std::vector<char> buf(static_cast<std::size_t>(len > 1 ? len : 2));
            GLsizei got = 0;
            glGetProgramInfoLog_(gProgram, static_cast<GLsizei>(buf.size()), &got, buf.data());
            gError.assign(buf.data(), got > 0 ? static_cast<std::size_t>(got) : std::strlen(buf.data()));
            glDeleteProgram_(gProgram);
            gProgram = 0;
            return false;
        }

        uBaseTexture = glGetUniformLocation_(gProgram, "uBaseTexture");
        uBoneTexture = glGetUniformLocation_(gProgram, "uBoneTexture");
        uBodyOrigin = glGetUniformLocation_(gProgram, "uBodyOrigin");
        uBodyScale = glGetUniformLocation_(gProgram, "uBodyScale");
        uBoneScale = glGetUniformLocation_(gProgram, "uBoneScale");
        uRestScale = glGetUniformLocation_(gProgram, "uRestScale");
        uLightDir = glGetUniformLocation_(gProgram, "uLightDir");
        uBodyLight = glGetUniformLocation_(gProgram, "uBodyLight");
        uAlpha = glGetUniformLocation_(gProgram, "uAlpha");
        uTexOffset = glGetUniformLocation_(gProgram, "uTexOffset");
        uWave = glGetUniformLocation_(gProgram, "uWave");
        uWave2 = glGetUniformLocation_(gProgram, "uWave2");
        uChromeLight = glGetUniformLocation_(gProgram, "uChromeLight");
        uTimeTerm = glGetUniformLocation_(gProgram, "uTimeTerm");
        uTranslate = glGetUniformLocation_(gProgram, "uTranslate");
        uLightEnabled = glGetUniformLocation_(gProgram, "uLightEnabled");
        uTexMode = glGetUniformLocation_(gProgram, "uTexMode");
        return true;
    }

    bool CreateBoneTexture()
    {
        glGenTextures(1, &gBoneTexture);
        glActiveTexture_(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, gBoneTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F_ARB, 3, 200, 0, GL_RGBA, GL_FLOAT, nullptr);
        GLenum err = glGetError();
        glActiveTexture_(GL_TEXTURE0);
        if (err != GL_NO_ERROR)
        {
            gError = "GL_RGBA32F bone texture allocation failed";
            glDeleteTextures(1, &gBoneTexture);
            gBoneTexture = 0;
            return false;
        }
        return true;
    }
}

namespace MuGpuSkin
{
    bool Initialize()
    {
        if (gInitTried) return gAvailable;
        gInitTried = true;

        if (!wglGetCurrentContext())
        {
            gError = "No current OpenGL context";
            return false;
        }
        if (!LoadFunctions()) return false;

        GLint vertexTextureUnits = 0;
        glGetIntegerv(GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS, &vertexTextureUnits);
        if (vertexTextureUnits <= 0)
        {
            gError = "GPU has no vertex texture fetch units";
            return false;
        }
        if (!BuildProgram()) return false;
        if (!CreateBoneTexture()) return false;

        gAvailable = true;
        return true;
    }

    bool IsAvailable()
    {
        return Initialize();
    }

    const char* LastError()
    {
        return gError.c_str();
    }

    bool HasMesh(const void* meshKey)
    {
        return gMeshes.find(meshKey) != gMeshes.end();
    }

    bool UploadMesh(const void* meshKey, const SkinnedVertex* vertices, std::size_t vertexCount)
    {
        if (!IsAvailable() || !meshKey || !vertices || vertexCount == 0) return false;
        auto old = gMeshes.find(meshKey);
        if (old != gMeshes.end()) return true;

        MeshBuffer b;
        glGenBuffers_(1, &b.vbo);
        glBindBuffer_(GL_ARRAY_BUFFER, b.vbo);
        glBufferData_(GL_ARRAY_BUFFER,
                      static_cast<std::ptrdiff_t>(vertexCount * sizeof(SkinnedVertex)),
                      vertices, GL_STATIC_DRAW);
        glBindBuffer_(GL_ARRAY_BUFFER, 0);
        if (glGetError() != GL_NO_ERROR)
        {
            if (b.vbo) glDeleteBuffers_(1, &b.vbo);
            gError = "Static BMD VBO upload failed";
            return false;
        }
        b.vertexCount = static_cast<GLsizei>(vertexCount);
        gMeshes.emplace(meshKey, b);
        return true;
    }

    bool DrawMesh(const void* meshKey, const SkinningState& s)
    {
        if (!IsAvailable() || !meshKey || !s.boneRows || s.boneCount <= 0) return false;
        auto it = gMeshes.find(meshKey);
        if (it == gMeshes.end()) return false;

        // Upload one compact 3x4 affine palette. 200 bones = 9.6 KB max.
        glActiveTexture_(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, gBoneTexture);
        const int rows = s.boneCount > 200 ? 200 : s.boneCount;
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 3, rows, GL_RGBA, GL_FLOAT, s.boneRows);
        if (glGetError() != GL_NO_ERROR)
        {
            glActiveTexture_(GL_TEXTURE0);
            gError = "Bone palette upload failed";
            return false;
        }

        glUseProgram_(gProgram);
        glUniform1i_(uBaseTexture, 0);
        glUniform1i_(uBoneTexture, 1);
        glUniform3f_(uBodyOrigin, s.bodyOrigin[0], s.bodyOrigin[1], s.bodyOrigin[2]);
        glUniform1f_(uBodyScale, s.bodyScale);
        glUniform1f_(uBoneScale, s.boneScale);
        glUniform1f_(uRestScale, s.restPoseScale);
        glUniform3f_(uLightDir, s.lightDirection[0], s.lightDirection[1], s.lightDirection[2]);
        glUniform3f_(uBodyLight, s.bodyLight[0], s.bodyLight[1], s.bodyLight[2]);
        glUniform1f_(uAlpha, s.alpha);
        glUniform2f_(uTexOffset, s.texOffset[0], s.texOffset[1]);
        glUniform1f_(uWave, s.chromeWave);
        glUniform1f_(uWave2, s.chromeWave2);
        glUniform2f_(uChromeLight, s.chromeLight[0], s.chromeLight[1]);
        glUniform1f_(uTimeTerm, s.chromeTimeTerm);
        glUniform1i_(uTranslate, s.translate);
        glUniform1i_(uLightEnabled, s.lightEnabled);
        glUniform1i_(uTexMode, s.texCoordMode);

        glBindBuffer_(GL_ARRAY_BUFFER, it->second.vbo);
        const GLsizei stride = static_cast<GLsizei>(sizeof(SkinnedVertex));
        glEnableVertexAttribArray_(0);
        glEnableVertexAttribArray_(1);
        glEnableVertexAttribArray_(2);
        glEnableVertexAttribArray_(3);
        glEnableVertexAttribArray_(4);
        glVertexAttribPointer_(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(offsetof(SkinnedVertex, px)));
        glVertexAttribPointer_(1, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(offsetof(SkinnedVertex, nx)));
        glVertexAttribPointer_(2, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(offsetof(SkinnedVertex, u)));
        glVertexAttribPointer_(3, 1, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(offsetof(SkinnedVertex, positionBone)));
        glVertexAttribPointer_(4, 1, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(offsetof(SkinnedVertex, normalBone)));

        glActiveTexture_(GL_TEXTURE0); // Main's currently bound BMD/chrome texture is unit 0.
        glDrawArrays(GL_TRIANGLES, 0, it->second.vertexCount);

        glDisableVertexAttribArray_(4);
        glDisableVertexAttribArray_(3);
        glDisableVertexAttribArray_(2);
        glDisableVertexAttribArray_(1);
        glDisableVertexAttribArray_(0);
        glBindBuffer_(GL_ARRAY_BUFFER, 0);
        glUseProgram_(0);
        glActiveTexture_(GL_TEXTURE0);
        return glGetError() == GL_NO_ERROR;
    }

    void ReleaseMesh(const void* meshKey)
    {
        auto it = gMeshes.find(meshKey);
        if (it == gMeshes.end()) return;
        if (it->second.vbo && glDeleteBuffers_) glDeleteBuffers_(1, &it->second.vbo);
        gMeshes.erase(it);
    }

    void Shutdown()
    {
        if (glDeleteBuffers_)
        {
            for (auto& kv : gMeshes)
                if (kv.second.vbo) glDeleteBuffers_(1, &kv.second.vbo);
        }
        gMeshes.clear();
        if (gBoneTexture) glDeleteTextures(1, &gBoneTexture);
        gBoneTexture = 0;
        if (gProgram && glDeleteProgram_) glDeleteProgram_(gProgram);
        gProgram = 0;
        gAvailable = false;
        gInitTried = false;
    }
}
