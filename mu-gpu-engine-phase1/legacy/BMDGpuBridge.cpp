#include "stdafx.h"
#include "BMDGpuBridge.h"
#include "GpuSkinningLegacy.h"

#include <unordered_map>
#include <vector>
#include <cstdint>
#include <cmath>
#include <cstring>

extern double WorldTime;

namespace
{
    struct TransformRecord
    {
        float (*bones)[3][4] = nullptr;
        bool translate = false;
        float restPoseScale = 0.0f;
        float boneScale = 1.0f;
        float bodyScale = 1.0f;
        vec3_t bodyOrigin = {0.f, 0.f, 0.f};
        vec3_t lightDirection = {0.f, 0.f, 0.f};
        bool lightEnabled = false;
        std::uint32_t serial = 0;
        bool cpuVertsReady[MAX_MESH] = {};
        bool cpuNormalsReady[MAX_MESH] = {};
    };

    std::unordered_map<BMD*, TransformRecord> gRecords;
    std::uint32_t gSerial = 0;

    TransformRecord* Get(BMD* bmd)
    {
        auto it = gRecords.find(bmd);
        return it == gRecords.end() ? nullptr : &it->second;
    }

    int ResolveTexMode(int renderFlags, int finalRenderFlags)
    {
        if (finalRenderFlags == RENDER_TEXTURE) return 0;
        if ((renderFlags & RENDER_CHROME2) == RENDER_CHROME2) return 2;
        if ((renderFlags & RENDER_CHROME3) == RENDER_CHROME3) return 3;
        if ((renderFlags & RENDER_CHROME4) == RENDER_CHROME4) return 4;
        if ((renderFlags & RENDER_CHROME5) == RENDER_CHROME5) return 5;
        if ((renderFlags & RENDER_CHROME6) == RENDER_CHROME6) return 6;
        if ((renderFlags & RENDER_CHROME7) == RENDER_CHROME7) return 7;
        if ((renderFlags & RENDER_OIL) == RENDER_OIL) return 8;
        if ((renderFlags & RENDER_METAL) == RENDER_METAL) return 9;
        if ((renderFlags & RENDER_CHROME) == RENDER_CHROME) return 1;
        return 9;
    }

    bool IsGpuRenderModeSupported(int renderFlags, int finalRenderFlags)
    {
        if ((renderFlags & RENDER_SHADOWMAP) == RENDER_SHADOWMAP) return false;
        if ((renderFlags & RENDER_WAVE) == RENDER_WAVE) return false;
        if ((renderFlags & RENDER_COLOR) == RENDER_COLOR) return false;
        if (finalRenderFlags == RENDER_BRIGHT) return false;
        return finalRenderFlags == RENDER_TEXTURE ||
               finalRenderFlags == RENDER_CHROME ||
               finalRenderFlags == RENDER_CHROME4 ||
               finalRenderFlags == RENDER_OIL;
    }

    bool BuildGpuMesh(Mesh_t* mesh)
    {
        if (!mesh || mesh->NumTriangles <= 0) return false;
        if (MuGpuSkin::HasMesh(mesh)) return true;

        std::vector<MuGpuSkin::SkinnedVertex> out;
        out.reserve(static_cast<std::size_t>(mesh->NumTriangles) * 3u);
        for (int j = 0; j < mesh->NumTriangles; ++j)
        {
            Triangle_t* triangle = &mesh->Triangles[j];
            for (int k = 0; k < triangle->Polygon; ++k)
            {
                const int vi = triangle->VertexIndex[k];
                const int ni = triangle->NormalIndex[k];
                const int ti = triangle->TexCoordIndex[k];
                if (vi < 0 || vi >= mesh->NumVertices || ni < 0 || ni >= mesh->NumNormals || ti < 0 || ti >= mesh->NumTexCoords)
                    return false;

                Vertex_t& v = mesh->Vertices[vi];
                Normal_t& n = mesh->Normals[ni];
                TexCoord_t& uv = mesh->TexCoords[ti];
                MuGpuSkin::SkinnedVertex gv;
                gv.px = v.Position[0]; gv.py = v.Position[1]; gv.pz = v.Position[2];
                gv.nx = n.Normal[0]; gv.ny = n.Normal[1]; gv.nz = n.Normal[2];
                gv.u = uv.TexCoordU; gv.v = uv.TexCoordV;
                gv.positionBone = static_cast<float>(v.Node);
                gv.normalBone = static_cast<float>(n.Node);
                out.push_back(gv);
            }
        }
        return MuGpuSkin::UploadMesh(mesh, out.data(), out.size());
    }
}

