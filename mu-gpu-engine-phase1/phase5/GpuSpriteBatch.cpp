#include "GpuSpriteBatch.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>

#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

#ifndef APIENTRY
#define APIENTRY __stdcall
#endif
#ifndef GL_ARRAY_BUFFER
#define GL_ARRAY_BUFFER 0x8892
#endif
#ifndef GL_STATIC_DRAW
#define GL_STATIC_DRAW 0x88E4
#endif
#ifndef GL_STREAM_DRAW
#define GL_STREAM_DRAW 0x88E0
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
    typedef void (APIENTRY* PFNGLGENBUFFERSPROC)(GLsizei, GLuint*);
    typedef void (APIENTRY* PFNGLBINDBUFFERPROC)(GLenum, GLuint);
    typedef void (APIENTRY* PFNGLBUFFERDATAPROC)(GLenum, std::ptrdiff_t, const void*, GLenum);
    typedef void (APIENTRY* PFNGLDELETEBUFFERSPROC)(GLsizei, const GLuint*);
    typedef void (APIENTRY* PFNGLENABLEVERTEXATTRIBARRAYPROC)(GLuint);
    typedef void (APIENTRY* PFNGLDISABLEVERTEXATTRIBARRAYPROC)(GLuint);
    typedef void (APIENTRY* PFNGLVERTEXATTRIBPOINTERPROC)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
    typedef void (APIENTRY* PFNGLVERTEXATTRIBDIVISORPROC)(GLuint, GLuint);
    typedef void (APIENTRY* PFNGLDRAWARRAYSINSTANCEDPROC)(GLenum, GLint, GLsizei, GLsizei);
    typedef void (APIENTRY* PFNGLACTIVETEXTUREPROC)(GLenum);

    PFNGLCREATESHADERPROC glCreateShader_ = 0;
    PFNGLSHADERSOURCEPROC glShaderSource_ = 0;
    PFNGLCOMPILESHADERPROC glCompileShader_ = 0;
    PFNGLGETSHADERIVPROC glGetShaderiv_ = 0;
    PFNGLGETSHADERINFOLOGPROC glGetShaderInfoLog_ = 0;
    PFNGLDELETESHADERPROC glDeleteShader_ = 0;
    PFNGLCREATEPROGRAMPROC glCreateProgram_ = 0;
    PFNGLATTACHSHADERPROC glAttachShader_ = 0;
    PFNGLBINDATTRIBLOCATIONPROC glBindAttribLocation_ = 0;
    PFNGLLINKPROGRAMPROC glLinkProgram_ = 0;
    PFNGLGETPROGRAMIVPROC glGetProgramiv_ = 0;
    PFNGLGETPROGRAMINFOLOGPROC glGetProgramInfoLog_ = 0;
    PFNGLUSEPROGRAMPROC glUseProgram_ = 0;
    PFNGLDELETEPROGRAMPROC glDeleteProgram_ = 0;
    PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation_ = 0;
    PFNGLUNIFORM1IPROC glUniform1i_ = 0;
    PFNGLGENBUFFERSPROC glGenBuffers_ = 0;
    PFNGLBINDBUFFERPROC glBindBuffer_ = 0;
    PFNGLBUFFERDATAPROC glBufferData_ = 0;
    PFNGLDELETEBUFFERSPROC glDeleteBuffers_ = 0;
    PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray_ = 0;
    PFNGLDISABLEVERTEXATTRIBARRAYPROC glDisableVertexAttribArray_ = 0;
    PFNGLVERTEXATTRIBPOINTERPROC glVertexAttribPointer_ = 0;
    PFNGLVERTEXATTRIBDIVISORPROC glVertexAttribDivisor_ = 0;
    PFNGLDRAWARRAYSINSTANCEDPROC glDrawArraysInstanced_ = 0;
    PFNGLACTIVETEXTUREPROC glActiveTexture_ = 0;

    GLuint gProgram = 0;
    GLuint gQuadVbo = 0;
    GLuint gInstanceVbo = 0;
    GLint gTextureUniform = -1;
    bool gTried = false;
    bool gAvailable = false;
    std::string gError;

    void* GetGLProc(const char* name)
    {
        void* p = reinterpret_cast<void*>(wglGetProcAddress(name));
        if (p == 0 || p == reinterpret_cast<void*>(1) || p == reinterpret_cast<void*>(2) ||
            p == reinterpret_cast<void*>(3) || p == reinterpret_cast<void*>(-1))
        {
            static HMODULE ogl = LoadLibraryA("opengl32.dll");
            p = ogl ? reinterpret_cast<void*>(GetProcAddress(ogl, name)) : 0;
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

    template<class T>
    bool LoadEither(T& fn, const char* coreName, const char* arbName)
    {
        fn = reinterpret_cast<T>(GetGLProc(coreName));
        if (!fn) fn = reinterpret_cast<T>(GetGLProc(arbName));
        if (!fn)
        {
            gError = std::string("Missing OpenGL instancing function: ") + coreName + "/" + arbName;
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
               Load(glGenBuffers_, "glGenBuffers") &&
               Load(glBindBuffer_, "glBindBuffer") &&
               Load(glBufferData_, "glBufferData") &&
               Load(glDeleteBuffers_, "glDeleteBuffers") &&
               Load(glEnableVertexAttribArray_, "glEnableVertexAttribArray") &&
               Load(glDisableVertexAttribArray_, "glDisableVertexAttribArray") &&
               Load(glVertexAttribPointer_, "glVertexAttribPointer") &&
               LoadEither(glVertexAttribDivisor_, "glVertexAttribDivisor", "glVertexAttribDivisorARB") &&
               LoadEither(glDrawArraysInstanced_, "glDrawArraysInstanced", "glDrawArraysInstancedARB") &&
               Load(glActiveTexture_, "glActiveTexture");
    }

    const char* kVertexShader =
        "#version 120\n"
        "attribute vec2 aCorner;\n"
        "attribute vec3 aCenter;\n"
        "attribute vec2 aSize;\n"
        "attribute float aRotationDeg;\n"
        "attribute vec4 aColor;\n"
        "attribute vec4 aUvRect;\n"
        "varying vec2 vUV;\n"
        "varying vec4 vColor;\n"
        "void main()\n"
        "{\n"
        "    float r = aRotationDeg * 0.017453292519943295;\n"
        "    float c = cos(r);\n"
        "    float s = sin(r);\n"
        "    vec2 local = aCorner * aSize;\n"
        "    vec2 rotated = vec2(local.x*c - local.y*s, local.x*s + local.y*c);\n"
        "    vec3 p = aCenter + vec3(rotated, 0.0);\n"
        "    gl_Position = gl_ProjectionMatrix * vec4(p, 1.0);\n"
        "    vec2 t = aCorner + vec2(0.5);\n"
        "    vUV = vec2(aUvRect.x + t.x*aUvRect.z,\n"
        "               aUvRect.y + (1.0-t.y)*aUvRect.w);\n"
        "    vColor = aColor;\n"
        "}\n";

    const char* kFragmentShader =
        "#version 120\n"
        "uniform sampler2D uTexture;\n"
        "varying vec2 vUV;\n"
        "varying vec4 vColor;\n"
        "void main()\n"
        "{\n"
        "    gl_FragColor = texture2D(uTexture, vUV) * vColor;\n"
        "}\n";

    GLuint Compile(GLenum type, const char* source)
    {
        GLuint shader = glCreateShader_(type);
        glShaderSource_(shader, 1, &source, 0);
        glCompileShader_(shader);
        GLint ok = 0;
        glGetShaderiv_(shader, GL_COMPILE_STATUS, &ok);
        if (!ok)
        {
            GLint len = 0;
            glGetShaderiv_(shader, GL_INFO_LOG_LENGTH, &len);
            std::vector<char> text(static_cast<std::size_t>(len > 1 ? len : 2), 0);
            GLsizei written = 0;
            glGetShaderInfoLog_(shader, static_cast<GLsizei>(text.size()), &written, &text[0]);
            gError.assign(&text[0], written > 0 ? static_cast<std::size_t>(written) : std::strlen(&text[0]));
            glDeleteShader_(shader);
            return 0;
        }
        return shader;
    }

    bool BuildProgram()
    {
        GLuint vs = Compile(GL_VERTEX_SHADER, kVertexShader);
        if (!vs) return false;
        GLuint fs = Compile(GL_FRAGMENT_SHADER, kFragmentShader);
        if (!fs)
        {
            glDeleteShader_(vs);
            return false;
        }

        gProgram = glCreateProgram_();
        glAttachShader_(gProgram, vs);
        glAttachShader_(gProgram, fs);
        glBindAttribLocation_(gProgram, 0, "aCorner");
        glBindAttribLocation_(gProgram, 1, "aCenter");
        glBindAttribLocation_(gProgram, 2, "aSize");
        glBindAttribLocation_(gProgram, 3, "aRotationDeg");
        glBindAttribLocation_(gProgram, 4, "aColor");
        glBindAttribLocation_(gProgram, 5, "aUvRect");
        glLinkProgram_(gProgram);

        glDeleteShader_(vs);
        glDeleteShader_(fs);

        GLint ok = 0;
        glGetProgramiv_(gProgram, GL_LINK_STATUS, &ok);
        if (!ok)
        {
            GLint len = 0;
            glGetProgramiv_(gProgram, GL_INFO_LOG_LENGTH, &len);
            std::vector<char> text(static_cast<std::size_t>(len > 1 ? len : 2), 0);
            GLsizei written = 0;
            glGetProgramInfoLog_(gProgram, static_cast<GLsizei>(text.size()), &written, &text[0]);
            gError.assign(&text[0], written > 0 ? static_cast<std::size_t>(written) : std::strlen(&text[0]));
            glDeleteProgram_(gProgram);
            gProgram = 0;
            return false;
        }

        gTextureUniform = glGetUniformLocation_(gProgram, "uTexture");
        return true;
    }

    bool BuildBuffers()
    {
        static const GLfloat quad[8] =
        {
            -0.5f, -0.5f,
             0.5f, -0.5f,
            -0.5f,  0.5f,
             0.5f,  0.5f
        };

        glGenBuffers_(1, &gQuadVbo);
        glBindBuffer_(GL_ARRAY_BUFFER, gQuadVbo);
        glBufferData_(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);

        glGenBuffers_(1, &gInstanceVbo);
        glBindBuffer_(GL_ARRAY_BUFFER, 0);

        if (!gQuadVbo || !gInstanceVbo || glGetError() != GL_NO_ERROR)
        {
            gError = "GPU sprite VBO creation failed";
            return false;
        }
        return true;
    }

    bool Initialize()
    {
        if (gTried) return gAvailable;
        gTried = true;

        if (!wglGetCurrentContext())
        {
            gError = "No current OpenGL context for GPU sprite batch";
            return false;
        }
        if (!LoadFunctions()) return false;
        if (!BuildProgram()) return false;
        if (!BuildBuffers()) return false;

        gAvailable = true;
        return true;
    }
}

namespace MuGpuSprite
{
    bool IsAvailable()
    {
        return Initialize();
    }

    const char* LastError()
    {
        return gError.c_str();
    }

    bool Draw(const SpriteInstance* instances, std::size_t count)
    {
        if (!instances || count == 0) return true;
        if (!Initialize()) return false;

        glActiveTexture_(GL_TEXTURE0);
        glUseProgram_(gProgram);
        glUniform1i_(gTextureUniform, 0);

        glBindBuffer_(GL_ARRAY_BUFFER, gQuadVbo);
        glEnableVertexAttribArray_(0);
        glVertexAttribPointer_(0, 2, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(2 * sizeof(float)), 0);
        glVertexAttribDivisor_(0, 0);

        glBindBuffer_(GL_ARRAY_BUFFER, gInstanceVbo);
        glBufferData_(GL_ARRAY_BUFFER,
                      static_cast<std::ptrdiff_t>(count * sizeof(SpriteInstance)),
                      instances, GL_STREAM_DRAW);

        const GLsizei stride = static_cast<GLsizei>(sizeof(SpriteInstance));
        glEnableVertexAttribArray_(1);
        glEnableVertexAttribArray_(2);
        glEnableVertexAttribArray_(3);
        glEnableVertexAttribArray_(4);
        glEnableVertexAttribArray_(5);
        glVertexAttribPointer_(1, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(offsetof(SpriteInstance, center)));
        glVertexAttribPointer_(2, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(offsetof(SpriteInstance, size)));
        glVertexAttribPointer_(3, 1, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(offsetof(SpriteInstance, rotationDeg)));
        glVertexAttribPointer_(4, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(offsetof(SpriteInstance, color)));
        glVertexAttribPointer_(5, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(offsetof(SpriteInstance, uvRect)));
        glVertexAttribDivisor_(1, 1);
        glVertexAttribDivisor_(2, 1);
        glVertexAttribDivisor_(3, 1);
        glVertexAttribDivisor_(4, 1);
        glVertexAttribDivisor_(5, 1);

        glDrawArraysInstanced_(GL_TRIANGLE_STRIP, 0, 4, static_cast<GLsizei>(count));

        for (GLuint i = 1; i <= 5; ++i)
            glVertexAttribDivisor_(i, 0);
        for (GLuint i = 0; i <= 5; ++i)
            glDisableVertexAttribArray_(i);

        glBindBuffer_(GL_ARRAY_BUFFER, 0);
        glUseProgram_(0);
        glActiveTexture_(GL_TEXTURE0);
        return true;
    }

    void Shutdown()
    {
        if (glDeleteBuffers_)
        {
            if (gInstanceVbo) glDeleteBuffers_(1, &gInstanceVbo);
            if (gQuadVbo) glDeleteBuffers_(1, &gQuadVbo);
        }
        gInstanceVbo = 0;
        gQuadVbo = 0;
        if (gProgram && glDeleteProgram_) glDeleteProgram_(gProgram);
        gProgram = 0;
        gAvailable = false;
        gTried = false;
    }
}
