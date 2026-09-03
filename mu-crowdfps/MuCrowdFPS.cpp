#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <gl/GL.h>
#include <vector>
#include <string>
#include <cstdint>
#include <cstdio>
#include <cstring>

#pragma comment(lib, "opengl32.lib")
#pragma comment(lib, "gdi32.lib")

static HMODULE gSelf = nullptr;
static bool gEnabled = true;
static bool gNoFlush = true;
static bool gLogStats = true;
static unsigned gMaxPendingVertices = 131072;
static std::string gLogPath;

struct BatchVertex
{
    GLfloat x,y,z,w;
    GLfloat r,g,b,a;
    GLfloat u,v;
};

thread_local bool gInsideBegin = false;
thread_local bool gCapture = false;
thread_local GLenum gBeginMode = 0;
thread_local size_t gBlockStart = 0;
thread_local GLfloat gColor[4] = {1,1,1,1};
thread_local GLfloat gTex[2] = {0,0};
thread_local GLenum gPendingMode = 0;
thread_local std::vector<BatchVertex> gPending;

static unsigned long long gFrame = 0;
static unsigned long long gHooked = 0;
static unsigned long long gBeginSeen = 0;
static unsigned long long gCapturedBegins = 0;
static unsigned long long gCapturedVerts = 0;
static unsigned long long gBatchDraws = 0;
static unsigned long long gStateFlushes = 0;
static unsigned long long gSwapFlushes = 0;
static unsigned long long gFlushSkipped = 0;
static unsigned long long gDirectDrawArrays = 0;

static std::string ModuleDir()
{
    char p[MAX_PATH] = {};
    GetModuleFileNameA(gSelf,p,MAX_PATH);
    char* s = strrchr(p,'\\');
    if(s) *(s+1)=0;
    return std::string(p);
}