namespace BMDGpuBridge
{
    bool BeginTransform(BMD* bmd,
                        float (*bones)[3][4],
                        vec3_t boundingBoxMin,
                        vec3_t boundingBoxMax,
                        OBB_t* obb,
                        bool translate,
                        float restPoseScale,
                        float boneScale,
                        const vec3_t lightDirection)
    {
        if (!bmd || !bones || !obb) return false;
        if (!MuGpuSkin::IsAvailable()) return false;

        TransformRecord& r = gRecords[bmd];
        r.bones = bones;
        r.translate = translate;
        r.restPoseScale = restPoseScale;
        r.boneScale = boneScale;
        r.bodyScale = bmd->BodyScale;
        VectorCopy(bmd->BodyOrigin, r.bodyOrigin);
        if (lightDirection) VectorCopy(lightDirection, r.lightDirection);
        else Vector(0.f, 0.f, 0.f, r.lightDirection);
        r.lightEnabled = bmd->LightEnable;
        r.serial = ++gSerial;
        std::memset(r.cpuVertsReady, 0, sizeof(r.cpuVertsReady));
        std::memset(r.cpuNormalsReady, 0, sizeof(r.cpuNormalsReady));

        // Same release/gameplay OBB branch as legacy BMD::Transform(EditFlag != 2).
        VectorCopy(boundingBoxMin, obb->StartPos);
        obb->XAxis[0] = boundingBoxMax[0] - boundingBoxMin[0];
        obb->YAxis[1] = boundingBoxMax[1] - boundingBoxMin[1];
        obb->ZAxis[2] = boundingBoxMax[2] - boundingBoxMin[2];
        VectorAdd(obb->StartPos, bmd->BodyOrigin, obb->StartPos);
        obb->XAxis[1] = obb->XAxis[2] = 0.f;
        obb->YAxis[0] = obb->YAxis[2] = 0.f;
        obb->ZAxis[0] = obb->ZAxis[1] = 0.f;
        return true;
    }

    bool IsActive(BMD* bmd)
    {
        return bmd && Get(bmd) != nullptr && MuGpuSkin::IsAvailable();
    }

    void EnsureCpuVertices(BMD* bmd, int meshIndex)
    {
        TransformRecord* r = Get(bmd);
        if (!bmd || !r || !r->bones) return;
        if (meshIndex < 0)
        {
            for (int i = 0; i < bmd->NumMeshs; ++i) EnsureCpuVertices(bmd, i);
            return;
        }
        if (meshIndex >= bmd->NumMeshs || meshIndex >= MAX_MESH || r->cpuVertsReady[meshIndex]) return;

        Mesh_t* m = &bmd->Meshs[meshIndex];
        for (int j = 0; j < m->NumVertices; ++j)
        {
            Vertex_t* v = &m->Vertices[j];
            float* vp = VertexTransform[meshIndex][j];
            if (r->boneScale == 1.f)
            {
                if (r->restPoseScale)
                {
                    vec3_t p;
                    VectorCopy(v->Position, p);
                    VectorScale(p, r->restPoseScale, p);
                    VectorTransform(p, r->bones[v->Node], vp);
                }
                else
                {
                    VectorTransform(v->Position, r->bones[v->Node], vp);
                }
                if (r->translate) VectorScale(vp, r->bodyScale, vp);
            }
            else
            {
                VectorRotate(v->Position, r->bones[v->Node], vp);
                vp[0] = vp[0] * r->boneScale + r->bones[v->Node][0][3];
                vp[1] = vp[1] * r->boneScale + r->bones[v->Node][1][3];
                vp[2] = vp[2] * r->boneScale + r->bones[v->Node][2][3];
                if (r->translate) VectorScale(vp, r->bodyScale, vp);
            }
            if (r->translate) VectorAdd(vp, r->bodyOrigin, vp);
        }
        r->cpuVertsReady[meshIndex] = true;
    }

