#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <gl/GL.h>
#include <vector>
#include <string>
#include <cstdint>
#include <cstdio>
#include <cstring>

#pragma comment(lib, "opengl32.lib")

static HMODULE gSelf = nullptr;
static bool gBatchImmediate = true;
static bool gNoFlush = true;
static bool gLogStats = true;
static std::string gLogPath;

struct BatchVertex
{
    GLfloat x, y, z, w;
    GLfloat r, g, b, a;
    GLfloat u, v;
};

thread_local bool gCapturing = false;
thread_local bool gFallbackBegin = false;
thread_local GLenum gMode = GL_TRIANGLES;
thread_local GLfloat gColor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
thread_local GLfloat gTex[2] = {0.0f, 0.0f};
thread_local std::vector<BatchVertex> gVertices;

static unsigned long long gFrame = 0;
static unsigned long long gBatches = 0;
static unsigned long long gVerticesBatched = 0;
static unsigned long long gFlushSkipped = 0;
static unsigned long long gHooked = 0;

static std::string ModuleDir()
{
    char path[MAX_PATH] = {};
    GetModuleFileNameA(gSelf, path, MAX_PATH);
    char* p = strrchr(path, '\\');
    if (p) *(p + 1) = 0;
    return std::string(path);
}

static void Log(const char* fmt, ...)
{
    if (gLogPath.empty()) return;
    char buffer[1024] = {};
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf_s(buffer, sizeof(buffer), _TRUNCATE, fmt, ap);
    va_end(ap);

    HANDLE h = CreateFileA(gLogPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(h, buffer, (DWORD)strlen(buffer), &written, nullptr);
    CloseHandle(h);
}

static bool SupportedPrimitive(GLenum mode)
{
    switch (mode)
    {
        case GL_POINTS:
        case GL_LINES:
        case GL_LINE_LOOP:
        case GL_LINE_STRIP:
        case GL_TRIANGLES:
        case GL_TRIANGLE_STRIP:
        case GL_TRIANGLE_FAN:
        case GL_QUADS:
        case GL_QUAD_STRIP:
        case GL_POLYGON:
            return true;
        default:
            return false;
    }
}

static void AddVertex(GLfloat x, GLfloat y, GLfloat z, GLfloat w)
{
    BatchVertex out;
    out.x = x; out.y = y; out.z = z; out.w = w;
    out.r = gColor[0]; out.g = gColor[1]; out.b = gColor[2]; out.a = gColor[3];
    out.u = gTex[0]; out.v = gTex[1];
    gVertices.push_back(out);
}

extern "C" void APIENTRY Hook_glBegin(GLenum mode)
{
    if (!gBatchImmediate || !SupportedPrimitive(mode) || gCapturing || gFallbackBegin)
    {
        gFallbackBegin = true;
        glBegin(mode);
        return;
    }

    if (gVertices.capacity() < 16384)
        gVertices.reserve(16384);
    gVertices.clear();
    gMode = mode;
    gCapturing = true;
}

extern "C" void APIENTRY Hook_glEnd()
{
    if (gFallbackBegin)
    {
        glEnd();
        gFallbackBegin = false;
        return;
    }

    if (!gCapturing)
    {
        glEnd();
        return;
    }

    gCapturing = false;
    if (gVertices.empty()) return;

    glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);

    const GLsizei stride = (GLsizei)sizeof(BatchVertex);
    glVertexPointer(4, GL_FLOAT, stride, &gVertices[0].x);
    glColorPointer(4, GL_FLOAT, stride, &gVertices[0].r);
    glTexCoordPointer(2, GL_FLOAT, stride, &gVertices[0].u);
    glDrawArrays(gMode, 0, (GLsizei)gVertices.size());

    glPopClientAttrib();

    // Preserve fixed-function current values exactly enough for subsequent draws.
    glColor4f(gColor[0], gColor[1], gColor[2], gColor[3]);
    glTexCoord2f(gTex[0], gTex[1]);

    ++gBatches;
    gVerticesBatched += gVertices.size();
}

