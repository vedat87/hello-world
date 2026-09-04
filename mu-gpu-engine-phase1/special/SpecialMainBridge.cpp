#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>
#include <cstdint>
#include <cstddef>
#include <cmath>
#include <unordered_map>
#include <vector>
#include <string>
#include <cstdio>
#include <cstring>
#include <cstdarg>
#include "GpuSkinningLegacy.h"

#pragma pack(push,4)
struct VertexSpecial { short Node; short _pad; float Position[3]; };
struct NormalSpecial { short Node; short _pad; float Normal[3]; short BindVertex; short _pad2; };
struct TexCoordSpecial { float u, v; };
struct TriangleSpecial
{
    signed char Polygon;
    unsigned char _pad0;
    short VertexIndex[4];
    short NormalIndex[4];
    short TexCoordIndex[4];
    short EdgeTriangleIndex[4];
    unsigned char Front;
    unsigned char _pad1[1];
};
struct MeshSpecial
{
    unsigned char NoneBlendMesh;
    unsigned char _pad0;
    short Texture;
    short NumVertices;
    short NumNormals;
    short NumTexCoords;
    short NumTriangles;
    int NumCommandBytes;
    VertexSpecial* Vertices;
    NormalSpecial* Normals;
    TexCoordSpecial* TexCoords;
    TriangleSpecial* Triangles;
    unsigned char* Commands;
    void* TextureScript;
    unsigned char _specialTail[0x4c - 0x28];
};
struct BMDSpecial
{
    char Name[32];
    char Version;
    char _pad21;
    short NumBones;
    short NumMeshs;
    short NumActions;
    MeshSpecial* Meshs;
    void* Bones;
    void* Actions;
    void* Textures;
    unsigned int* IndexTexture;
    short NumLightMaps;
    short IndexLightMap;
    void* LightMaps;
    unsigned char LightEnable;
    unsigned char ContrastEnable;
    unsigned char _pad46[2];
    float BodyLight[3];
    int BoneHead;
    int BoneFoot[4];
    float BodyScale;
    float BodyOrigin[3];
    float BodyAngle[3];
    float BodyHeight;
    signed char StreamMesh;
    unsigned char _pad89[3];
    float ShadowAngle[3];
    signed char Skin;
    unsigned char HideSkin;
    unsigned char _pad9a[2];
    float Velocity;
    unsigned short CurrentAction;
    unsigned short PriorAction;
    float CurrentAnimation;
    short CurrentAnimationFrame;
    short Sounds[10];
    unsigned char _padbe[2];
    int renderCount;
    float fTransformedSize;
    unsigned int sequenceId;
    unsigned char bLightMap;
    unsigned char bOffLight;
    signed char iBillType;
    unsigned char completed;
};
#pragma pack(pop)

static_assert(sizeof(VertexSpecial)==0x10, "Vertex layout");
static_assert(sizeof(NormalSpecial)==0x14, "Normal layout");
static_assert(sizeof(TexCoordSpecial)==0x08, "Texcoord layout");
static_assert(sizeof(TriangleSpecial)==0x24, "Triangle layout");
static_assert(sizeof(MeshSpecial)==0x4c, "Mesh layout");
static_assert(offsetof(MeshSpecial, Triangles)==0x1c, "Mesh triangle offset");
static_assert(offsetof(MeshSpecial, TextureScript)==0x24, "Mesh script offset");
static_assert(offsetof(BMDSpecial, Meshs)==0x28, "BMD mesh offset");
static_assert(offsetof(BMDSpecial, LightEnable)==0x44, "BMD light offset");
static_assert(offsetof(BMDSpecial, BodyScale)==0x68, "BMD scale offset");
static_assert(offsetof(BMDSpecial, BodyOrigin)==0x6c, "BMD origin offset");
static_assert(offsetof(BMDSpecial, ShadowAngle)==0x8c, "BMD shadow offset");
static_assert(offsetof(BMDSpecial, fTransformedSize)==0xc4, "BMD transformed size offset");

namespace
{
    constexpr std::uintptr_t kImageBase = 0x00400000u;
    constexpr std::uintptr_t kTransform = 0x0077A5F0u;
    constexpr std::uintptr_t kRenderMesh = 0x00778DF0u;
    constexpr std::uintptr_t kBoneScale = 0x013A735Cu;
    constexpr std::uintptr_t kHighLight = 0x013A7358u;
    constexpr std::uintptr_t kMapManager = 0x00D14CD8u;
    constexpr std::uintptr_t kInBattleCastle = 0x00595940u;
    constexpr std::uintptr_t kGetTexture = 0x0050A9B0u;
    constexpr std::uintptr_t kBitmaps = 0x0A082680u;
    constexpr std::uintptr_t kBindTexture = 0x008BA610u;
    constexpr std::uintptr_t kDisableAlphaBlend = 0x008BAD90u;