static void Log(const char* fmt,...)
{
    if(gLogPath.empty()) return;
    char b[1024] = {};
    va_list ap; va_start(ap,fmt);
    _vsnprintf_s(b,sizeof(b),_TRUNCATE,fmt,ap);
    va_end(ap);
    HANDLE h=CreateFileA(gLogPath.c_str(),FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(h==INVALID_HANDLE_VALUE) return;
    DWORD w=0; WriteFile(h,b,(DWORD)strlen(b),&w,nullptr); CloseHandle(h);
}

static bool BatchableMode(GLenum mode)
{
    return mode==GL_TRIANGLES || mode==GL_QUADS || mode==GL_LINES || mode==GL_POINTS;
}

static size_t CompleteCount(GLenum mode,size_t n)
{
    if(mode==GL_TRIANGLES) return n-(n%3);
    if(mode==GL_QUADS) return n-(n%4);
    if(mode==GL_LINES) return n-(n%2);
    return n;
}

static void RestoreCurrentAttributes()
{
    glColor4f(gColor[0],gColor[1],gColor[2],gColor[3]);
    glTexCoord2f(gTex[0],gTex[1]);
}

static void FlushPending(bool stateFlush=false,bool swapFlush=false)
{
    if(gPending.empty()) return;

    const GLsizei stride=(GLsizei)sizeof(BatchVertex);
    glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glVertexPointer(4,GL_FLOAT,stride,&gPending[0].x);
    glColorPointer(4,GL_FLOAT,stride,&gPending[0].r);
    glTexCoordPointer(2,GL_FLOAT,stride,&gPending[0].u);
    glDrawArrays(gPendingMode,0,(GLsizei)gPending.size());
    glPopClientAttrib();
    RestoreCurrentAttributes();

    ++gBatchDraws;
    if(stateFlush) ++gStateFlushes;
    if(swapFlush) ++gSwapFlushes;
    gPending.clear();
    gPendingMode=0;
}

static inline void AddVertex(GLfloat x,GLfloat y,GLfloat z,GLfloat w)
{
    BatchVertex q={x,y,z,w,gColor[0],gColor[1],gColor[2],gColor[3],gTex[0],gTex[1]};
    gPending.push_back(q);
    ++gCapturedVerts;
}

extern "C" void APIENTRY Hook_glBegin(GLenum mode)
{
    ++gBeginSeen;
    if(!gEnabled || !BatchableMode(mode) || gInsideBegin)
    {
        FlushPending(true,false);
        gInsideBegin=true; gCapture=false; gBeginMode=mode;
        glBegin(mode);
        return;
    }

    if(!gPending.empty() && gPendingMode!=mode)
        FlushPending(true,false);

    if(gPending.empty()) gPendingMode=mode;
    if(gPending.capacity()<gMaxPendingVertices) gPending.reserve(gMaxPendingVertices);

    gInsideBegin=true;
    gCapture=true;
    gBeginMode=mode;
    gBlockStart=gPending.size();
    ++gCapturedBegins;
}

extern "C" void APIENTRY Hook_glEnd()
{
    if(!gInsideBegin)
    {
        FlushPending(true,false);
        glEnd();
        return;
    }

    if(!gCapture)
    {
        glEnd();
    }
    else
    {
        const size_t blockCount=gPending.size()-gBlockStart;
        const size_t keep=CompleteCount(gBeginMode,blockCount);
        if(keep<blockCount)
            gPending.resize(gBlockStart+keep);
        if(gPending.size()>=gMaxPendingVertices)
            FlushPending(false,false);
    }

    gInsideBegin=false;
    gCapture=false;
    gBeginMode=0;
}

extern "C" void APIENTRY Hook_glVertex2f(GLfloat x,GLfloat y){ if(gCapture){AddVertex(x,y,0,1);return;} glVertex2f(x,y); }
extern "C" void APIENTRY Hook_glVertex3f(GLfloat x,GLfloat y,GLfloat z){ if(gCapture){AddVertex(x,y,z,1);return;} glVertex3f(x,y,z); }
extern "C" void APIENTRY Hook_glVertex3fv(const GLfloat* v){ if(gCapture){AddVertex(v[0],v[1],v[2],1);return;} glVertex3fv(v); }
extern "C" void APIENTRY Hook_glVertex4fv(const GLfloat* v){ if(gCapture){AddVertex(v[0],v[1],v[2],v[3]);return;} glVertex4fv(v); }
extern "C" void APIENTRY Hook_glTexCoord2f(GLfloat u,GLfloat v){ gTex[0]=u;gTex[1]=v; if(!gCapture) glTexCoord2f(u,v); }
extern "C" void APIENTRY Hook_glTexCoord2fv(const GLfloat* t){ gTex[0]=t[0];gTex[1]=t[1]; if(!gCapture) glTexCoord2fv(t); }
extern "C" void APIENTRY Hook_glColor3f(GLfloat r,GLfloat g,GLfloat b){ gColor[0]=r;gColor[1]=g;gColor[2]=b;gColor[3]=1; if(!gCapture) glColor3f(r,g,b); }
extern "C" void APIENTRY Hook_glColor4f(GLfloat r,GLfloat g,GLfloat b,GLfloat a){ gColor[0]=r;gColor[1]=g;gColor[2]=b;gColor[3]=a; if(!gCapture) glColor4f(r,g,b,a); }
extern "C" void APIENTRY Hook_glColor3fv(const GLfloat* c){ gColor[0]=c[0];gColor[1]=c[1];gColor[2]=c[2];gColor[3]=1; if(!gCapture) glColor3fv(c); }
extern "C" void APIENTRY Hook_glColor4fv(const GLfloat* c){ gColor[0]=c[0];gColor[1]=c[1];gColor[2]=c[2];gColor[3]=c[3]; if(!gCapture) glColor4fv(c); }
extern "C" void APIENTRY Hook_glColor3ub(GLubyte r,GLubyte g,GLubyte b){ gColor[0]=r/255.f;gColor[1]=g/255.f;gColor[2]=b/255.f;gColor[3]=1; if(!gCapture) glColor3ub(r,g,b); }
extern "C" void APIENTRY Hook_glColor4ub(GLubyte r,GLubyte g,GLubyte b,GLubyte a){ gColor[0]=r/255.f;gColor[1]=g/255.f;gColor[2]=b/255.f;gColor[3]=a/255.f; if(!gCapture) glColor4ub(r,g,b,a); }

#define FLUSH_STATE() do { if(!gInsideBegin) FlushPending(true,false); } while(0)

extern "C" void APIENTRY Hook_glEnable(GLenum x){ FLUSH_STATE(); glEnable(x); }
extern "C" void APIENTRY Hook_glDisable(GLenum x){ FLUSH_STATE(); glDisable(x); }
extern "C" void APIENTRY Hook_glBindTexture(GLenum a,GLuint b){ FLUSH_STATE(); glBindTexture(a,b); }
extern "C" void APIENTRY Hook_glBlendFunc(GLenum a,GLenum b){ FLUSH_STATE(); glBlendFunc(a,b); }
extern "C" void APIENTRY Hook_glAlphaFunc(GLenum a,GLclampf b){ FLUSH_STATE(); glAlphaFunc(a,b); }
extern "C" void APIENTRY Hook_glDepthFunc(GLenum a){ FLUSH_STATE(); glDepthFunc(a); }
extern "C" void APIENTRY Hook_glDepthMask(GLboolean a){ FLUSH_STATE(); glDepthMask(a); }
extern "C" void APIENTRY Hook_glStencilOp(GLenum a,GLenum b,GLenum c){ FLUSH_STATE(); glStencilOp(a,b,c); }
extern "C" void APIENTRY Hook_glTexEnvf(GLenum a,GLenum b,GLfloat c){ FLUSH_STATE(); glTexEnvf(a,b,c); }
extern "C" void APIENTRY Hook_glTexEnvi(GLenum a,GLenum b,GLint c){ FLUSH_STATE(); glTexEnvi(a,b,c); }
extern "C" void APIENTRY Hook_glTexParameteri(GLenum a,GLenum b,GLint c){ FLUSH_STATE(); glTexParameteri(a,b,c); }
extern "C" void APIENTRY Hook_glTexImage2D(GLenum a,GLint b,GLint c,GLsizei d,GLsizei e,GLint f,GLenum g,GLenum h,const GLvoid* i){ FLUSH_STATE(); glTexImage2D(a,b,c,d,e,f,g,h,i); }
extern "C" void APIENTRY Hook_glDeleteTextures(GLsizei n,const GLuint* t){ FLUSH_STATE(); glDeleteTextures(n,t); }
extern "C" void APIENTRY Hook_glMatrixMode(GLenum a){ FLUSH_STATE(); glMatrixMode(a); }
extern "C" void APIENTRY Hook_glPushMatrix(){ FLUSH_STATE(); glPushMatrix(); }
extern "C" void APIENTRY Hook_glPopMatrix(){ FLUSH_STATE(); glPopMatrix(); }
extern "C" void APIENTRY Hook_glLoadIdentity(){ FLUSH_STATE(); glLoadIdentity(); }
extern "C" void APIENTRY Hook_glTranslatef(GLfloat a,GLfloat b,GLfloat c){ FLUSH_STATE(); glTranslatef(a,b,c); }
extern "C" void APIENTRY Hook_glRotatef(GLfloat a,GLfloat b,GLfloat c,GLfloat d){ FLUSH_STATE(); glRotatef(a,b,c,d); }
extern "C" void APIENTRY Hook_glViewport(GLint a,GLint b,GLsizei c,GLsizei d){ FLUSH_STATE(); glViewport(a,b,c,d); }
extern "C" void APIENTRY Hook_glFogf(GLenum a,GLfloat b){ FLUSH_STATE(); glFogf(a,b); }
extern "C" void APIENTRY Hook_glFogfv(GLenum a,const GLfloat* b){ FLUSH_STATE(); glFogfv(a,b); }
extern "C" void APIENTRY Hook_glFogi(GLenum a,GLint b){ FLUSH_STATE(); glFogi(a,b); }
extern "C" void APIENTRY Hook_glVertexPointer(GLint a,GLenum b,GLsizei c,const GLvoid* d){ FLUSH_STATE(); glVertexPointer(a,b,c,d); }
extern "C" void APIENTRY Hook_glTexCoordPointer(GLint a,GLenum b,GLsizei c,const GLvoid* d){ FLUSH_STATE(); glTexCoordPointer(a,b,c,d); }
extern "C" void APIENTRY Hook_glColorPointer(GLint a,GLenum b,GLsizei c,const GLvoid* d){ FLUSH_STATE(); glColorPointer(a,b,c,d); }
extern "C" void APIENTRY Hook_glEnableClientState(GLenum a){ FLUSH_STATE(); glEnableClientState(a); }
extern "C" void APIENTRY Hook_glDisableClientState(GLenum a){ FLUSH_STATE(); glDisableClientState(a); }
extern "C" void APIENTRY Hook_glDrawArrays(GLenum a,GLint b,GLsizei c){ FLUSH_STATE(); ++gDirectDrawArrays; glDrawArrays(a,b,c); }
extern "C" void APIENTRY Hook_glCallLists(GLsizei a,GLenum b,const GLvoid* c){ FLUSH_STATE(); glCallLists(a,b,c); }
extern "C" void APIENTRY Hook_glReadPixels(GLint a,GLint b,GLsizei c,GLsizei d,GLenum e,GLenum f,GLvoid* g){ FLUSH_STATE(); glReadPixels(a,b,c,d,e,f,g); }

extern "C" void APIENTRY Hook_glFlush()
{
    if(!gInsideBegin) FlushPending(true,false);
    if(gNoFlush){ ++gFlushSkipped; return; }
    glFlush();
}

extern "C" void APIENTRY Hook_glClear(GLbitfield m)
{
    if(!gInsideBegin) FlushPending(true,false);
    glClear(m);
    ++gFrame;
    if(gLogStats && (gFrame%300ull)==0ull)
    {
        unsigned long long saved = gCapturedBegins>gBatchDraws ? gCapturedBegins-gBatchDraws : 0;
        Log("frame=%llu begins=%llu capturedBegins=%llu capturedVerts=%llu batchDraws=%llu savedDraws=%llu stateFlush=%llu swapFlush=%llu directArrays=%llu flushSkipped=%llu\r\n",
            gFrame,gBeginSeen,gCapturedBegins,gCapturedVerts,gBatchDraws,saved,gStateFlushes,gSwapFlushes,gDirectDrawArrays,gFlushSkipped);
    }
}

extern "C" BOOL WINAPI Hook_SwapBuffers(HDC dc)
{
    if(!gInsideBegin) FlushPending(false,true);
    return ::SwapBuffers(dc);
}

struct HookItem{const char* dll;const char* name;void* fn;};

static bool PatchOneImport(const char* dllWanted,const char* procWanted,void* hook)
{
    HMODULE m=GetModuleHandleA(nullptr); if(!m)return false;
    auto base=(unsigned char*)m; auto dos=(IMAGE_DOS_HEADER*)base; if(dos->e_magic!=IMAGE_DOS_SIGNATURE)return false;
    auto nt=(IMAGE_NT_HEADERS*)(base+dos->e_lfanew); if(nt->Signature!=IMAGE_NT_SIGNATURE)return false;
    const auto& dir=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT]; if(!dir.VirtualAddress)return false;
    auto imp=(IMAGE_IMPORT_DESCRIPTOR*)(base+dir.VirtualAddress);
    for(;imp->Name;++imp)
    {
        const char* dn=(const char*)(base+imp->Name); if(_stricmp(dn,dllWanted))continue; if(!imp->OriginalFirstThunk)continue;
        auto o=(IMAGE_THUNK_DATA*)(base+imp->OriginalFirstThunk); auto t=(IMAGE_THUNK_DATA*)(base+imp->FirstThunk);
        for(;o->u1.AddressOfData;++o,++t)
        {
            if(IMAGE_SNAP_BY_ORDINAL(o->u1.Ordinal))continue;
            auto bn=(IMAGE_IMPORT_BY_NAME*)(base+o->u1.AddressOfData); if(strcmp((const char*)bn->Name,procWanted))continue;
            DWORD old=0; if(!VirtualProtect(&t->u1.Function,sizeof(t->u1.Function),PAGE_EXECUTE_READWRITE,&old))return false;
            t->u1.Function=(ULONG_PTR)hook; DWORD z=0; VirtualProtect(&t->u1.Function,sizeof(t->u1.Function),old,&z);
            FlushInstructionCache(GetCurrentProcess(),&t->u1.Function,sizeof(t->u1.Function)); return true;
        }
    }
    return false;
}

