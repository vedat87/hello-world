#define MUGPUENGINE_EXPORTS
#include "MuGpuEngineExports.h"
#include "GpuSkinningLegacy.h"

namespace
{
    static_assert(sizeof(MuGpuSkinnedVertex) == sizeof(MuGpuSkin::SkinnedVertex), "vertex ABI mismatch");
}

int MuGpu_Initialize()
{
    return MuGpuSkin::Initialize() ? 1 : 0;
}

int MuGpu_IsAvailable()
{
    return MuGpuSkin::IsAvailable() ? 1 : 0;
}

const char* MuGpu_LastError()
{
    return MuGpuSkin::LastError();
}

int MuGpu_HasMesh(const void* meshKey)
{
    return MuGpuSkin::HasMesh(meshKey) ? 1 : 0;
}

int MuGpu_UploadMesh(const void* meshKey, const MuGpuSkinnedVertex* vertices, std::size_t vertexCount)
{
    const auto* p = reinterpret_cast<const MuGpuSkin::SkinnedVertex*>(vertices);
    return MuGpuSkin::UploadMesh(meshKey, p, vertexCount) ? 1 : 0;
}

int MuGpu_DrawMesh(const void* meshKey, const MuGpuSkinningState* s)
{
    if (!s) return 0;
    MuGpuSkin::SkinningState st{};
    st.boneRows = s->boneRows;
    st.boneCount = s->boneCount;
    st.bodyOrigin[0] = s->bodyOrigin[0]; st.bodyOrigin[1] = s->bodyOrigin[1]; st.bodyOrigin[2] = s->bodyOrigin[2];
    st.bodyScale = s->bodyScale;
    st.boneScale = s->boneScale;
    st.restPoseScale = s->restPoseScale;
    st.lightDirection[0] = s->lightDirection[0]; st.lightDirection[1] = s->lightDirection[1]; st.lightDirection[2] = s->lightDirection[2];
    st.bodyLight[0] = s->bodyLight[0]; st.bodyLight[1] = s->bodyLight[1]; st.bodyLight[2] = s->bodyLight[2];
    st.alpha = s->alpha;
    st.texOffset[0] = s->texOffset[0]; st.texOffset[1] = s->texOffset[1];
    st.chromeWave = s->chromeWave;
    st.chromeWave2 = s->chromeWave2;
    st.chromeLight[0] = s->chromeLight[0]; st.chromeLight[1] = s->chromeLight[1];
    st.chromeTimeTerm = s->chromeTimeTerm;
    st.translate = s->translate;
    st.lightEnabled = s->lightEnabled;
    st.texCoordMode = s->texCoordMode;
    return MuGpuSkin::DrawMesh(meshKey, st) ? 1 : 0;
}

void MuGpu_ReleaseMesh(const void* meshKey)
{
    MuGpuSkin::ReleaseMesh(meshKey);
}

void MuGpu_Shutdown()
{
    MuGpuSkin::Shutdown();
}