    constexpr int RENDER_TEXTURE = 0x00000002;
    constexpr int BITMAP_WATER = 0x766e;
    constexpr int BITMAP_SKIN  = 0x7725;
    constexpr int BITMAP_HAIR  = 0x773a;
    constexpr int BITMAP_HIDE  = 0x775e;

    typedef void (__thiscall* TransformFn)(BMDSpecial*, float (*)[3][4], float*, float*, void*, bool, float);
    typedef void (__thiscall* RenderMeshFn)(BMDSpecial*, int, int, float, int, float, float, float, int);
    typedef bool (__thiscall* InBattleCastleFn)(void*, int);
    typedef void* (__thiscall* GetTextureFn)(void*, int);
    typedef void (__cdecl* BindTextureFn)(int);
    typedef void (__cdecl* VoidFn)();

    TransformFn gOriginalTransform = nullptr;
    RenderMeshFn gOriginalRenderMesh = nullptr;
    bool gInstalled = false;
    bool gConfigRead = false;
    bool gEnabled = true;
    bool gGpuMeshes = true;
    bool gForceVsyncOff = true;
    bool gVsyncDone = false;
    bool gGpuAnnounced = false;
    std::uint32_t gSerial = 1;
    std::uint64_t gGpuDraws = 0;
    std::uint64_t gFallbackDraws = 0;

    struct Record
    {
        float (*bones)[3][4] = nullptr;
        int boneCount = 0;
        bool translate = false;
        float restScale = 0.0f;
        float boneScale = 1.0f;
        float lightDir[3] = {0.f,-1.5f,0.f};
        std::uint32_t serial = 0;
    };
    std::unordered_map<BMDSpecial*, Record> gRecords;

    std::string ModuleDir()
    {
        char path[MAX_PATH] = {};
        GetModuleFileNameA(nullptr, path, MAX_PATH);
        char* slash = std::strrchr(path, '\\');
        if (slash) *(slash + 1) = '\0';
        else path[0] = '\0';
        return std::string(path);
    }

