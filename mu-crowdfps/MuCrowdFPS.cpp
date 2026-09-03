#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <gl/GL.h>
#include <vector>
#include <string>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <intrin.h>

#pragma comment(lib, "opengl32.lib")

static HMODULE gSelf = nullptr;
static bool gAdaptiveTriangles = true;
static bool gNoFlush = true;
static bool gLogStats = true;
static unsigned gMinVertices = 96;
static std::string gLogPath;

struct BatchVertex { GLfloat x,y,z,w; GLfloat r,g,b,a; GLfloat u,v; };
struct SiteInfo { void* site; unsigned lastCount; unsigned hits; bool large; };

thread_local bool gInsideBegin=false;
thread_local bool gCapturing=false;
thread_local GLenum gMode=0;
thread_local void* gSite=nullptr;
thread_local unsigned gPassCount=0;
thread_local GLfloat gColor[4]={1,1,1,1};
thread_local GLfloat gTex[2]={0,0};
thread_local std::vector<BatchVertex> gVertices;

static SiteInfo gSites[512] = {};
static CRITICAL_SECTION gSiteLock;
static unsigned long long gFrame=0, gBatches=0, gBatchedVerts=0, gPassBegins=0, gPassVerts=0, gFlushSkipped=0, gHooked=0;

static std::string ModuleDir(){ char p[MAX_PATH]={}; GetModuleFileNameA(gSelf,p,MAX_PATH); char* s=strrchr(p,'\\'); if(s) *(s+1)=0; return std::string(p); }
static void Log(const char* fmt,...){ if(gLogPath.empty()) return; char b[1024]={}; va_list ap; va_start(ap,fmt); _vsnprintf_s(b,sizeof(b),_TRUNCATE,fmt,ap); va_end(ap); HANDLE h=CreateFileA(gLogPath.c_str(),FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr); if(h==INVALID_HANDLE_VALUE) return; DWORD w=0; WriteFile(h,b,(DWORD)strlen(b),&w,nullptr); CloseHandle(h); }

static SiteInfo* GetSite(void* site){
    uintptr_t k=((uintptr_t)site>>2) & 511u;
    EnterCriticalSection(&gSiteLock);
    for(unsigned n=0;n<512;n++){
        SiteInfo* s=&gSites[(k+n)&511u];
        if(s->site==site || s->site==nullptr){ if(!s->site) s->site=site; LeaveCriticalSection(&gSiteLock); return s; }
    }
    LeaveCriticalSection(&gSiteLock); return nullptr;
}

static void RecordSite(void* site,unsigned count){
    SiteInfo* s=GetSite(site); if(!s) return;
    EnterCriticalSection(&gSiteLock);
    s->lastCount=count; if(s->hits<0xffffffffu) ++s->hits;
    if(count>=gMinVertices) s->large=true;
    else if(s->hits>8 && count < gMinVertices/2) s->large=false;
    LeaveCriticalSection(&gSiteLock);
}

static bool ShouldCapture(void* site, GLenum mode){
    if(!gAdaptiveTriangles || mode!=GL_TRIANGLES) return false;
    SiteInfo* s=GetSite(site); if(!s) return false;
    bool v=false; EnterCriticalSection(&gSiteLock); v=s->large; LeaveCriticalSection(&gSiteLock); return v;
}

static inline void AddVertex(GLfloat x,GLfloat y,GLfloat z,GLfloat w){ BatchVertex q={x,y,z,w,gColor[0],gColor[1],gColor[2],gColor[3],gTex[0],gTex[1]}; gVertices.push_back(q); }

extern "C" void APIENTRY Hook_glBegin(GLenum mode){
    gInsideBegin=true; gMode=mode; gSite=_ReturnAddress(); gPassCount=0;
    gCapturing=ShouldCapture(gSite,mode);
    if(gCapturing){ if(gVertices.capacity()<32768) gVertices.reserve(32768); gVertices.clear(); }
    else { ++gPassBegins; glBegin(mode); }
}

