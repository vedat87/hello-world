from pathlib import Path
import shutil, sys, re

# Usage: python integrate_gpu_phase3.py <Source Main 5.2 root> <generated GPU source dir>
root = Path(sys.argv[1])
gpu = Path(sys.argv[2])
src = root / 'source'

for name in ['GpuSkinningLegacy.cpp','GpuSkinningLegacy.h','BMDGpuBridge.cpp','BMDGpuBridge.h']:
    shutil.copy2(gpu / name, src / name)

def read_latin(path):
    return path.read_bytes().decode('latin1')

def write_latin(path, text):
    path.write_bytes(text.encode('latin1'))

def patch_function(text, start_marker, end_marker, fn):
    a = text.index(start_marker)
    b = text.index(end_marker, a)
    return text[:a] + fn(text[a:b]) + text[b:]

bmd_path = src / 'ZzzBMD.cpp'
c = read_latin(bmd_path)

needle = '#include "ZzzBMD.h"'
assert needle in c
if '#include "BMDGpuBridge.h"' not in c:
    c = c.replace(needle, needle + '\r\n#include "GpuSkinningLegacy.h"\r\n#include "BMDGpuBridge.h"', 1)

transform_start = '#ifdef PBG_ADD_NEWCHAR_MONK_ITEM\r\nvoid BMD::Transform('
if transform_start not in c:
    transform_start = '#ifdef PBG_ADD_NEWCHAR_MONK_ITEM\nvoid BMD::Transform('
assert transform_start in c
insert_anchor = '\tvec3_t BoundingMin;\r\n\tvec3_t BoundingMax;'
if insert_anchor not in c:
    insert_anchor = '\tvec3_t BoundingMin;\n\tvec3_t BoundingMax;'
assert insert_anchor in c
if 'gpuRestPoseScale' not in c:
    gpu_transform = '''#ifdef PBG_ADD_NEWCHAR_MONK_ITEM
    const float gpuRestPoseScale = _Scale;
#else
    const float gpuRestPoseScale = 0.0f;
#endif
#ifndef _DEBUG
    if(EditFlag != 2 &&
       BMDGpuBridge::BeginTransform(this, BoneMatrix, BoundingBoxMin, BoundingBoxMax, OBB,
                                    Translate, gpuRestPoseScale, BoneScale,
                                    LightEnable ? LightPosition : NULL))
    {
        return;
    }
#endif
'''.replace('\n','\r\n')
    c = c.replace(insert_anchor, gpu_transform + insert_anchor, 1)

def patch_render_mesh(fn):
    light_pat = re.compile(r'(\telse if\(EnableLight\)\s*\{\s*)(\t\tfor\(int j=0;j<m->NumNormals;j\+\+\))', re.S)
    if 'EnsureCpuNormals(this, i);' not in fn:
        fn, n = light_pat.subn(r'\1\t\tif(BMDGpuBridge::IsActive(this))\r\n\t\t\tBMDGpuBridge::EnsureCpuNormals(this, i);\r\n\2', fn, count=1)
        assert n == 1, 'RenderMesh lighting loop patch failed'

    chrome_anchor = '        vec3_t L = { (float)(cos(WorldTime*0.001f)), (float)(sin(WorldTime*0.002f)), 1.f };'
    if chrome_anchor in fn and 'GPU_NORMALS_FOR_CHROME' not in fn:
        repl = chrome_anchor + '\r\n\t\t// GPU_NORMALS_FOR_CHROME\r\n\t\tif(BMDGpuBridge::IsActive(this))\r\n\t\t\tBMDGpuBridge::EnsureCpuNormals(this, i);'
        fn = fn.replace(chrome_anchor, repl, 1)

    draw_anchor = '    // ver 1.0 (triangle)'
    assert draw_anchor in fn
    if 'GPU_PHASE3_DRAW' not in fn:
        block = '''    // GPU_PHASE3_DRAW: original Main still owns texture/blend/depth state.
    const bool gpuPlainMesh = (i != streamMesh) && !(BlendMesh <= -2 || m->Texture == BlendMesh);
    if(gpuPlainMesh && BMDGpuBridge::IsActive(this))
    {
        const float gpuU = EnableWave ? BlendMeshTexCoordU : 0.0f;
        const float gpuV = EnableWave ? BlendMeshTexCoordV : 0.0f;
        if(BMDGpuBridge::TryDraw(this, i, RenderFlag, Render, Alpha, gpuU, gpuV, EnableLight))
            return;
    }

    if(BMDGpuBridge::IsActive(this))
    {
        BMDGpuBridge::EnsureCpuVertices(this, i);
        if((RenderFlag & RENDER_WAVE) == RENDER_WAVE)
            BMDGpuBridge::EnsureCpuNormals(this, i);
    }

'''.replace('\n','\r\n')
        fn = fn.replace(draw_anchor, block + draw_anchor, 1)
    return fn
