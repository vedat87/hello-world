#pragma once

#include "ZzzBMD.h"

namespace BMDGpuBridge
{
    // Called from BMD::Transform after the legacy light-direction calculation.
    // Returns true when the expensive CPU vertex/normal loops may be skipped.
    bool BeginTransform(BMD* bmd,
                        float (*bones)[3][4],
                        vec3_t boundingBoxMin,
                        vec3_t boundingBoxMax,
                        OBB_t* obb,
                        bool translate,
                        float restPoseScale,
                        float boneScale,
                        const vec3_t lightDirection);

    bool IsActive(BMD* bmd);
    void EnsureCpuVertices(BMD* bmd, int meshIndex = -1);
    void EnsureCpuNormals(BMD* bmd, int meshIndex = -1);

    // Call after the original RenderMesh code has selected/bound the texture and
    // configured blend/depth/alpha state, but before the legacy client arrays are built.
    bool TryDraw(BMD* bmd,
                 int meshIndex,
                 int renderFlags,
                 int finalRenderFlags,
                 float alpha,
                 float texOffsetU,
                 float texOffsetV,
                 bool lightEnabled);

    void ReleaseBMD(BMD* bmd);
    void Shutdown();
}
