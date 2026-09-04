from pathlib import Path
import re

# Special Main v5: remove per-transform heap churn and reduce per-mesh GL uniform overhead.
# This is deliberately a visual-parity/hot-path patch: it does not change render flags,
# blend modes or texture selection.

bridge = Path('out/SpecialMainBridge.cpp')
b = bridge.read_text(encoding='utf-8')

# More useful counters, but log much less frequently than v4.
b = b.replace(
'''    std::uint64_t gLazyCpuMeshTransforms = 0;\n\n    struct Record''',
'''    std::uint64_t gLazyCpuMeshTransforms = 0;\n    std::uint64_t gTransformCalls = 0;\n\n    struct Record''', 1)

# Fixed arrays avoid vector assign/allocation on every animated object transform.
old = '''        std::vector<unsigned char> skipped;\n        std::vector<unsigned char> cpuReady;'''
new = '''        unsigned char skipped[50] = {};\n        unsigned char cpuReady[50] = {};\n        signed char gpuCandidate[50] = {};'''
assert old in b, 'Record vector fields not found'
b = b.replace(old, new, 1)

# Split SEH texture probing from C++ cache operations (MSVC forbids object unwinding in __try functions).
pat = re.compile(r'''    bool IsOpaqueTexture\(int texture\)\n    \{.*?\n    \}\n\n    bool TryGpuRender''', re.S)
m = pat.search(b)
assert m, 'IsOpaqueTexture block not found'
new_opaque = '''    bool ProbeOpaqueTextureRaw(int texture)\n    {\n        __try\n        {\n            auto getTexture=reinterpret_cast<GetTextureFn>(kGetTexture);\n            void* bmp=getTexture(reinterpret_cast<void*>(kBitmaps), texture);\n            if (!bmp) return false;\n            unsigned char components=*reinterpret_cast<unsigned char*>(reinterpret_cast<unsigned char*>(bmp)+0x10c);\n            return components!=4;\n        }\n        __except(EXCEPTION_EXECUTE_HANDLER)\n        {\n            return false;\n        }\n    }\n\n    std::unordered_map<int, unsigned char> gOpaqueTextureCache;\n\n    bool IsOpaqueTexture(int texture)\n    {\n        auto it = gOpaqueTextureCache.find(texture);\n        if (it != gOpaqueTextureCache.end()) return it->second != 0;\n        const bool opaque = ProbeOpaqueTextureRaw(texture);\n        gOpaqueTextureCache.emplace(texture, opaque ? 1u : 0u);\n        return opaque;\n    }\n\n    bool TryGpuRender'''
b = b[:m.start()] + new_opaque + b[m.end():]

# Cache static GPU-candidate decision in the BMD record. Texture/script/geometry are static per model mesh.
old = '''    bool MeshGpuCandidate(BMDSpecial* self, int i)\n    {\n        if (!gEnabled || !gGpuMeshes || !self || !self->Meshs || !self->IndexTexture) return false;\n        if (self->NumBones <= 0 || self->NumBones > 200 || i < 0 || i >= self->NumMeshs) return false;\n        MeshSpecial* m = &self->Meshs[i];\n        if (m->NumTriangles <= 0 || m->NumVertices <= 0 || m->NumNormals <= 0 || !m->Vertices || !m->Normals || !m->TexCoords || !m->Triangles) return false;\n        if (m->TextureScript != nullptr) return false;\n        int texture = self->IndexTexture[m->Texture];\n        if (texture==BITMAP_HIDE || texture==BITMAP_SKIN || texture==BITMAP_WATER || texture==BITMAP_HAIR) return false;\n        return IsOpaqueTexture(texture);\n    }'''
new = '''    bool MeshGpuCandidate(BMDSpecial* self, Record& rec, int i)\n    {\n        if (!gEnabled || !gGpuMeshes || !self || !self->Meshs || !self->IndexTexture) return false;\n        if (self->NumBones <= 0 || self->NumBones > 200 || i < 0 || i >= self->NumMeshs || i >= 50) return false;\n        if (rec.gpuCandidate[i] != 0) return rec.gpuCandidate[i] > 0;\n        MeshSpecial* m = &self->Meshs[i];\n        bool ok = true;\n        if (m->NumTriangles <= 0 || m->NumVertices <= 0 || m->NumNormals <= 0 || !m->Vertices || !m->Normals || !m->TexCoords || !m->Triangles) ok = false;\n        if (ok && m->TextureScript != nullptr) ok = false;\n        if (ok)\n        {\n            int texture = self->IndexTexture[m->Texture];\n            if (texture==BITMAP_HIDE || texture==BITMAP_SKIN || texture==BITMAP_WATER || texture==BITMAP_HAIR) ok = false;\n            else ok = IsOpaqueTexture(texture);\n        }\n        rec.gpuCandidate[i] = ok ? 1 : -1;\n        return ok;\n    }'''
assert old in b, 'MeshGpuCandidate block not found'
b = b.replace(old, new, 1)

