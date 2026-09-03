from pathlib import Path
import re

p = Path('out/GpuSkinningLegacy.cpp')
c = p.read_text(encoding='utf-8')

# Phase4: bone atlas ring, 64 character slots x 200 bones x 3 RGBA texels.
c = c.replace('    GLint uBoneTexture = -1;', '    GLint uBoneTexture = -1;\n    GLint uBoneSlot = -1;')
c = c.replace('    std::uint32_t gLastPaletteSerial = 0;', '    std::uint32_t gLastPaletteSerial = 0;\n    int gLastPaletteSlot = 0;\n    int gNextPaletteSlot = 0;')

# Shader: slot uniform + atlas addressing.
c = c.replace('uniform sampler2D uBoneTexture;\n', 'uniform sampler2D uBoneTexture;\nuniform int uBoneSlot;\n')
old = '''vec4 boneRow(float bone, float row)\n{\n    // Texture is fixed at 3 x 200 RGBA32F. Each bone occupies one Y row,\n    // with its affine 3x4 rows in X texels 0,1,2.\n    return texture2D(uBoneTexture, vec2((row + 0.5) / 3.0, (bone + 0.5) / 200.0));\n}'''
new = '''vec4 boneRow(float bone, float row)\n{\n    // Phase4 atlas: each character palette occupies one horizontal row.\n    // 200 bones * 3 affine rows = 600 RGBA32F texels per slot.\n    float x = (bone * 3.0 + row + 0.5) / 600.0;\n    float y = (float(uBoneSlot) + 0.5) / 64.0;\n    return texture2D(uBoneTexture, vec2(x, y));\n}'''
if old not in c:
    raise SystemExit('boneRow shader block not found')
c = c.replace(old, new, 1)

c = c.replace('        uBoneTexture = glGetUniformLocation_(gProgram, "uBoneTexture");', '        uBoneTexture = glGetUniformLocation_(gProgram, "uBoneTexture");\n        uBoneSlot = glGetUniformLocation_(gProgram, "uBoneSlot");')

# Allocate atlas instead of a single 3x200 texture.
old_alloc = '        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F_ARB, 3, 200, 0, GL_RGBA, GL_FLOAT, nullptr);'
new_alloc = '        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F_ARB, 600, 64, 0, GL_RGBA, GL_FLOAT, nullptr);'
if old_alloc not in c:
    raise SystemExit('bone texture allocation not found')
c = c.replace(old_alloc, new_alloc, 1)

# Replace Phase3 one-palette overwrite with rotating atlas rows.
pat = re.compile(r'''        // Phase3 hot path: upload compact 3x4 palette once per BMD transform serial\.\n        if \(gLastPaletteSerial != s\.paletteSerial\)\n        \{\n            glActiveTexture_\(GL_TEXTURE1\);\n            glBindTexture\(GL_TEXTURE_2D, gBoneTexture\);\n            const int rows = s\.boneCount > 200 \? 200 : s\.boneCount;\n            glTexSubImage2D\(GL_TEXTURE_2D, 0, 0, 0, 3, rows, GL_RGBA, GL_FLOAT, s\.boneRows\);\n            gLastPaletteSerial = s\.paletteSerial;\n        \}\n\n        glUseProgram_\(gProgram\);''')
rep = '''        // Phase4 hot path: do not overwrite a texture region that may still be in use\n        // by the GPU. Stream each character/monster palette into a rotating atlas row.\n        if (gLastPaletteSerial != s.paletteSerial)\n        {\n            gLastPaletteSlot = gNextPaletteSlot;\n            gNextPaletteSlot = (gNextPaletteSlot + 1) & 63;\n            glActiveTexture_(GL_TEXTURE1);\n            glBindTexture(GL_TEXTURE_2D, gBoneTexture);\n            const int rows = s.boneCount > 200 ? 200 : s.boneCount;\n            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, gLastPaletteSlot, rows * 3, 1, GL_RGBA, GL_FLOAT, s.boneRows);\n            gLastPaletteSerial = s.paletteSerial;\n        }\n\n        glUseProgram_(gProgram);'''
c2, n = pat.subn(rep, c, count=1)
if n != 1:
    raise SystemExit('phase3 palette block not found')
c = c2

c = c.replace('        glUniform1i_(uBoneTexture, 1);', '        glUniform1i_(uBoneTexture, 1);\n        glUniform1i_(uBoneSlot, gLastPaletteSlot);')
c = c.replace('        gLastPaletteSerial = 0;\n        gAvailable = false;', '        gLastPaletteSerial = 0;\n        gLastPaletteSlot = 0;\n        gNextPaletteSlot = 0;\n        gAvailable = false;')

p.write_text(c, encoding='utf-8')

Path('out/PHASE4-BONE-ATLAS.txt').write_text('''MU GPU ENGINE PHASE4 - BONE PALETTE ATLAS RING\n\n- Keeps Phase1 GPU skeletal skinning.\n- Keeps Phase2 one upload per BMD transform serial.\n- Keeps Phase3 static indexed VBO/IBO mesh cache.\n- Replaces the single 3x200 bone texture with a 600x64 RGBA32F atlas.\n- Every character/monster palette is streamed into a different atlas row before reuse.\n- This avoids repeatedly overwriting the exact same texture memory while earlier GPU draws may still be consuming it, reducing driver/GPU synchronization pressure in crowded scenes.\n- Shader selects the correct palette through uBoneSlot.\n- Visual materials, textures, blend/depth and original Main assets are unchanged.\n\nThis remains a source integration core, not a drop-in replacement Main.exe.\n''', encoding='utf-8')
