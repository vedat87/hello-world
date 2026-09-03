from pathlib import Path
import re, shutil

src = Path('mu-gpu-engine-phase1/legacy')
out = Path('out')
out.mkdir(exist_ok=True)
for p in src.iterdir():
    if p.is_file():
        shutil.copy2(p, out / p.name)

# Header
h = out / 'GpuSkinningLegacy.h'
hc = h.read_text(encoding='utf-8')
needle = '    bool UploadMesh(const void* meshKey, const SkinnedVertex* vertices, std::size_t vertexCount);'
insert = needle + '\n    bool UploadIndexedMesh(const void* meshKey, const SkinnedVertex* vertices, std::size_t vertexCount,\n                           const std::uint32_t* indices, std::size_t indexCount);'
assert needle in hc
hc = hc.replace(needle, insert)
h.write_text(hc, encoding='utf-8')

# Core
p = out / 'GpuSkinningLegacy.cpp'
c = p.read_text(encoding='utf-8')
c = c.replace('#ifndef GL_STATIC_DRAW', '#ifndef GL_ELEMENT_ARRAY_BUFFER\n#define GL_ELEMENT_ARRAY_BUFFER 0x8893\n#endif\n#ifndef GL_STATIC_DRAW')
c = re.sub(r'(struct MeshBuffer\s*\{\s*GLuint vbo = 0;\s*)GLsizei vertexCount = 0;', r'\1GLuint ibo = 0;\n        GLsizei vertexCount = 0;\n        GLsizei indexCount = 0;', c, count=1)
c = c.replace('    std::string gError;', '    std::string gError;\n    std::uint32_t gLastPaletteSerial = 0;')

pat = re.compile(r'        // Upload one compact 3x4 affine palette\. 200 bones = 9\.6 KB max\..*?        glUseProgram_\(gProgram\);', re.S)
rep = '''        // Phase3 hot path: upload compact 3x4 palette once per BMD transform serial.
        if (gLastPaletteSerial != s.paletteSerial)
        {
            glActiveTexture_(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, gBoneTexture);
            const int rows = s.boneCount > 200 ? 200 : s.boneCount;
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 3, rows, GL_RGBA, GL_FLOAT, s.boneRows);
            gLastPaletteSerial = s.paletteSerial;
        }

        glUseProgram_(gProgram);'''
c2, n = pat.subn(rep, c, count=1)
assert n == 1, 'palette patch failed'
c = c2

marker = '    bool DrawMesh(const void* meshKey, const SkinningState& s)'
assert marker in c
indexed = '''    bool UploadIndexedMesh(const void* meshKey, const SkinnedVertex* vertices, std::size_t vertexCount,
                           const std::uint32_t* indices, std::size_t indexCount)
    {
        if (!IsAvailable() || !meshKey || !vertices || vertexCount == 0 || !indices || indexCount == 0) return false;
        auto old = gMeshes.find(meshKey);
        if (old != gMeshes.end()) return true;

        MeshBuffer b;
        glGenBuffers_(1, &b.vbo);
        glBindBuffer_(GL_ARRAY_BUFFER, b.vbo);
        glBufferData_(GL_ARRAY_BUFFER, static_cast<std::ptrdiff_t>(vertexCount * sizeof(SkinnedVertex)), vertices, GL_STATIC_DRAW);

        glGenBuffers_(1, &b.ibo);
        glBindBuffer_(GL_ELEMENT_ARRAY_BUFFER, b.ibo);
        glBufferData_(GL_ELEMENT_ARRAY_BUFFER, static_cast<std::ptrdiff_t>(indexCount * sizeof(std::uint32_t)), indices, GL_STATIC_DRAW);

        glBindBuffer_(GL_ARRAY_BUFFER, 0);
        glBindBuffer_(GL_ELEMENT_ARRAY_BUFFER, 0);
        if (glGetError() != GL_NO_ERROR)
        {
            if (b.ibo) glDeleteBuffers_(1, &b.ibo);
            if (b.vbo) glDeleteBuffers_(1, &b.vbo);
            gError = "Static indexed BMD VBO/IBO upload failed";
            return false;
        }
        b.vertexCount = static_cast<GLsizei>(vertexCount);
        b.indexCount = static_cast<GLsizei>(indexCount);
        gMeshes.emplace(meshKey, b);
        return true;
    }

'''
c = c.replace(marker, indexed + marker, 1)

old_draw = '''        glActiveTexture_(GL_TEXTURE0); // Main's currently bound BMD/chrome texture is unit 0.
        glDrawArrays(GL_TRIANGLES, 0, it->second.vertexCount);'''
new_draw = '''        glActiveTexture_(GL_TEXTURE0); // Main's currently bound BMD/chrome texture is unit 0.
        if (it->second.ibo && it->second.indexCount > 0)
        {
            glBindBuffer_(GL_ELEMENT_ARRAY_BUFFER, it->second.ibo);
            glDrawElements(GL_TRIANGLES, it->second.indexCount, GL_UNSIGNED_INT, nullptr);
            glBindBuffer_(GL_ELEMENT_ARRAY_BUFFER, 0);
        }
        else
        {
            glDrawArrays(GL_TRIANGLES, 0, it->second.vertexCount);
        }'''