static DWORD WINAPI InitThread(LPVOID)
{
    Sleep(300);
    std::string d=ModuleDir(); gLogPath=d+"MuCrowdFPS.log"; std::string ini=d+"MuCrowdFPS.ini";
    gEnabled=GetPrivateProfileIntA("CrowdFPS","DeferredBatching",1,ini.c_str())!=0;
    gNoFlush=GetPrivateProfileIntA("CrowdFPS","NoFlush",1,ini.c_str())!=0;
    gLogStats=GetPrivateProfileIntA("CrowdFPS","LogStats",1,ini.c_str())!=0;
    gMaxPendingVertices=(unsigned)GetPrivateProfileIntA("CrowdFPS","MaxPendingVertices",131072,ini.c_str());
    if(gMaxPendingVertices<4096) gMaxPendingVertices=4096;
    if(gMaxPendingVertices>524288) gMaxPendingVertices=524288;
    DeleteFileA(gLogPath.c_str());
    Log("MuCrowdFPS v3 - deferred mesh batcher\r\n");
    Log("DeferredBatching=%d MaxPendingVertices=%u NoFlush=%d LogStats=%d\r\n",(int)gEnabled,gMaxPendingVertices,(int)gNoFlush,(int)gLogStats);

    HookItem hs[]={
        {"OPENGL32.dll","glBegin",(void*)&Hook_glBegin},{"OPENGL32.dll","glEnd",(void*)&Hook_glEnd},
        {"OPENGL32.dll","glVertex2f",(void*)&Hook_glVertex2f},{"OPENGL32.dll","glVertex3f",(void*)&Hook_glVertex3f},{"OPENGL32.dll","glVertex3fv",(void*)&Hook_glVertex3fv},{"OPENGL32.dll","glVertex4fv",(void*)&Hook_glVertex4fv},
        {"OPENGL32.dll","glTexCoord2f",(void*)&Hook_glTexCoord2f},{"OPENGL32.dll","glTexCoord2fv",(void*)&Hook_glTexCoord2fv},
        {"OPENGL32.dll","glColor3f",(void*)&Hook_glColor3f},{"OPENGL32.dll","glColor4f",(void*)&Hook_glColor4f},{"OPENGL32.dll","glColor3fv",(void*)&Hook_glColor3fv},{"OPENGL32.dll","glColor4fv",(void*)&Hook_glColor4fv},{"OPENGL32.dll","glColor3ub",(void*)&Hook_glColor3ub},{"OPENGL32.dll","glColor4ub",(void*)&Hook_glColor4ub},
        {"OPENGL32.dll","glEnable",(void*)&Hook_glEnable},{"OPENGL32.dll","glDisable",(void*)&Hook_glDisable},{"OPENGL32.dll","glBindTexture",(void*)&Hook_glBindTexture},{"OPENGL32.dll","glBlendFunc",(void*)&Hook_glBlendFunc},{"OPENGL32.dll","glAlphaFunc",(void*)&Hook_glAlphaFunc},{"OPENGL32.dll","glDepthFunc",(void*)&Hook_glDepthFunc},{"OPENGL32.dll","glDepthMask",(void*)&Hook_glDepthMask},{"OPENGL32.dll","glStencilOp",(void*)&Hook_glStencilOp},
        {"OPENGL32.dll","glTexEnvf",(void*)&Hook_glTexEnvf},{"OPENGL32.dll","glTexEnvi",(void*)&Hook_glTexEnvi},{"OPENGL32.dll","glTexParameteri",(void*)&Hook_glTexParameteri},{"OPENGL32.dll","glTexImage2D",(void*)&Hook_glTexImage2D},{"OPENGL32.dll","glDeleteTextures",(void*)&Hook_glDeleteTextures},
        {"OPENGL32.dll","glMatrixMode",(void*)&Hook_glMatrixMode},{"OPENGL32.dll","glPushMatrix",(void*)&Hook_glPushMatrix},{"OPENGL32.dll","glPopMatrix",(void*)&Hook_glPopMatrix},{"OPENGL32.dll","glLoadIdentity",(void*)&Hook_glLoadIdentity},{"OPENGL32.dll","glTranslatef",(void*)&Hook_glTranslatef},{"OPENGL32.dll","glRotatef",(void*)&Hook_glRotatef},{"OPENGL32.dll","glViewport",(void*)&Hook_glViewport},
        {"OPENGL32.dll","glFogf",(void*)&Hook_glFogf},{"OPENGL32.dll","glFogfv",(void*)&Hook_glFogfv},{"OPENGL32.dll","glFogi",(void*)&Hook_glFogi},
        {"OPENGL32.dll","glVertexPointer",(void*)&Hook_glVertexPointer},{"OPENGL32.dll","glTexCoordPointer",(void*)&Hook_glTexCoordPointer},{"OPENGL32.dll","glColorPointer",(void*)&Hook_glColorPointer},{"OPENGL32.dll","glEnableClientState",(void*)&Hook_glEnableClientState},{"OPENGL32.dll","glDisableClientState",(void*)&Hook_glDisableClientState},{"OPENGL32.dll","glDrawArrays",(void*)&Hook_glDrawArrays},
        {"OPENGL32.dll","glCallLists",(void*)&Hook_glCallLists},{"OPENGL32.dll","glReadPixels",(void*)&Hook_glReadPixels},{"OPENGL32.dll","glFlush",(void*)&Hook_glFlush},{"OPENGL32.dll","glClear",(void*)&Hook_glClear},
        {"GDI32.dll","SwapBuffers",(void*)&Hook_SwapBuffers}
    };

    for(auto& h:hs)
    {
        if(PatchOneImport(h.dll,h.name,h.fn)){ ++gHooked; Log("hooked %s!%s\r\n",h.dll,h.name); }
        else Log("not-found %s!%s\r\n",h.dll,h.name);
    }
    Log("IAT hooks installed=%llu\r\n",gHooked);
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE h,DWORD r,LPVOID)
{
    if(r==DLL_PROCESS_ATTACH)
    {
        gSelf=h; DisableThreadLibraryCalls(h);
        HANDLE t=CreateThread(nullptr,0,InitThread,nullptr,0,nullptr); if(t)CloseHandle(t);
    }
    return TRUE;
}
