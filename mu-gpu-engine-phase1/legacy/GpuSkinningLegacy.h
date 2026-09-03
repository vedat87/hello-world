#pragma once

#include <cstddef>
#include <cstdint>

namespace MuGpuSkin
{
    struct SkinnedVertex
    {
        float px, py, pz;
        float nx, ny, nz;
        float u, v;
        float positionBone;
        float normalBone;
    };

    struct SkinningState
    {
        const float* boneRows;      // contiguous [bone][3][4]
        int boneCount;
        std::uint32_t paletteSerial; // increment once for each BMD::Transform call
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
        int texCoordMode;           // 0 mesh, 1 chrome, 2 chrome2 ... 8 oil, 9 metal/default chrome
    };

    // Works on the existing Windows compatibility OpenGL context. It does not
    // replace Main.exe's window/context creation and does not require DirectX.
    bool Initialize();
    bool IsAvailable();
    const char* LastError();

    bool HasMesh(const void* meshKey);
    bool UploadMesh(const void* meshKey, const SkinnedVertex* vertices, std::size_t vertexCount);
    bool DrawMesh(const void* meshKey, const SkinningState& state);
    void ReleaseMesh(const void* meshKey);
    void Shutdown();
}