assert old_draw in c, 'draw block not found'
c = c.replace(old_draw, new_draw, 1)
c = c.replace('        if (it->second.vbo && glDeleteBuffers_) glDeleteBuffers_(1, &it->second.vbo);', '        if (it->second.ibo && glDeleteBuffers_) glDeleteBuffers_(1, &it->second.ibo);\n        if (it->second.vbo && glDeleteBuffers_) glDeleteBuffers_(1, &it->second.vbo);')
c = c.replace('                if (kv.second.vbo) glDeleteBuffers_(1, &kv.second.vbo);', '                if (kv.second.ibo) glDeleteBuffers_(1, &kv.second.ibo);\n                if (kv.second.vbo) glDeleteBuffers_(1, &kv.second.vbo);')
c = c.replace('        return glGetError() == GL_NO_ERROR;', '        return true;')
c = c.replace('        gAvailable = false;', '        gLastPaletteSerial = 0;\n        gAvailable = false;')
p.write_text(c, encoding='utf-8')

# Bridge: build unique vertex tuples + static IBO.
b = out / 'BMDGpuBridge.cpp'
bc = b.read_text(encoding='utf-8')
if '#include <unordered_map>' not in bc:
    bc = bc.replace('#include <cstring>', '#include <cstring>\n#include <unordered_map>')
patb = re.compile(r'    bool BuildGpuMesh\(Mesh_t\* mesh\)\s*\{.*?        return MuGpuSkin::UploadMesh\(mesh, out\.data\(\), out\.size\(\)\);\s*    \}', re.S)
repb = '''    struct CornerKey
    {
        int vi, ni, ti;
        bool operator==(const CornerKey& o) const { return vi == o.vi && ni == o.ni && ti == o.ti; }
    };
    struct CornerHash
    {
        std::size_t operator()(const CornerKey& k) const
        {
            std::size_t h = static_cast<std::size_t>(k.vi) * 73856093u;
            h ^= static_cast<std::size_t>(k.ni) * 19349663u;
            h ^= static_cast<std::size_t>(k.ti) * 83492791u;
            return h;
        }
    };

    bool BuildGpuMesh(Mesh_t* mesh)
    {
        if (!mesh || mesh->NumTriangles <= 0) return false;
        if (MuGpuSkin::HasMesh(mesh)) return true;

        std::vector<MuGpuSkin::SkinnedVertex> vertices;
        std::vector<std::uint32_t> indices;
        vertices.reserve(static_cast<std::size_t>(mesh->NumVertices));
        indices.reserve(static_cast<std::size_t>(mesh->NumTriangles) * 3u);
        std::unordered_map<CornerKey, std::uint32_t, CornerHash> remap;
        remap.reserve(static_cast<std::size_t>(mesh->NumTriangles) * 2u);

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

                CornerKey key{vi, ni, ti};
                auto found = remap.find(key);
                if (found != remap.end())
                {
                    indices.push_back(found->second);
                    continue;
                }

                Vertex_t& v = mesh->Vertices[vi];
                Normal_t& n = mesh->Normals[ni];
                TexCoord_t& uv = mesh->TexCoords[ti];
                MuGpuSkin::SkinnedVertex gv{};
                gv.px = v.Position[0]; gv.py = v.Position[1]; gv.pz = v.Position[2];
                gv.nx = n.Normal[0]; gv.ny = n.Normal[1]; gv.nz = n.Normal[2];
                gv.u = uv.TexCoordU; gv.v = uv.TexCoordV;
                gv.positionBone = static_cast<float>(v.Node);
                gv.normalBone = static_cast<float>(n.Node);
                const std::uint32_t newIndex = static_cast<std::uint32_t>(vertices.size());
                vertices.push_back(gv);
                remap.emplace(key, newIndex);
                indices.push_back(newIndex);
            }
        }
        return MuGpuSkin::UploadIndexedMesh(mesh, vertices.data(), vertices.size(), indices.data(), indices.size());
    }'''
bc2, n = patb.subn(repb, bc, count=1)
assert n == 1, 'bridge patch failed'
b.write_text(bc2, encoding='utf-8')

(out / 'PHASE3-INDEXED.txt').write_text('''MU GPU ENGINE PHASE3 - INDEXED CHARACTER MESH CACHE\n\nPhase1: static rest-pose VBO GPU skinning.\nPhase2: one bone-palette upload per BMD transform.\nPhase3: unique (position,normal,uv) corner cache + static IBO + glDrawElements.\nThis lets the GPU post-transform vertex cache reuse skinned vertices shared by adjacent triangles.\nOriginal textures/blend/depth behavior stays owned by Main. CPU fallback remains for cloth, collision, wave, shadowmap and unsupported passes.\n\nRaGEZONE/Elion reference: high FPS comes from engine rework, culling, batching/instancing and GPU animation rather than merely replacing the graphics API.\n''', encoding='utf-8')
