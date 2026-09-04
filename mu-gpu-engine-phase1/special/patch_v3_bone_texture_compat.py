from pathlib import Path

core = Path('out/GpuSkinningLegacy.cpp')
s = core.read_text(encoding='utf-8')

# Older compatibility OpenGL drivers can expose vertex texture fetch but reject RGBA32F.
# Add a half-float palette fallback; 16F is plenty for MU local bone matrices and keeps
# the same shader/upload path.
needle = '''#ifndef GL_RGBA32F_ARB\n#define GL_RGBA32F_ARB 0x8814\n#endif\n'''
repl = needle + '''#ifndef GL_RGBA16F_ARB\n#define GL_RGBA16F_ARB 0x881A\n#endif\n'''
assert needle in s, 'RGBA32F define block not found'
s = s.replace(needle, repl, 1)

old = '''    bool CreateBoneTexture()\n    {\n        glGenTextures(1, &gBoneTexture);\n        glActiveTexture_(GL_TEXTURE1);\n        glBindTexture(GL_TEXTURE_2D, gBoneTexture);\n        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);\n        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);\n        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);\n        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);\n        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F_ARB, 3, 200, 0, GL_RGBA, GL_FLOAT, nullptr);\n        GLenum err = glGetError();\n        glActiveTexture_(GL_TEXTURE0);\n        if (err != GL_NO_ERROR)\n        {\n            gError = "GL_RGBA32F bone texture allocation failed";\n            glDeleteTextures(1, &gBoneTexture);\n            gBoneTexture = 0;\n            return false;\n        }\n        return true;\n    }\n'''
new = '''    bool CreateBoneTexture()\n    {\n        // Clear stale errors left by the legacy fixed-function renderer before probing.\n        while (glGetError() != GL_NO_ERROR) {}\n\n        glGenTextures(1, &gBoneTexture);\n        glActiveTexture_(GL_TEXTURE1);\n        glBindTexture(GL_TEXTURE_2D, gBoneTexture);\n        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);\n        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);\n        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);\n        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);\n\n        // Preferred: full precision. Some old/compatibility drivers reject this even\n        // though GLSL vertex texture fetch is present.\n        while (glGetError() != GL_NO_ERROR) {}\n        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F_ARB, 3, 200, 0, GL_RGBA, GL_FLOAT, nullptr);\n        GLenum err32 = glGetError();\n        if (err32 == GL_NO_ERROR)\n        {\n            glActiveTexture_(GL_TEXTURE0);\n            gError = "GPU ready; bone palette RGBA32F";\n            return true;\n        }\n\n        // Compatibility fallback: 16-bit float. Bone rotation/translation precision is\n        // far better than normalized 8-bit and avoids the unsupported RGBA32F path.\n        while (glGetError() != GL_NO_ERROR) {}\n        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F_ARB, 3, 200, 0, GL_RGBA, GL_FLOAT, nullptr);\n        GLenum err16 = glGetError();\n        glActiveTexture_(GL_TEXTURE0);\n        if (err16 == GL_NO_ERROR)\n        {\n            gError = "GPU ready; bone palette RGBA16F compatibility mode";\n            return true;\n        }\n\n        char msg[160] = {};\n        _snprintf_s(msg, sizeof(msg), _TRUNCATE,\n                    "Bone texture failed: RGBA32F=0x%04X RGBA16F=0x%04X",\n                    static_cast<unsigned>(err32), static_cast<unsigned>(err16));\n        gError = msg;\n        glDeleteTextures(1, &gBoneTexture);\n        gBoneTexture = 0;\n        return false;\n    }\n'''
assert old in s, 'CreateBoneTexture block not found'
s = s.replace(old, new, 1)
core.write_text(s, encoding='utf-8')

bridge = Path('out/SpecialMainBridge.cpp')
b = bridge.read_text(encoding='utf-8')
oldlog = 'Log("GPU skinning active: indexed VBO/IBO + GLSL 1.20 bone texture.");'
newlog = 'Log("GPU skinning active: indexed VBO/IBO + GLSL 1.20 bone texture. %s", MuGpuSkin::LastError());'
assert oldlog in b, 'active GPU log line not found'
b = b.replace(oldlog, newlog, 1)
bridge.write_text(b, encoding='utf-8')

print('Special Main v3 bone texture compatibility patch applied')