    void Log(const char* fmt, ...)
    {
        char buf[1024];
        va_list ap;
        va_start(ap, fmt);
        _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, ap);
        va_end(ap);
        std::string path = ModuleDir() + "MuGPU.log";
        FILE* f = nullptr;
        fopen_s(&f, path.c_str(), "a");
        if (f)
        {
            SYSTEMTIME st; GetLocalTime(&st);
            std::fprintf(f, "[%02u:%02u:%02u] %s\n", st.wHour, st.wMinute, st.wSecond, buf);
            std::fclose(f);
        }
    }

    void ReadConfig()
    {
        if (gConfigRead) return;
        gConfigRead = true;
        std::string ini = ModuleDir() + "MuGPU.ini";
        gEnabled = GetPrivateProfileIntA("MuGPU", "Enabled", 1, ini.c_str()) != 0;
        gGpuMeshes = GetPrivateProfileIntA("MuGPU", "GpuMeshes", 1, ini.c_str()) != 0;
        gForceVsyncOff = GetPrivateProfileIntA("MuGPU", "ForceVSyncOff", 1, ini.c_str()) != 0;
        Log("Special Main bridge active. Enabled=%d GpuMeshes=%d ForceVSyncOff=%d", (int)gEnabled, (int)gGpuMeshes, (int)gForceVsyncOff);
    }

    void TryDisableVsync()
    {
        if (gVsyncDone || !gForceVsyncOff || !wglGetCurrentContext()) return;
        typedef BOOL (WINAPI* SwapIntervalFn)(int);
        auto fn = reinterpret_cast<SwapIntervalFn>(wglGetProcAddress("wglSwapIntervalEXT"));
        if (fn)
        {
            fn(0);
            Log("wglSwapIntervalEXT(0) applied.");
        }
        gVsyncDone = true;
    }

    void AngleMatrix(const float a[3], float m[3][4])
    {
        const float k = 6.2831853071795864769f / 360.0f;
        float sy=sinf(a[2]*k), cy=cosf(a[2]*k);
        float sp=sinf(a[1]*k), cp=cosf(a[1]*k);
        float sr=sinf(a[0]*k), cr=cosf(a[0]*k);
        m[0][0]=cp*cy; m[1][0]=cp*sy; m[2][0]=-sp;
        m[0][1]=sr*sp*cy+cr*-sy; m[1][1]=sr*sp*sy+cr*cy; m[2][1]=sr*cp;
        m[0][2]=cr*sp*cy+-sr*-sy; m[1][2]=cr*sp*sy+-sr*cy; m[2][2]=cr*cp;
        m[0][3]=m[1][3]=m[2][3]=0.f;
    }

    void ComputeLightDir(BMDSpecial* bmd, float out[3])
    {
        float p[3] = {0.f,-1.5f,0.f};
        __try
        {
            if (*reinterpret_cast<volatile unsigned char*>(kHighLight))
            {
                p[0]=1.3f; p[1]=0.f; p[2]=2.f;
            }
            else
            {
                auto inBattle = reinterpret_cast<InBattleCastleFn>(kInBattleCastle);
                if (inBattle(reinterpret_cast<void*>(kMapManager), -1))
                {
                    p[0]=0.5f; p[1]=-1.f; p[2]=1.f;
                }
            }
        }
        __except(EXCEPTION_EXECUTE_HANDLER)
        {
            p[0]=0.f; p[1]=-1.5f; p[2]=0.f;
        }
        float m[3][4];
        AngleMatrix(bmd->ShadowAngle, m);
        out[0]=p[0]*m[0][0]+p[1]*m[1][0]+p[2]*m[2][0];
        out[1]=p[0]*m[0][1]+p[1]*m[1][1]+p[2]*m[2][1];
        out[2]=p[0]*m[0][2]+p[1]*m[1][2]+p[2]*m[2][2];
    }

    struct CornerKey
    {
        int vi, ni, ti;
        bool operator==(const CornerKey& o) const { return vi==o.vi && ni==o.ni && ti==o.ti; }
    };
    struct CornerHash
    {
        std::size_t operator()(const CornerKey& k) const
        {
            std::size_t h=static_cast<std::size_t>(k.vi)*73856093u;
            h^=static_cast<std::size_t>(k.ni)*19349663u;
            h^=static_cast<std::size_t>(k.ti)*83492791u;
            return h;
        }
    };

    bool BuildGpuMesh(MeshSpecial* m, int boneCount)
    {
        if (!m || m->NumTriangles<=0 || !m->Vertices || !m->Normals || !m->TexCoords || !m->Triangles) return false;
        if (MuGpuSkin::HasMesh(m)) return true;
        if (m->NumVertices<=0 || m->NumNormals<=0 || m->NumTexCoords<=0) return false;

        std::vector<MuGpuSkin::SkinnedVertex> verts;
        std::vector<std::uint32_t> inds;
        std::unordered_map<CornerKey,std::uint32_t,CornerHash> remap;
        verts.reserve(static_cast<std::size_t>(m->NumVertices));
        inds.reserve(static_cast<std::size_t>(m->NumTriangles)*3u);
        remap.reserve(static_cast<std::size_t>(m->NumTriangles)*2u);

        for (int t=0;t<m->NumTriangles;++t)
        {
            TriangleSpecial& tri=m->Triangles[t];
            const int poly=tri.Polygon;
            if (poly<=0 || poly>4) return false;
            for (int k=0;k<poly;++k)
            {
                int vi=tri.VertexIndex[k], ni=tri.NormalIndex[k], ti=tri.TexCoordIndex[k];
                if (vi<0 || vi>=m->NumVertices || ni<0 || ni>=m->NumNormals || ti<0 || ti>=m->NumTexCoords) return false;
                VertexSpecial& vv=m->Vertices[vi];
                NormalSpecial& nn=m->Normals[ni];
                if (vv.Node<0 || vv.Node>=boneCount || nn.Node<0 || nn.Node>=boneCount) return false;
                CornerKey key{vi,ni,ti};
                auto it=remap.find(key);
                if (it!=remap.end()) { inds.push_back(it->second); continue; }
                TexCoordSpecial& uv=m->TexCoords[ti];
                MuGpuSkin::SkinnedVertex gv{};
                gv.px=vv.Position[0]; gv.py=vv.Position[1]; gv.pz=vv.Position[2];
                gv.nx=nn.Normal[0]; gv.ny=nn.Normal[1]; gv.nz=nn.Normal[2];
                gv.u=uv.u; gv.v=uv.v;
                gv.positionBone=static_cast<float>(vv.Node);
                gv.normalBone=static_cast<float>(nn.Node);
                auto idx=static_cast<std::uint32_t>(verts.size());
                verts.push_back(gv); remap.emplace(key,idx); inds.push_back(idx);
            }
        }
        return MuGpuSkin::UploadIndexedMesh(m, verts.data(), verts.size(), inds.data(), inds.size());
    }

    bool IsOpaqueTexture(int texture)
    {
        __try
        {
            auto getTexture=reinterpret_cast<GetTextureFn>(kGetTexture);
            void* b=getTexture(reinterpret_cast<void*>(kBitmaps), texture);
            if (!b) return false;
            unsigned char components=*reinterpret_cast<unsigned char*>(reinterpret_cast<unsigned char*>(b)+0x10c);
            return components!=4;
        }
        __except(EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool TryGpuRender(BMDSpecial* self,int i,int renderFlag,float alpha,int blendMesh,float blendLight,float u,float v,int meshTexture)
    {
        (void)blendLight;
        if (!gEnabled || !gGpuMeshes || !self || !wglGetCurrentContext()) return false;
        if (renderFlag!=RENDER_TEXTURE || alpha<0.99f || blendMesh!=-1 || meshTexture!=-1 || u!=0.f || v!=0.f) return false;
        if (i<0 || i>=self->NumMeshs || !self->Meshs || !self->IndexTexture) return false;
        if (self->NumBones<=0 || self->NumBones>200) return false;

        MeshSpecial* m=&self->Meshs[i];
        if (m->NumTriangles<=0 || m->TextureScript!=nullptr) return false;

        int texture=self->IndexTexture[m->Texture];
        if (texture==BITMAP_HIDE || texture==BITMAP_SKIN || texture==BITMAP_WATER || texture==BITMAP_HAIR) return false;
        if (!IsOpaqueTexture(texture)) return false;

        auto rit=gRecords.find(self);
        if (rit==gRecords.end() || !rit->second.bones) return false;
        Record& rec=rit->second;

        if (!MuGpuSkin::IsAvailable())
        {
            if (!gGpuAnnounced)
            {
                gGpuAnnounced=true;
                Log("GPU path unavailable: %s", MuGpuSkin::LastError());
            }
            return false;
        }
        if (!gGpuAnnounced)
        {
            gGpuAnnounced=true;
            Log("GPU skinning active: indexed VBO/IBO + GLSL 1.20 bone texture.");
        }
        if (!BuildGpuMesh(m,self->NumBones)) return false;

        reinterpret_cast<BindTextureFn>(kBindTexture)(texture);
        reinterpret_cast<VoidFn>(kDisableAlphaBlend)();

        MuGpuSkin::SkinningState st{};
        st.boneRows=&rec.bones[0][0][0];
        st.boneCount=rec.boneCount;
        st.paletteSerial=rec.serial;
        st.bodyOrigin[0]=self->BodyOrigin[0]; st.bodyOrigin[1]=self->BodyOrigin[1]; st.bodyOrigin[2]=self->BodyOrigin[2];
        st.bodyScale=self->BodyScale;
        st.boneScale=rec.boneScale;
        st.restPoseScale=rec.restScale;
        st.lightDirection[0]=rec.lightDir[0]; st.lightDirection[1]=rec.lightDir[1]; st.lightDirection[2]=rec.lightDir[2];
        st.bodyLight[0]=self->BodyLight[0]; st.bodyLight[1]=self->BodyLight[1]; st.bodyLight[2]=self->BodyLight[2];
        st.alpha=1.f;
        st.texOffset[0]=0.f; st.texOffset[1]=0.f;
        st.chromeWave=0.f; st.chromeWave2=0.f; st.chromeLight[0]=0.f; st.chromeLight[1]=0.f; st.chromeTimeTerm=0.f;
        st.translate=rec.translate?1:0;
        st.lightEnabled=(self->LightEnable && i!=self->StreamMesh)?1:0;
        st.texCoordMode=0;

        if (!MuGpuSkin::DrawMesh(m,st)) return false;
        glColor3fv(self->BodyLight);
        ++gGpuDraws;
        if ((gGpuDraws % 10000u)==0u) Log("GPU draws=%llu fallback=%llu records=%u",
            static_cast<unsigned long long>(gGpuDraws), static_cast<unsigned long long>(gFallbackDraws), static_cast<unsigned>(gRecords.size()));
        return true;
    }

    void* MakeTrampoline(void* target, std::size_t n)
    {
        unsigned char* mem=reinterpret_cast<unsigned char*>(VirtualAlloc(nullptr,n+5,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
        if (!mem) return nullptr;
        std::memcpy(mem,target,n);
        std::uintptr_t src=reinterpret_cast<std::uintptr_t>(mem)+n;
        std::uintptr_t dst=reinterpret_cast<std::uintptr_t>(target)+n;
        mem[n]=0xE9;
        *reinterpret_cast<std::int32_t*>(mem+n+1)=static_cast<std::int32_t>(dst-(src+5));
        FlushInstructionCache(GetCurrentProcess(),mem,n+5);
        return mem;
    }

    bool Hook(void* target, void* detour, std::size_t n, void** trampoline)
    {
        if (n<5) return false;
        *trampoline=MakeTrampoline(target,n);
        if (!*trampoline) return false;
        DWORD old=0;
        if (!VirtualProtect(target,n,PAGE_EXECUTE_READWRITE,&old)) return false;
        unsigned char* p=reinterpret_cast<unsigned char*>(target);
        p[0]=0xE9;
        *reinterpret_cast<std::int32_t*>(p+1)=static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(detour)-(reinterpret_cast<std::uintptr_t>(target)+5));
        for (std::size_t j=5;j<n;++j) p[j]=0x90;
        DWORD tmp=0; VirtualProtect(target,n,old,&tmp);
        FlushInstructionCache(GetCurrentProcess(),target,n);
        return true;
    }

    void __fastcall HookTransform(BMDSpecial* self, void*, float (*bones)[3][4], float* bbMin, float* bbMax, void* obb, bool translate, float restScale)
    {
        ReadConfig();
        gOriginalTransform(self,bones,bbMin,bbMax,obb,translate,restScale);
        if (!gEnabled || !self || !bones) return;
        Record& r=gRecords[self];
        r.bones=bones;
        r.boneCount=self->NumBones;
        r.translate=translate;
        r.restScale=restScale;
        __try { r.boneScale=*reinterpret_cast<volatile float*>(kBoneScale); }
        __except(EXCEPTION_EXECUTE_HANDLER) { r.boneScale=1.f; }
        ComputeLightDir(self,r.lightDir);
        r.serial=++gSerial;
    }

    void __fastcall HookRenderMesh(BMDSpecial* self, void*, int i, int renderFlag, float alpha, int blendMesh, float blendLight, float u, float v, int meshTexture)
    {
        ReadConfig();
        TryDisableVsync();
        if (TryGpuRender(self,i,renderFlag,alpha,blendMesh,blendLight,u,v,meshTexture)) return;
        ++gFallbackDraws;
        gOriginalRenderMesh(self,i,renderFlag,alpha,blendMesh,blendLight,u,v,meshTexture);
    }

    bool VerifyMain()
    {
        HMODULE h=GetModuleHandleA(nullptr);
        if (reinterpret_cast<std::uintptr_t>(h)!=kImageBase) return false;
        const unsigned char tSig[9]={0x55,0x8B,0xEC,0x81,0xEC,0x88,0x00,0x00,0x00};
        const unsigned char rSig[6]={0x55,0x8B,0xEC,0x83,0xEC,0x3C};
        return std::memcmp(reinterpret_cast<void*>(kTransform),tSig,sizeof(tSig))==0 &&
               std::memcmp(reinterpret_cast<void*>(kRenderMesh),rSig,sizeof(rSig))==0;
    }

    bool Install()
    {
        if (gInstalled) return true;
        if (!VerifyMain()) return false;
        void* tr1=nullptr; void* tr2=nullptr;
        if (!Hook(reinterpret_cast<void*>(kTransform),reinterpret_cast<void*>(&HookTransform),9,&tr1)) return false;
        gOriginalTransform=reinterpret_cast<TransformFn>(tr1);
        if (!Hook(reinterpret_cast<void*>(kRenderMesh),reinterpret_cast<void*>(&HookRenderMesh),6,&tr2)) return false;
        gOriginalRenderMesh=reinterpret_cast<RenderMeshFn>(tr2);
        gInstalled=true;
        return true;
    }
}

extern "C" __declspec(dllexport) int __stdcall MuGPU_Status()
{
    return gInstalled ? (MuGpuSkin::IsAvailable()?2:1) : 0;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID)
{
    if (reason==DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(h);
        Install();
    }
    return TRUE;
}