extern "C" void APIENTRY Hook_glVertex2f(GLfloat x, GLfloat y)
{
    if (gCapturing) { AddVertex(x, y, 0.0f, 1.0f); return; }
    glVertex2f(x, y);
}

extern "C" void APIENTRY Hook_glVertex3f(GLfloat x, GLfloat y, GLfloat z)
{
    if (gCapturing) { AddVertex(x, y, z, 1.0f); return; }
    glVertex3f(x, y, z);
}

extern "C" void APIENTRY Hook_glVertex3fv(const GLfloat* v)
{
    if (gCapturing) { AddVertex(v[0], v[1], v[2], 1.0f); return; }
    glVertex3fv(v);
}

extern "C" void APIENTRY Hook_glVertex4fv(const GLfloat* v)
{
    if (gCapturing) { AddVertex(v[0], v[1], v[2], v[3]); return; }
    glVertex4fv(v);
}

extern "C" void APIENTRY Hook_glTexCoord2f(GLfloat u, GLfloat v)
{
    gTex[0] = u; gTex[1] = v;
    if (gCapturing) return;
    glTexCoord2f(u, v);
}

extern "C" void APIENTRY Hook_glTexCoord2fv(const GLfloat* t)
{
    gTex[0] = t[0]; gTex[1] = t[1];
    if (gCapturing) return;
    glTexCoord2fv(t);
}

extern "C" void APIENTRY Hook_glColor3f(GLfloat r, GLfloat g, GLfloat b)
{
    gColor[0] = r; gColor[1] = g; gColor[2] = b; gColor[3] = 1.0f;
    if (gCapturing) return;
    glColor3f(r, g, b);
}

extern "C" void APIENTRY Hook_glColor4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a)
{
    gColor[0] = r; gColor[1] = g; gColor[2] = b; gColor[3] = a;
    if (gCapturing) return;
    glColor4f(r, g, b, a);
}

extern "C" void APIENTRY Hook_glColor3fv(const GLfloat* c)
{
    gColor[0] = c[0]; gColor[1] = c[1]; gColor[2] = c[2]; gColor[3] = 1.0f;
    if (gCapturing) return;
    glColor3fv(c);
}

extern "C" void APIENTRY Hook_glColor4fv(const GLfloat* c)
{
    gColor[0] = c[0]; gColor[1] = c[1]; gColor[2] = c[2]; gColor[3] = c[3];
    if (gCapturing) return;
    glColor4fv(c);
}

extern "C" void APIENTRY Hook_glColor3ub(GLubyte r, GLubyte g, GLubyte b)
{
    gColor[0] = r / 255.0f; gColor[1] = g / 255.0f; gColor[2] = b / 255.0f; gColor[3] = 1.0f;
    if (gCapturing) return;
    glColor3ub(r, g, b);
}

extern "C" void APIENTRY Hook_glColor4ub(GLubyte r, GLubyte g, GLubyte b, GLubyte a)
{
    gColor[0] = r / 255.0f; gColor[1] = g / 255.0f; gColor[2] = b / 255.0f; gColor[3] = a / 255.0f;
    if (gCapturing) return;
    glColor4ub(r, g, b, a);
}

extern "C" void APIENTRY Hook_glFlush()
{
    if (gNoFlush)
    {
        ++gFlushSkipped;
        return;
    }
    glFlush();
}

extern "C" void APIENTRY Hook_glClear(GLbitfield mask)
{
    glClear(mask);
    ++gFrame;
    if (gLogStats && (gFrame % 300ull) == 0ull)
    {
        Log("frame=%llu batches=%llu vertices=%llu flushSkipped=%llu\r\n",
            gFrame, gBatches, gVerticesBatched, gFlushSkipped);
    }
}

struct HookItem
{
    const char* name;
    void* fn;
};

