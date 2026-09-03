#pragma once

#include <cstddef>

#ifdef MUGPUENGINE_EXPORTS
#define MUGPU_API extern "C" __declspec(dllexport)
#else
#define MUGPU_API extern "C" __declspec(dllimport)
#endif

struct MuGpuSkinnedVertex
{
    float px, py, pz;
    float nx, ny, nz;
    float u, v;
    float positionBone;
    float normalBone;
};

struct MuGpuSkinningState
{
    const float* boneRows;
    int boneCount;
    float bodyOrigin[3];
    float bodyScale;
    float boneScale;
    float restPoseScale;
    float lightDirection[3];
    float bodyLight[3];
    float alpha;
    float texOffset[2];
    float chromeWave;
    float chromeWave2;
    float chromeLight[2];
    float chromeTimeTerm;
    int translate;
    int lightEnabled;
    int texCoordMode;
};

MUGPU_API int  MuGpu_Initialize();
MUGPU_API int  MuGpu_IsAvailable();
MUGPU_API const char* MuGpu_LastError();
MUGPU_API int  MuGpu_HasMesh(const void* meshKey);
MUGPU_API int  MuGpu_UploadMesh(const void* meshKey, const MuGpuSkinnedVertex* vertices, std::size_t vertexCount);
MUGPU_API int  MuGpu_DrawMesh(const void* meshKey, const MuGpuSkinningState* state);
MUGPU_API void MuGpu_ReleaseMesh(const void* meshKey);
MUGPU_API void MuGpu_Shutdown();
