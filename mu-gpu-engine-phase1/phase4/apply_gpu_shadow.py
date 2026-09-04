from pathlib import Path
import sys

out = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('out')

# ---- GpuSkinningLegacy.h ----
h = out / 'GpuSkinningLegacy.h'
hc = h.read_text(encoding='utf-8')
needle = '        int texCoordMode;           // 0 mesh, 1 chrome, 2 chrome2 ... 8 oil, 9 metal/default chrome\n'
assert needle in hc, 'SkinningState texCoordMode anchor not found'
if 'int shadowMode;' not in hc:
    hc = hc.replace(needle, needle +
        '        int shadowMode;             // 1 = projected character/monster shadow pass\n'
        '        float shadowSX;\n'
        '        float shadowSY;\n', 1)
h.write_text(hc, encoding='utf-8')

# ---- GpuSkinningLegacy.cpp ----
p = out / 'GpuSkinningLegacy.cpp'
c = p.read_text(encoding='utf-8')

# CPU-side uniform locations.
needle = '    GLint uTexMode = -1;\n'
assert needle in c, 'uTexMode location anchor not found'
if 'GLint uShadowMode' not in c:
    c = c.replace(needle, needle +
        '    GLint uShadowMode = -1;\n'
        '    GLint uShadowSX = -1;\n'
        '    GLint uShadowSY = -1;\n', 1)

# GLSL uniforms.
needle = 'uniform int uTexMode;\n\nvarying vec2 vUV;'
assert needle in c, 'GLSL uTexMode anchor not found'
if 'uniform int uShadowMode;' not in c:
    c = c.replace(needle,
        'uniform int uTexMode;\n'
        'uniform int uShadowMode;\n'
        'uniform float uShadowSX;\n'
        'uniform float uShadowSY;\n\n'
        'varying vec2 vUV;', 1)

old_main = '''void main()\n{\n    vec3 p = skinPosition(aPositionBone);\n    vec3 n = skinNormal(aNormalBone);\n    gl_Position = gl_ModelViewProjectionMatrix * vec4(p, 1.0);\n\n    if (uTexMode == 0)\n        vUV = aTexCoord + uTexOffset;\n    else\n        vUV = chromeUV(n);\n\n    float intensity = 1.0;\n    if (uLightEnabled != 0)\n        intensity = max(dot(n, uLightDir) * 0.8 + 0.4, 0.2);\n    vColor = vec4(uBodyLight * intensity, uAlpha);\n}\n)GLSL";'''
new_main = '''void main()\n{\n    vec3 p = skinPosition(aPositionBone);\n    vec3 n = skinNormal(aNormalBone);\n\n    if (uShadowMode != 0)\n    {\n        // Exact legacy BMD::RenderBodyShadow projection, now after GPU skinning.\n        vec3 q = p - uBodyOrigin;\n        float denom = q.z - uShadowSY;\n        if (abs(denom) > 0.0001)\n            q.x += q.z * (q.x + uShadowSX) / denom;\n        q.z = 5.0;\n        p = q + uBodyOrigin;\n    }\n\n    gl_Position = gl_ModelViewProjectionMatrix * vec4(p, 1.0);\n\n    if (uShadowMode != 0)\n    {\n        vUV = vec2(0.0, 0.0);\n        vColor = vec4(uBodyLight, uAlpha);\n    }\n    else\n    {\n        if (uTexMode == 0)\n            vUV = aTexCoord + uTexOffset;\n        else\n            vUV = chromeUV(n);\n\n        float intensity = 1.0;\n        if (uLightEnabled != 0)\n            intensity = max(dot(n, uLightDir) * 0.8 + 0.4, 0.2);\n        vColor = vec4(uBodyLight * intensity, uAlpha);\n    }\n}\n)GLSL";'''
assert old_main in c, 'vertex shader main anchor not found'
c = c.replace(old_main, new_main, 1)

old_frag = '''void main()\n{\n    gl_FragColor = texture2D(uBaseTexture, vUV) * vColor;\n}\n)GLSL";'''
new_frag = '''uniform int uShadowMode;\nvoid main()\n{\n    if (uShadowMode != 0)\n        gl_FragColor = vColor;\n    else\n        gl_FragColor = texture2D(uBaseTexture, vUV) * vColor;\n}\n)GLSL";'''
assert old_frag in c, 'fragment shader main anchor not found'
c = c.replace(old_frag, new_frag, 1)