extern "C" void APIENTRY Hook_glEnd(){
    if(!gInsideBegin){ glEnd(); return; }
    if(!gCapturing){ glEnd(); RecordSite(gSite,gPassCount); }
    else if(!gVertices.empty()){
        const GLsizei stride=(GLsizei)sizeof(BatchVertex);
        glEnableClientState(GL_VERTEX_ARRAY); glEnableClientState(GL_COLOR_ARRAY); glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glVertexPointer(4,GL_FLOAT,stride,&gVertices[0].x);
        glColorPointer(4,GL_FLOAT,stride,&gVertices[0].r);
        glTexCoordPointer(2,GL_FLOAT,stride,&gVertices[0].u);
        glDrawArrays(GL_TRIANGLES,0,(GLsizei)gVertices.size());
        glDisableClientState(GL_TEXTURE_COORD_ARRAY); glDisableClientState(GL_COLOR_ARRAY); glDisableClientState(GL_VERTEX_ARRAY);
        glColor4f(gColor[0],gColor[1],gColor[2],gColor[3]); glTexCoord2f(gTex[0],gTex[1]);
        ++gBatches; gBatchedVerts += gVertices.size(); RecordSite(gSite,(unsigned)gVertices.size());
    }
    gInsideBegin=false; gCapturing=false; gSite=nullptr; gPassCount=0;
}

extern "C" void APIENTRY Hook_glVertex2f(GLfloat x,GLfloat y){ if(gCapturing){AddVertex(x,y,0,1);return;} if(gInsideBegin){++gPassCount;++gPassVerts;} glVertex2f(x,y); }
extern "C" void APIENTRY Hook_glVertex3f(GLfloat x,GLfloat y,GLfloat z){ if(gCapturing){AddVertex(x,y,z,1);return;} if(gInsideBegin){++gPassCount;++gPassVerts;} glVertex3f(x,y,z); }
extern "C" void APIENTRY Hook_glVertex3fv(const GLfloat* v){ if(gCapturing){AddVertex(v[0],v[1],v[2],1);return;} if(gInsideBegin){++gPassCount;++gPassVerts;} glVertex3fv(v); }
extern "C" void APIENTRY Hook_glVertex4fv(const GLfloat* v){ if(gCapturing){AddVertex(v[0],v[1],v[2],v[3]);return;} if(gInsideBegin){++gPassCount;++gPassVerts;} glVertex4fv(v); }
extern "C" void APIENTRY Hook_glTexCoord2f(GLfloat u,GLfloat v){ gTex[0]=u;gTex[1]=v; if(!gCapturing) glTexCoord2f(u,v); }
extern "C" void APIENTRY Hook_glTexCoord2fv(const GLfloat* t){ gTex[0]=t[0];gTex[1]=t[1]; if(!gCapturing) glTexCoord2fv(t); }
extern "C" void APIENTRY Hook_glColor3f(GLfloat r,GLfloat g,GLfloat b){ gColor[0]=r;gColor[1]=g;gColor[2]=b;gColor[3]=1; if(!gCapturing) glColor3f(r,g,b); }
extern "C" void APIENTRY Hook_glColor4f(GLfloat r,GLfloat g,GLfloat b,GLfloat a){ gColor[0]=r;gColor[1]=g;gColor[2]=b;gColor[3]=a; if(!gCapturing) glColor4f(r,g,b,a); }
extern "C" void APIENTRY Hook_glColor3fv(const GLfloat* c){ gColor[0]=c[0];gColor[1]=c[1];gColor[2]=c[2];gColor[3]=1; if(!gCapturing) glColor3fv(c); }
extern "C" void APIENTRY Hook_glColor4fv(const GLfloat* c){ gColor[0]=c[0];gColor[1]=c[1];gColor[2]=c[2];gColor[3]=c[3]; if(!gCapturing) glColor4fv(c); }
extern "C" void APIENTRY Hook_glColor3ub(GLubyte r,GLubyte g,GLubyte b){ gColor[0]=r/255.f;gColor[1]=g/255.f;gColor[2]=b/255.f;gColor[3]=1; if(!gCapturing) glColor3ub(r,g,b); }
extern "C" void APIENTRY Hook_glColor4ub(GLubyte r,GLubyte g,GLubyte b,GLubyte a){ gColor[0]=r/255.f;gColor[1]=g/255.f;gColor[2]=b/255.f;gColor[3]=a/255.f; if(!gCapturing) glColor4ub(r,g,b,a); }
extern "C" void APIENTRY Hook_glFlush(){ if(gNoFlush){++gFlushSkipped;return;} glFlush(); }
extern "C" void APIENTRY Hook_glClear(GLbitfield m){ glClear(m); ++gFrame; if(gLogStats && gFrame%300ull==0ull) Log("frame=%llu passBegins=%llu passVerts=%llu batches=%llu batchedVerts=%llu flushSkipped=%llu\r\n",gFrame,gPassBegins,gPassVerts,gBatches,gBatchedVerts,gFlushSkipped); }

