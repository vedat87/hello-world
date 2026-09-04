from pathlib import Path

core = Path('out/GpuSkinningLegacy.cpp')
s = core.read_text(encoding='utf-8')

# v4: use a power-of-two full precision palette texture. Some compatibility OpenGL
# drivers reject or mishandle NPOT float vertex textures such as 3x200. 4x256 keeps
# RGBA32F precision and avoids the visual corruption seen with RGBA16F fallback.
s = s.replace('(row + 0.5) / 3.0, (bone + 0.5) / 200.0',
              '(row + 0.5) / 4.0, (bone + 0.5) / 256.0')

start = s.find('    bool CreateBoneTexture()\n    {')
end_marker = '\n}\n\nnamespace MuGpuSkin'
end = s.find(end_marker, start)
assert start >= 0 and end >= 0, 'CreateBoneTexture block not found'
new_func = '''    bool CreateBoneTexture()\n    {\n        // Clear stale errors from the legacy fixed-function renderer.\n        while (glGetError() != GL_NO_ERROR) {}\n\n        glGenTextures(1, &gBoneTexture);\n        glActiveTexture_(GL_TEXTURE1);\n        glBindTexture(GL_TEXTURE_2D, gBoneTexture);\n        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);\n        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);\n        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);\n        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);\n\n        // POT 4x256 RGBA32F: three texels are used per bone row; the fourth is padding.\n        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F_ARB, 4, 256, 0, GL_RGBA, GL_FLOAT, nullptr);\n        GLenum err = glGetError();\n        glActiveTexture_(GL_TEXTURE0);\n        if (err != GL_NO_ERROR)\n        {\n            char msg[128] = {};\n            _snprintf_s(msg, sizeof(msg), _TRUNCATE,\n                        "POT RGBA32F bone texture failed: 0x%04X", static_cast<unsigned>(err));\n            gError = msg;\n            glDeleteTextures(1, &gBoneTexture);\n            gBoneTexture = 0;\n            return false;\n        }\n        gError = "GPU ready; POT 4x256 RGBA32F bone palette";\n        return true;\n    }\n'''
s = s[:start] + new_func + s[end:]
core.write_text(s, encoding='utf-8')

bridge = Path('out/SpecialMainBridge.cpp')
b = bridge.read_text(encoding='utf-8')
# Do not suppress CPU transforms unless the GPU path actually initialized.
old = 'if (gEnabled && gGpuMeshes && meshCount > 0 && self->Meshs && self->NumBones > 0 && self->NumBones <= 200)'
new = 'if (gEnabled && gGpuMeshes && MuGpuSkin::IsAvailable() && meshCount > 0 && self->Meshs && self->NumBones > 0 && self->NumBones <= 200)'
assert old in b, 'v2 CPU bypass condition not found'
b = b.replace(old, new, 1)
bridge.write_text(b, encoding='utf-8')

print('Special Main v4 POT RGBA32F patch applied')