static bool PatchOneImport(const char* dllWanted, const char* procWanted, void* hook)
{
    HMODULE mainModule = GetModuleHandleA(nullptr);
    if (!mainModule) return false;

    auto base = reinterpret_cast<unsigned char*>(mainModule);
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;

    auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;

    const IMAGE_DATA_DIRECTORY& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return false;

    auto imp = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress);
    for (; imp->Name; ++imp)
    {
        const char* dllName = reinterpret_cast<const char*>(base + imp->Name);
        if (_stricmp(dllName, dllWanted) != 0) continue;
        if (!imp->OriginalFirstThunk) continue;

        auto original = reinterpret_cast<IMAGE_THUNK_DATA*>(base + imp->OriginalFirstThunk);
        auto thunk = reinterpret_cast<IMAGE_THUNK_DATA*>(base + imp->FirstThunk);

        for (; original->u1.AddressOfData; ++original, ++thunk)
        {
            if (IMAGE_SNAP_BY_ORDINAL(original->u1.Ordinal)) continue;
            auto byName = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + original->u1.AddressOfData);
            if (strcmp(reinterpret_cast<const char*>(byName->Name), procWanted) != 0) continue;

            DWORD oldProtect = 0;
            if (!VirtualProtect(&thunk->u1.Function, sizeof(thunk->u1.Function), PAGE_EXECUTE_READWRITE, &oldProtect))
                return false;

            thunk->u1.Function = reinterpret_cast<ULONG_PTR>(hook);
            DWORD ignored = 0;
            VirtualProtect(&thunk->u1.Function, sizeof(thunk->u1.Function), oldProtect, &ignored);
            FlushInstructionCache(GetCurrentProcess(), &thunk->u1.Function, sizeof(thunk->u1.Function));
            return true;
        }
    }
    return false;
}

static DWORD WINAPI InitThread(LPVOID)
{
    Sleep(300);
    const std::string dir = ModuleDir();
    gLogPath = dir + "MuCrowdFPS.log";
    const std::string ini = dir + "MuCrowdFPS.ini";

    gBatchImmediate = GetPrivateProfileIntA("CrowdFPS", "BatchImmediate", 1, ini.c_str()) != 0;
    gNoFlush = GetPrivateProfileIntA("CrowdFPS", "NoFlush", 1, ini.c_str()) != 0;
    gLogStats = GetPrivateProfileIntA("CrowdFPS", "LogStats", 1, ini.c_str()) != 0;

    DeleteFileA(gLogPath.c_str());
    Log("MuCrowdFPS v1 - Original OpenGL crowd optimizer\r\n");
    Log("BatchImmediate=%d NoFlush=%d LogStats=%d\r\n", (int)gBatchImmediate, (int)gNoFlush, (int)gLogStats);

    HookItem hooks[] = {
        {"glBegin",       (void*)&Hook_glBegin},
        {"glEnd",         (void*)&Hook_glEnd},
        {"glVertex2f",    (void*)&Hook_glVertex2f},
        {"glVertex3f",    (void*)&Hook_glVertex3f},
        {"glVertex3fv",   (void*)&Hook_glVertex3fv},
        {"glVertex4fv",   (void*)&Hook_glVertex4fv},
        {"glTexCoord2f",  (void*)&Hook_glTexCoord2f},
        {"glTexCoord2fv", (void*)&Hook_glTexCoord2fv},
        {"glColor3f",     (void*)&Hook_glColor3f},
        {"glColor4f",     (void*)&Hook_glColor4f},
        {"glColor3fv",    (void*)&Hook_glColor3fv},
        {"glColor4fv",    (void*)&Hook_glColor4fv},
        {"glColor3ub",    (void*)&Hook_glColor3ub},
        {"glColor4ub",    (void*)&Hook_glColor4ub},
        {"glFlush",       (void*)&Hook_glFlush},
        {"glClear",       (void*)&Hook_glClear},
    };

    for (const auto& h : hooks)
    {
        if (PatchOneImport("OPENGL32.dll", h.name, h.fn))
        {
            ++gHooked;
            Log("hooked %s\r\n", h.name);
        }
        else
        {
            Log("not-found %s\r\n", h.name);
        }
    }

    Log("IAT hooks installed=%llu\r\n", gHooked);
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        gSelf = hinst;
        DisableThreadLibraryCalls(hinst);
        HANDLE h = CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);
        if (h) CloseHandle(h);
    }
    return TRUE;
}