# Replace transform-time vectors with fixed stack arrays and byte arrays in Record.
old = '''        Record& r = gRecords[self];\n        const int meshCount = (self->NumMeshs > 0 && self->NumMeshs < 512) ? self->NumMeshs : 0;\n        r.skipped.assign(static_cast<std::size_t>(meshCount), 0);\n        r.cpuReady.assign(static_cast<std::size_t>(meshCount), 1);\n\n        std::vector<short> savedVertices(static_cast<std::size_t>(meshCount));\n        std::vector<short> savedNormals(static_cast<std::size_t>(meshCount));\n        if (gEnabled && gGpuMeshes && MuGpuSkin::IsAvailable() && meshCount > 0 && self->Meshs && self->NumBones > 0 && self->NumBones <= 200)\n        {\n            for (int i=0; i<meshCount; ++i)\n            {\n                MeshSpecial& m = self->Meshs[i];\n                savedVertices[static_cast<std::size_t>(i)] = m.NumVertices;\n                savedNormals[static_cast<std::size_t>(i)] = m.NumNormals;\n                if (MeshGpuCandidate(self,i))\n                {\n                    r.skipped[static_cast<std::size_t>(i)] = 1;\n                    r.cpuReady[static_cast<std::size_t>(i)] = 0;\n                    gSkippedVertices += static_cast<unsigned short>(m.NumVertices);\n                    gSkippedNormals += static_cast<unsigned short>(m.NumNormals);\n                    m.NumVertices = 0;\n                    m.NumNormals = 0;\n                }\n            }\n        }'''
new = '''        Record& r = gRecords[self];\n        ++gTransformCalls;\n        const int meshCount = (self->NumMeshs > 0 && self->NumMeshs <= 50) ? self->NumMeshs : 0;\n        std::memset(r.skipped, 0, sizeof(r.skipped));\n        std::memset(r.cpuReady, 1, sizeof(r.cpuReady));\n\n        short savedVertices[50] = {};\n        short savedNormals[50] = {};\n        if (gEnabled && gGpuMeshes && MuGpuSkin::IsAvailable() && meshCount > 0 && self->Meshs && self->NumBones > 0 && self->NumBones <= 200)\n        {\n            for (int i=0; i<meshCount; ++i)\n            {\n                MeshSpecial& m = self->Meshs[i];\n                savedVertices[i] = m.NumVertices;\n                savedNormals[i] = m.NumNormals;\n                if (MeshGpuCandidate(self,r,i))\n                {\n                    r.skipped[i] = 1;\n                    r.cpuReady[i] = 0;\n                    gSkippedVertices += static_cast<unsigned short>(m.NumVertices);\n                    gSkippedNormals += static_cast<unsigned short>(m.NumNormals);\n                    m.NumVertices = 0;\n                    m.NumNormals = 0;\n                }\n            }\n        }'''
assert old in b, 'HookTransform allocation block not found'
b = b.replace(old, new, 1)

b = b.replace('if (r.skipped[static_cast<std::size_t>(i)])', 'if (r.skipped[i])')
b = b.replace('savedVertices[static_cast<std::size_t>(i)]', 'savedVertices[i]')
b = b.replace('savedNormals[static_cast<std::size_t>(i)]', 'savedNormals[i]')