c = patch_function(c, 'void BMD::RenderMesh(', 'void BMD::RenderMeshAlternative(', patch_render_mesh)

def patch_alt(fn):
    anchor = '\tif(m->NumTriangles == 0) return;'
    assert anchor in fn
    if 'GPU_PHASE3_ALT_FALLBACK' not in fn:
        block = '''
    // GPU_PHASE3_ALT_FALLBACK
    if(BMDGpuBridge::IsActive(this))
    {
        BMDGpuBridge::EnsureCpuVertices(this, i);
        BMDGpuBridge::EnsureCpuNormals(this, i);
    }
'''.replace('\n','\r\n')
        fn = fn.replace(anchor, anchor + block, 1)
    return fn
c = patch_function(c, 'void BMD::RenderMeshAlternative(', 'void BMD::RenderMeshEffect', patch_alt)

def patch_effect(fn):
    anchor = '\tif(m->NumTriangles <= 0) return;'
    if anchor not in fn:
        anchor = '\tif(m->NumTriangles == 0) return;'
    assert anchor in fn
    if 'GPU_PHASE3_EFFECT_FALLBACK' not in fn:
        block = '''
    // GPU_PHASE3_EFFECT_FALLBACK
    if(BMDGpuBridge::IsActive(this))
        BMDGpuBridge::EnsureCpuVertices(this, i);
'''.replace('\n','\r\n')
        fn = fn.replace(anchor, anchor + block, 1)
    return fn
try:
    c = patch_function(c, 'void BMD::RenderMeshEffect', 'void BMD::RenderBody(', patch_effect)
except ValueError:
    pass

def patch_collision(fn):
    brace = '{\r\n'
    if 'GPU_PHASE3_COLLISION_FALLBACK' not in fn:
        block = '''    // GPU_PHASE3_COLLISION_FALLBACK
    if(BMDGpuBridge::IsActive(this))
        BMDGpuBridge::EnsureCpuVertices(this, Mesh);
'''.replace('\n','\r\n')
        fn = fn.replace(brace, brace + block, 1)
    return fn
try:
    c = patch_function(c, 'bool BMD::CollisionDetectLineToMesh(', 'void BMD::CreateLightMapSurface(', patch_collision)
except ValueError:
    pass

def patch_lightmap(fn):
    brace = '{\r\n'
    if 'GPU_PHASE3_LIGHTMAP_FALLBACK' not in fn:
        block = '''    // GPU_PHASE3_LIGHTMAP_FALLBACK
    if(BMDGpuBridge::IsActive(this))
    {
        BMDGpuBridge::EnsureCpuVertices(this, i);
        BMDGpuBridge::EnsureCpuNormals(this, i);
    }
'''.replace('\n','\r\n')
        fn = fn.replace(brace, brace + block, 1)
    return fn
try:
    c = patch_function(c, 'void BMD::CreateLightMapSurface(', 'void BMD::CreateLightMaps(', patch_lightmap)
except ValueError:
    pass

def patch_translate(fn):
    anchor = '\tif(m->NumTriangles == 0) return;'
    assert anchor in fn
    if 'GPU_PHASE3_TRANSLATE_FALLBACK' not in fn:
        block = '''
    // GPU_PHASE3_TRANSLATE_FALLBACK
    if(BMDGpuBridge::IsActive(this))
    {
        BMDGpuBridge::EnsureCpuVertices(this, i);
        BMDGpuBridge::EnsureCpuNormals(this, i);
    }
'''.replace('\n','\r\n')
        fn = fn.replace(anchor, anchor + block, 1)
    return fn