struct HookItem{const char* name;void* fn;};
static bool PatchOneImport(const char* dllWanted,const char* procWanted,void* hook){ HMODULE m=GetModuleHandleA(nullptr); if(!m)return false; auto base=(unsigned char*)m; auto dos=(IMAGE_DOS_HEADER*)base; if(dos->e_magic!=IMAGE_DOS_SIGNATURE)return false; auto nt=(IMAGE_NT_HEADERS*)(base+dos->e_lfanew); if(nt->Signature!=IMAGE_NT_SIGNATURE)return false; const auto& dir=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT]; if(!dir.VirtualAddress)return false; auto imp=(IMAGE_IMPORT_DESCRIPTOR*)(base+dir.VirtualAddress); for(;imp->Name;++imp){const char* dn=(const char*)(base+imp->Name); if(_stricmp(dn,dllWanted))continue; if(!imp->OriginalFirstThunk)continue; auto o=(IMAGE_THUNK_DATA*)(base+imp->OriginalFirstThunk); auto t=(IMAGE_THUNK_DATA*)(base+imp->FirstThunk); for(;o->u1.AddressOfData;++o,++t){ if(IMAGE_SNAP_BY_ORDINAL(o->u1.Ordinal))continue; auto bn=(IMAGE_IMPORT_BY_NAME*)(base+o->u1.AddressOfData); if(strcmp((const char*)bn->Name,procWanted))continue; DWORD old=0; if(!VirtualProtect(&t->u1.Function,sizeof(t->u1.Function),PAGE_EXECUTE_READWRITE,&old))return false; t->u1.Function=(ULONG_PTR)hook; DWORD z=0;VirtualProtect(&t->u1.Function,sizeof(t->u1.Function),old,&z);FlushInstructionCache(GetCurrentProcess(),&t->u1.Function,sizeof(t->u1.Function)); return true; } } return false; }

static DWORD WINAPI InitThread(LPVOID){ Sleep(300); InitializeCriticalSection(&gSiteLock); std::string d=ModuleDir(); gLogPath=d+"MuCrowdFPS.log"; std::string ini=d+"MuCrowdFPS.ini"; gAdaptiveTriangles=GetPrivateProfileIntA("CrowdFPS","AdaptiveTriangles",1,ini.c_str())!=0; gNoFlush=GetPrivateProfileIntA("CrowdFPS","NoFlush",1,ini.c_str())!=0; gLogStats=GetPrivateProfileIntA("CrowdFPS","LogStats",1,ini.c_str())!=0; gMinVertices=(unsigned)GetPrivateProfileIntA("CrowdFPS","MinVertices",96,ini.c_str()); if(gMinVertices<12)gMinVertices=12; DeleteFileA(gLogPath.c_str()); Log("MuCrowdFPS v2 - adaptive large-triangle optimizer\r\n"); Log("AdaptiveTriangles=%d MinVertices=%u NoFlush=%d LogStats=%d\r\n",(int)gAdaptiveTriangles,gMinVertices,(int)gNoFlush,(int)gLogStats); HookItem hs[]={ {"glBegin",(void*)&Hook_glBegin},{"glEnd",(void*)&Hook_glEnd},{"glVertex2f",(void*)&Hook_glVertex2f},{"glVertex3f",(void*)&Hook_glVertex3f},{"glVertex3fv",(void*)&Hook_glVertex3fv},{"glVertex4fv",(void*)&Hook_glVertex4fv},{"glTexCoord2f",(void*)&Hook_glTexCoord2f},{"glTexCoord2fv",(void*)&Hook_glTexCoord2fv},{"glColor3f",(void*)&Hook_glColor3f},{"glColor4f",(void*)&Hook_glColor4f},{"glColor3fv",(void*)&Hook_glColor3fv},{"glColor4fv",(void*)&Hook_glColor4fv},{"glColor3ub",(void*)&Hook_glColor3ub},{"glColor4ub",(void*)&Hook_glColor4ub},{"glFlush",(void*)&Hook_glFlush},{"glClear",(void*)&Hook_glClear} }; for(auto& h:hs){if(PatchOneImport("OPENGL32.dll",h.name,h.fn)){++gHooked;Log("hooked %s\r\n",h.name);}else Log("not-found %s\r\n",h.name);} Log("IAT hooks installed=%llu\r\n",gHooked); return 0; }

BOOL WINAPI DllMain(HINSTANCE h,DWORD r,LPVOID){ if(r==DLL_PROCESS_ATTACH){gSelf=h;DisableThreadLibraryCalls(h);HANDLE t=CreateThread(nullptr,0,InitThread,nullptr,0,nullptr);if(t)CloseHandle(t);} return TRUE; }