# Lazy CPU fallback: remove two vector allocations per fallback mesh.
old = '''        if (target < 0 || target >= self->NumMeshs || static_cast<std::size_t>(target) >= r.cpuReady.size()) return false;\n        if (r.cpuReady[static_cast<std::size_t>(target)]) return true;\n        if (!r.bones) return false;\n\n        const int meshCount = self->NumMeshs;\n        std::vector<short> nv(static_cast<std::size_t>(meshCount));\n        std::vector<short> nn(static_cast<std::size_t>(meshCount));\n        for (int i=0; i<meshCount; ++i)\n        {\n            nv[static_cast<std::size_t>(i)] = self->Meshs[i].NumVertices;\n            nn[static_cast<std::size_t>(i)] = self->Meshs[i].NumNormals;\n            if (i != target)\n            {\n                self->Meshs[i].NumVertices = 0;\n                self->Meshs[i].NumNormals = 0;\n            }\n        }'''
new = '''        if (target < 0 || target >= self->NumMeshs || target >= 50) return false;\n        if (r.cpuReady[target]) return true;\n        if (!r.bones || self->NumMeshs <= 0 || self->NumMeshs > 50) return false;\n\n        const int meshCount = self->NumMeshs;\n        short nv[50] = {};\n        short nn[50] = {};\n        for (int i=0; i<meshCount; ++i)\n        {\n            nv[i] = self->Meshs[i].NumVertices;\n            nn[i] = self->Meshs[i].NumNormals;\n            if (i != target)\n            {\n                self->Meshs[i].NumVertices = 0;\n                self->Meshs[i].NumNormals = 0;\n            }\n        }'''
assert old in b, 'EnsureCpuMesh allocation block not found'
b = b.replace(old, new, 1)
b = b.replace('self->Meshs[i].NumVertices = nv[static_cast<std::size_t>(i)];', 'self->Meshs[i].NumVertices = nv[i];')
b = b.replace('self->Meshs[i].NumNormals = nn[static_cast<std::size_t>(i)];', 'self->Meshs[i].NumNormals = nn[i];')
b = b.replace('r.cpuReady[static_cast<std::size_t>(target)] = 1;', 'r.cpuReady[target] = 1;')

# Log every 100k GPU draws, not every 10k. Include transform count to measure calls/frame.
oldlog = '''        if ((gGpuDraws % 10000u)==0u) Log("GPU draws=%llu fallback=%llu skippedV=%llu skippedN=%llu lazyCPU=%llu records=%u",\n            static_cast<unsigned long long>(gGpuDraws), static_cast<unsigned long long>(gFallbackDraws),\n            static_cast<unsigned long long>(gSkippedVertices), static_cast<unsigned long long>(gSkippedNormals),\n            static_cast<unsigned long long>(gLazyCpuMeshTransforms), static_cast<unsigned>(gRecords.size()));'''
newlog = '''        if ((gGpuDraws % 100000u)==0u) Log("GPU draws=%llu fallback=%llu transforms=%llu skippedV=%llu skippedN=%llu lazyCPU=%llu records=%u",\n            static_cast<unsigned long long>(gGpuDraws), static_cast<unsigned long long>(gFallbackDraws),\n            static_cast<unsigned long long>(gTransformCalls),\n            static_cast<unsigned long long>(gSkippedVertices), static_cast<unsigned long long>(gSkippedNormals),\n            static_cast<unsigned long long>(gLazyCpuMeshTransforms), static_cast<unsigned>(gRecords.size()));'''
assert oldlog in b, 'periodic log block not found'
b = b.replace(oldlog, newlog, 1)

bridge.write_text(b, encoding='utf-8')

# GPU core: cache uniforms that are identical across all meshes of one transformed body.
core = Path('out/GpuSkinningLegacy.cpp')
s = core.read_text(encoding='utf-8')

needle = '    std::uint32_t gLastPaletteSerial = 0;'
assert needle in s, 'palette serial cache not found'
s = s.replace(needle, needle + '''\n    std::uint32_t gLastUniformSerial = 0;\n    bool gSamplerUniformsSet = false;\n    int gLastLightEnabled = -999;\n    int gLastTexMode = -999;\n    float gLastAlpha = -999.f;\n    float gLastTexOffset0 = -999.f, gLastTexOffset1 = -999.f;\n    float gLastWave = -999.f, gLastWave2 = -999.f;\n    float gLastChrome0 = -999.f, gLastChrome1 = -999.f;\n    float gLastTimeTerm = -999.f;''', 1)

