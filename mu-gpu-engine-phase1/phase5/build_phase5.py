from pathlib import Path
import re

p = Path('out/GpuSkinningLegacy.cpp')
c = p.read_text(encoding='utf-8')

c = c.replace('#ifndef GL_ELEMENT_ARRAY_BUFFER', '#ifndef GL_PIXEL_UNPACK_BUFFER\n#define GL_PIXEL_UNPACK_BUFFER 0x88EC\n#endif\n#ifndef GL_STREAM_DRAW\n#define GL_STREAM_DRAW 0x88E0\n#endif\n#ifndef GL_ELEMENT_ARRAY_BUFFER')

# Add glBufferSubData function pointer.
c = c.replace('typedef void (APIENTRY* PFNGLBUFFERDATAPROC)(GLenum, std::ptrdiff_t, const void*, GLenum);',
              'typedef void (APIENTRY* PFNGLBUFFERDATAPROC)(GLenum, std::ptrdiff_t, const void*, GLenum);\ntypedef void (APIENTRY* PFNGLBUFFERSUBDATAPROC)(GLenum, std::ptrdiff_t, std::ptrdiff_t, const void*);')
c = c.replace('    PFNGLBUFFERDATAPROC glBufferData_ = nullptr;', '    PFNGLBUFFERDATAPROC glBufferData_ = nullptr;\n    PFNGLBUFFERSUBDATAPROC glBufferSubData_ = nullptr;')
c = c.replace('               Load(glBufferData_, "glBufferData") &&', '               Load(glBufferData_, "glBufferData") &&\n               Load(glBufferSubData_, "glBufferSubData") &&')

# PBO ring state.
c = c.replace('    int gNextPaletteSlot = 0;', '    int gNextPaletteSlot = 0;\n    GLuint gBonePbos[8] = {};\n    int gNextBonePbo = 0;')

# Allocate PBO ring immediately after bone texture allocation succeeds.
needle = '''        if (err != GL_NO_ERROR)\n        {\n            gError = "GL_RGBA32F bone texture allocation failed";\n            glDeleteTextures(1, &gBoneTexture);\n            gBoneTexture = 0;\n            return false;\n        }\n        return true;'''
replacement = '''        if (err != GL_NO_ERROR)\n        {\n            gError = "GL_RGBA32F bone texture allocation failed";\n            glDeleteTextures(1, &gBoneTexture);\n            gBoneTexture = 0;\n            return false;\n        }\n\n        // Phase5: eight small pixel-unpack buffers allow bone uploads to be queued\n        // without reusing the same CPU->GPU staging allocation on consecutive actors.\n        glGenBuffers_(8, gBonePbos);\n        const std::ptrdiff_t pboBytes = static_cast<std::ptrdiff_t>(200 * 3 * 4 * sizeof(float));\n        for (int i = 0; i < 8; ++i)\n        {\n            glBindBuffer_(GL_PIXEL_UNPACK_BUFFER, gBonePbos[i]);\n            glBufferData_(GL_PIXEL_UNPACK_BUFFER, pboBytes, nullptr, GL_STREAM_DRAW);\n        }\n        glBindBuffer_(GL_PIXEL_UNPACK_BUFFER, 0);\n        return true;'''
if needle not in c:
    raise SystemExit('CreateBoneTexture success block not found')
c = c.replace(needle, replacement, 1)

# Replace direct client pointer atlas upload with rotating PBO staging.
old = '''            glActiveTexture_(GL_TEXTURE1);\n            glBindTexture(GL_TEXTURE_2D, gBoneTexture);\n            const int rows = s.boneCount > 200 ? 200 : s.boneCount;\n            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, gLastPaletteSlot, rows * 3, 1, GL_RGBA, GL_FLOAT, s.boneRows);\n            gLastPaletteSerial = s.paletteSerial;'''
new = '''            glActiveTexture_(GL_TEXTURE1);\n            glBindTexture(GL_TEXTURE_2D, gBoneTexture);\n            const int rows = s.boneCount > 200 ? 200 : s.boneCount;\n            const std::ptrdiff_t bytes = static_cast<std::ptrdiff_t>(rows * 3 * 4 * sizeof(float));\n\n            const GLuint pbo = gBonePbos[gNextBonePbo];\n            gNextBonePbo = (gNextBonePbo + 1) & 7;\n            glBindBuffer_(GL_PIXEL_UNPACK_BUFFER, pbo);\n            // Orphan before writing: the driver may hand us fresh storage if the old\n            // staging allocation is still consumed by the GPU.\n            glBufferData_(GL_PIXEL_UNPACK_BUFFER, static_cast<std::ptrdiff_t>(200 * 3 * 4 * sizeof(float)), nullptr, GL_STREAM_DRAW);\n            glBufferSubData_(GL_PIXEL_UNPACK_BUFFER, 0, bytes, s.boneRows);\n            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, gLastPaletteSlot, rows * 3, 1, GL_RGBA, GL_FLOAT, reinterpret_cast<const void*>(0));\n            glBindBuffer_(GL_PIXEL_UNPACK_BUFFER, 0);\n            gLastPaletteSerial = s.paletteSerial;'''
if old not in c:
    raise SystemExit('Phase4 atlas upload block not found')
c = c.replace(old, new, 1)

# Delete PBO ring at shutdown before texture deletion.
needle2 = '        if (gBoneTexture) glDeleteTextures(1, &gBoneTexture);'
rep2 = '''        if (glDeleteBuffers_)\n        {\n            for (int i = 0; i < 8; ++i)\n                if (gBonePbos[i]) glDeleteBuffers_(1, &gBonePbos[i]);\n        }\n        std::memset(gBonePbos, 0, sizeof(gBonePbos));\n        gNextBonePbo = 0;\n        if (gBoneTexture) glDeleteTextures(1, &gBoneTexture);'''
if needle2 not in c:
    raise SystemExit('shutdown bone texture line not found')
c = c.replace(needle2, rep2, 1)

p.write_text(c, encoding='utf-8')

Path('out/PHASE5-PBO-STREAM.txt').write_text('''MU GPU ENGINE PHASE5 - ASYNC BONE PALETTE PBO STREAM\n\n- Phase1 GPU skeletal skinning preserved.\n- Phase2 one palette upload per BMD transform preserved.\n- Phase3 indexed VBO/IBO mesh cache preserved.\n- Phase4 64-slot bone atlas preserved.\n- Phase5 adds an 8-buffer GL_PIXEL_UNPACK_BUFFER ring.\n- Bone matrices are staged through orphaned GL_STREAM_DRAW PBOs, then glTexSubImage2D reads from the bound PBO.\n- This reduces the chance that the CPU blocks while the GPU is still reading previous character/monster palette uploads.\n- Especially aimed at crowded scenes where dozens of animated actors are submitted every frame.\n- Original textures, materials, blend/depth state and image assets are untouched.\n\nSource integration core only; must be linked into the user's Source Main 5.2 BMD render path.\n''', encoding='utf-8')