try:
    c = patch_function(c, 'void BMD::RenderMeshTranslate(', 'void BMD::RenderBodyTranslate(', patch_translate)
except ValueError:
    pass

release_marker = 'void BMD::Release()\r\n{'
if release_marker not in c:
    release_marker = 'void BMD::Release()\n{'
assert release_marker in c
if 'GPU_PHASE3_RELEASE' not in c:
    c = c.replace(release_marker, release_marker + '\r\n\t// GPU_PHASE3_RELEASE\r\n\tBMDGpuBridge::ReleaseBMD(this);', 1)
write_latin(bmd_path, c)

phys_path = src / 'PhysicsManager.cpp'
p = read_latin(phys_path)
inc = '#include "PhysicsManager.h"'
assert inc in p
if '#include "BMDGpuBridge.h"' not in p:
    p = p.replace(inc, inc + '\r\n#include "BMDGpuBridge.h"', 1)
create_anchor = '\tm_iNumVertices = pMesh->NumVertices;'
if create_anchor in p and 'GPU_PHASE3_CLOTH_CREATE' not in p:
    p = p.replace(create_anchor, '\t// GPU_PHASE3_CLOTH_CREATE\r\n\tBMDGpuBridge::EnsureCpuVertices(b, m_iMesh);\r\n\r\n' + create_anchor, 1)
setfixed_sig = 'void CPhysicsClothMesh::SetFixedVertices( float Matrix[3][4])'
idx = p.find(setfixed_sig)
if idx >= 0 and 'GPU_PHASE3_CLOTH_FIXED' not in p[idx:idx+1400]:
    seg_end = p.find('void CPhysicsClothMesh::NotifyVertexPos', idx)
    seg = p[idx:seg_end]
    anchor = '\tMesh_t *pMesh = &b->Meshs[m_iMesh];'
    assert anchor in seg
    seg = seg.replace(anchor, anchor + '\r\n\t// GPU_PHASE3_CLOTH_FIXED\r\n\tBMDGpuBridge::EnsureCpuVertices(b, m_iMesh);', 1)
    p = p[:idx] + seg + p[seg_end:]
write_latin(phys_path, p)

proj_path = root / 'Main.vcxproj'
proj = proj_path.read_text(encoding='utf-8-sig')
if 'source\\GpuSkinningLegacy.cpp' not in proj:
    gpu_items = '''    <ClCompile Include="source\\GpuSkinningLegacy.cpp">
      <PrecompiledHeader>NotUsing</PrecompiledHeader>
    </ClCompile>
    <ClCompile Include="source\\BMDGpuBridge.cpp" />'''
    proj = proj.replace('    <ClCompile Include="source\\ZzzBMD.cpp" />',
                        '    <ClCompile Include="source\\ZzzBMD.cpp" />\n' + gpu_items, 1)
if 'source\\GpuSkinningLegacy.h' not in proj:
    first_header = '    <ClInclude Include="source\\BaseCls.h" />'
    proj = proj.replace(first_header,
                        '    <ClInclude Include="source\\GpuSkinningLegacy.h" />\n    <ClInclude Include="source\\BMDGpuBridge.h" />\n' + first_header, 1)
proj_path.write_text(proj, encoding='utf-8-sig')

report = root / 'GPU-PHASE3-INTEGRATION.txt'
report.write_text('''MU Main 5.2 GPU Phase3 integration\n\n- BMD::Transform: release gameplay can skip eager CPU position transforms.\n- RenderMesh: common texture/chrome passes draw through GLSL GPU skinning.\n- Static rest-pose VBO + indexed IBO/glDrawElements.\n- Bone palette upload cache retained.\n- Cloth/collision/lightmap/effect/alternative/translate paths retain lazy CPU fallback.\n- Original textures, alpha/blend, depth and visual assets remain under the old Main renderer.\n''', encoding='utf-8')
print('GPU Phase3 integration applied to', root)