pat = re.compile(r'''        glUseProgram_\(gProgram\);\n        glUniform1i_\(uBaseTexture, 0\);.*?        glUniform1i_\(uTexMode, s\.texCoordMode\);\n\n        glBindBuffer_''', re.S)
m = pat.search(s)
assert m, 'DrawMesh uniform block not found'
replacement = '''        glUseProgram_(gProgram);\n        if (!gSamplerUniformsSet)\n        {\n            glUniform1i_(uBaseTexture, 0);\n            glUniform1i_(uBoneTexture, 1);\n            gSamplerUniformsSet = true;\n        }\n        if (gLastUniformSerial != s.paletteSerial)\n        {\n            glUniform3f_(uBodyOrigin, s.bodyOrigin[0], s.bodyOrigin[1], s.bodyOrigin[2]);\n            glUniform1f_(uBodyScale, s.bodyScale);\n            glUniform1f_(uBoneScale, s.boneScale);\n            glUniform1f_(uRestScale, s.restPoseScale);\n            glUniform3f_(uLightDir, s.lightDirection[0], s.lightDirection[1], s.lightDirection[2]);\n            glUniform3f_(uBodyLight, s.bodyLight[0], s.bodyLight[1], s.bodyLight[2]);\n            glUniform1i_(uTranslate, s.translate);\n            gLastUniformSerial = s.paletteSerial;\n        }\n        if (gLastLightEnabled != s.lightEnabled) { glUniform1i_(uLightEnabled, s.lightEnabled); gLastLightEnabled = s.lightEnabled; }\n        if (gLastTexMode != s.texCoordMode) { glUniform1i_(uTexMode, s.texCoordMode); gLastTexMode = s.texCoordMode; }\n        if (gLastAlpha != s.alpha) { glUniform1f_(uAlpha, s.alpha); gLastAlpha = s.alpha; }\n        if (gLastTexOffset0 != s.texOffset[0] || gLastTexOffset1 != s.texOffset[1])\n        { glUniform2f_(uTexOffset, s.texOffset[0], s.texOffset[1]); gLastTexOffset0=s.texOffset[0]; gLastTexOffset1=s.texOffset[1]; }\n        if (gLastWave != s.chromeWave) { glUniform1f_(uWave, s.chromeWave); gLastWave=s.chromeWave; }\n        if (gLastWave2 != s.chromeWave2) { glUniform1f_(uWave2, s.chromeWave2); gLastWave2=s.chromeWave2; }\n        if (gLastChrome0 != s.chromeLight[0] || gLastChrome1 != s.chromeLight[1])\n        { glUniform2f_(uChromeLight, s.chromeLight[0], s.chromeLight[1]); gLastChrome0=s.chromeLight[0]; gLastChrome1=s.chromeLight[1]; }\n        if (gLastTimeTerm != s.chromeTimeTerm) { glUniform1f_(uTimeTerm, s.chromeTimeTerm); gLastTimeTerm=s.chromeTimeTerm; }\n\n        glBindBuffer_'''
s = s[:m.start()] + replacement + s[m.end():]

# Reset caches on shutdown/re-init path.
reset = '        gLastPaletteSerial = 0;\n        gAvailable = false;'
assert reset in s, 'shutdown reset block not found'
s = s.replace(reset, '''        gLastPaletteSerial = 0;\n        gLastUniformSerial = 0;\n        gSamplerUniformsSet = false;\n        gLastLightEnabled = -999;\n        gLastTexMode = -999;\n        gLastAlpha = -999.f;\n        gLastTexOffset0 = gLastTexOffset1 = -999.f;\n        gLastWave = gLastWave2 = -999.f;\n        gLastChrome0 = gLastChrome1 = -999.f;\n        gLastTimeTerm = -999.f;\n        gAvailable = false;''', 1)

core.write_text(s, encoding='utf-8')
print('Special Main v5 hot-path cache patch applied')