    void EnsureCpuNormals(BMD* bmd, int meshIndex)
    {
        TransformRecord* r = Get(bmd);
        if (!bmd || !r || !r->bones) return;
        if (meshIndex < 0)
        {
            for (int i = 0; i < bmd->NumMeshs; ++i) EnsureCpuNormals(bmd, i);
            return;
        }
        if (meshIndex >= bmd->NumMeshs || meshIndex >= MAX_MESH || r->cpuNormalsReady[meshIndex]) return;

        Mesh_t* m = &bmd->Meshs[meshIndex];
        for (int j = 0; j < m->NumNormals; ++j)
        {
            Normal_t* sn = &m->Normals[j];
            float* tn = NormalTransform[meshIndex][j];
            VectorRotate(sn->Normal, r->bones[sn->Node], tn);
            if (r->lightEnabled)
            {
                float luminosity = DotProduct(tn, r->lightDirection) * 0.8f + 0.4f;
                if (luminosity < 0.2f) luminosity = 0.2f;
                IntensityTransform[meshIndex][j] = luminosity;
            }
        }
        r->cpuNormalsReady[meshIndex] = true;
    }

    bool TryDraw(BMD* bmd,
                 int meshIndex,
                 int renderFlags,
                 int finalRenderFlags,
                 float alpha,
                 float texOffsetU,
                 float texOffsetV,
                 bool lightEnabled)
    {
        TransformRecord* r = Get(bmd);
        if (!bmd || !r || !r->bones || meshIndex < 0 || meshIndex >= bmd->NumMeshs) return false;
        if (!IsGpuRenderModeSupported(renderFlags, finalRenderFlags)) return false;

        Mesh_t* m = &bmd->Meshs[meshIndex];
        if (!BuildGpuMesh(m)) return false;

        MuGpuSkin::SkinningState s = {};
        s.boneRows = &r->bones[0][0][0];
        s.boneCount = bmd->NumBones > MAX_BONES ? MAX_BONES : bmd->NumBones;
        s.paletteSerial = r->serial;
        VectorCopy(r->bodyOrigin, s.bodyOrigin);
        s.bodyScale = r->bodyScale;
        s.boneScale = r->boneScale;
        s.restPoseScale = r->restPoseScale;
        VectorCopy(r->lightDirection, s.lightDirection);
        VectorCopy(bmd->BodyLight, s.bodyLight);
        s.alpha = alpha;
        s.texOffset[0] = texOffsetU;
        s.texOffset[1] = texOffsetV;
        s.chromeWave = static_cast<long>(WorldTime) % 10000 * 0.0001f;
        s.chromeWave2 = static_cast<int>(WorldTime) % 5000 * 0.00024f - 0.4f;
        s.chromeLight[0] = static_cast<float>(std::cos(WorldTime * 0.001));
        s.chromeLight[1] = static_cast<float>(std::sin(WorldTime * 0.002));
        s.chromeTimeTerm = static_cast<float>(std::fmod(WorldTime * 0.00006, 4096.0));
        s.translate = r->translate ? 1 : 0;
        s.lightEnabled = lightEnabled ? 1 : 0;
        s.texCoordMode = ResolveTexMode(renderFlags, finalRenderFlags);
        return MuGpuSkin::DrawMesh(m, s);
    }

    void ReleaseBMD(BMD* bmd)
    {
        if (!bmd) return;
        for (int i = 0; i < bmd->NumMeshs; ++i) MuGpuSkin::ReleaseMesh(&bmd->Meshs[i]);
        gRecords.erase(bmd);
    }

    void Shutdown()
    {
        gRecords.clear();
        MuGpuSkin::Shutdown();
    }
}