# Uniform locations.
needle = '        uTexMode = glGetUniformLocation_(gProgram, "uTexMode");\n'
assert needle in c, 'BuildProgram uTexMode anchor not found'
if 'uShadowMode = glGetUniformLocation_' not in c:
    c = c.replace(needle, needle +
        '        uShadowMode = glGetUniformLocation_(gProgram, "uShadowMode");\n'
        '        uShadowSX = glGetUniformLocation_(gProgram, "uShadowSX");\n'
        '        uShadowSY = glGetUniformLocation_(gProgram, "uShadowSY");\n', 1)

# Per-draw values.
needle = '        glUniform1i_(uTexMode, s.texCoordMode);\n'
assert needle in c, 'DrawMesh uTexMode anchor not found'
if 'glUniform1i_(uShadowMode' not in c:
    c = c.replace(needle, needle +
        '        glUniform1i_(uShadowMode, s.shadowMode);\n'
        '        glUniform1f_(uShadowSX, s.shadowSX);\n'
        '        glUniform1f_(uShadowSY, s.shadowSY);\n', 1)

p.write_text(c, encoding='utf-8')

# ---- BMDGpuBridge.h ----
bh = out / 'BMDGpuBridge.h'
bhc = bh.read_text(encoding='utf-8')
needle = '    void ReleaseBMD(BMD* bmd);\n'
assert needle in bhc, 'BMDGpuBridge.h release anchor not found'
if 'TryDrawShadow' not in bhc:
    decl = '''    bool TryDrawShadow(BMD* bmd,\n                       int meshIndex,\n                       float shadowSX,\n                       float shadowSY,\n                       const float* shadowColor);\n\n'''
    bhc = bhc.replace(needle, decl + needle, 1)
bh.write_text(bhc, encoding='utf-8')

# ---- BMDGpuBridge.cpp ----
b = out / 'BMDGpuBridge.cpp'
bc = b.read_text(encoding='utf-8')
needle = '    void ReleaseBMD(BMD* bmd)\n'
assert needle in bc, 'BMDGpuBridge.cpp ReleaseBMD anchor not found'
if 'bool TryDrawShadow(' not in bc:
    fn = '''    bool TryDrawShadow(BMD* bmd,\n                       int meshIndex,\n                       float shadowSX,\n                       float shadowSY,\n                       const float* shadowColor)\n    {\n        TransformRecord* r = Get(bmd);\n        if (!bmd || !r || !r->bones || meshIndex < 0 || meshIndex >= bmd->NumMeshs) return false;\n        if (!shadowColor) return false;\n\n        Mesh_t* m = &bmd->Meshs[meshIndex];\n        if (!BuildGpuMesh(m)) return false;\n\n        MuGpuSkin::SkinningState s = {};\n        s.boneRows = &r->bones[0][0][0];\n        s.boneCount = bmd->NumBones > MAX_BONES ? MAX_BONES : bmd->NumBones;\n        s.paletteSerial = r->serial;\n        VectorCopy(r->bodyOrigin, s.bodyOrigin);\n        s.bodyScale = r->bodyScale;\n        s.boneScale = r->boneScale;\n        s.restPoseScale = r->restPoseScale;\n        Vector(0.f, 0.f, 0.f, s.lightDirection);\n        s.bodyLight[0] = shadowColor[0];\n        s.bodyLight[1] = shadowColor[1];\n        s.bodyLight[2] = shadowColor[2];\n        s.alpha = shadowColor[3];\n        s.texOffset[0] = s.texOffset[1] = 0.f;\n        s.chromeWave = s.chromeWave2 = 0.f;\n        s.chromeLight[0] = s.chromeLight[1] = 0.f;\n        s.chromeTimeTerm = 0.f;\n        s.translate = r->translate ? 1 : 0;\n        s.lightEnabled = 0;\n        s.texCoordMode = 0;\n        s.shadowMode = 1;\n        s.shadowSX = shadowSX;\n        s.shadowSY = shadowSY;\n        return MuGpuSkin::DrawMesh(m, s);\n    }\n\n'''
    bc = bc.replace(needle, fn + needle, 1)
b.write_text(bc, encoding='utf-8')

(out / 'PHASE4-GPU-SHADOW.txt').write_text(
    'MU GPU ENGINE PHASE4 - GPU CHARACTER/MONSTER SHADOWS\n\n'
    '- Keeps Phase3 GPU skeletal skinning + indexed VBO/IBO.\n'
    '- Projects BMD body shadows in the vertex shader after GPU skinning.\n'
    '- Removes the normal shadow path dependency on eagerly populated VertexTransform arrays.\n'
    '- CPU shadow fallback remains available when the GPU path is unavailable.\n'
    '- Shadow projection math matches the Louis Main 5.2 RenderBodyShadow formula.\n',
    encoding='utf-8')
print('Phase4 GPU shadow source patch applied to', out)
