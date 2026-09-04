from pathlib import Path
import sys

root = Path(sys.argv[1])
p = root / 'source' / 'ZzzBMD.cpp'
text = p.read_bytes().decode('latin1')

start = text.index('void BMD::RenderBodyShadow(')
end = text.index('void BMD::RenderObjectBoundingBox()', start)
fn = text[start:end]

if 'GPU_PHASE4_SHADOW_COLOR' not in fn:
    anchor = '''    if ( gMapManager.InBattleCastle() )\r\n    {\r\n        sx = 2500.f;\r\n        sy = 4000.f;\r\n    }\r\n'''
    if anchor not in fn:
        anchor = '''    if ( gMapManager.InBattleCastle() )\n    {\n        sx = 2500.f;\n        sy = 4000.f;\n    }\n'''
    if anchor not in fn:
        raise SystemExit('Phase4 shadow: BattleCastle anchor not found')
    nl = '\r\n' if '\r\n' in fn else '\n'
    block = (nl + '    // GPU_PHASE4_SHADOW_COLOR: preserve the exact color selected by the original renderer.' + nl +
             '    GLfloat gpuShadowColor[4] = { 0.f, 0.f, 0.f, 1.f };' + nl +
             '    glGetFloatv(GL_CURRENT_COLOR, gpuShadowColor);' + nl)
    fn = fn.replace(anchor, anchor + block, 1)

if 'GPU_PHASE4_SHADOW_DRAW' not in fn:
    anchor_crlf = '''\t\t\tif(m->NumTriangles > 0 && m->Texture != BlendMesh)\r\n\t\t\t{\r\n\t\t\t\tglBegin(GL_TRIANGLES);'''
    anchor_lf = '''\t\t\tif(m->NumTriangles > 0 && m->Texture != BlendMesh)\n\t\t\t{\n\t\t\t\tglBegin(GL_TRIANGLES);'''
    anchor = anchor_crlf if anchor_crlf in fn else anchor_lf
    if anchor not in fn:
        raise SystemExit('Phase4 shadow: RenderBodyShadow mesh anchor not found')
    nl = '\r\n' if '\r\n' in fn else '\n'
    repl = ('\t\t\tif(m->NumTriangles > 0 && m->Texture != BlendMesh)' + nl +
            '\t\t\t{' + nl +
            '\t\t\t\t// GPU_PHASE4_SHADOW_DRAW: skin + project this mesh fully on the GPU.' + nl +
            '\t\t\t\tif(BMDGpuBridge::IsActive(this) &&' + nl +
            '\t\t\t\t   BMDGpuBridge::TryDrawShadow(this, i, sx, sy, gpuShadowColor))' + nl +
            '\t\t\t\t{' + nl +
            '\t\t\t\t\tcontinue;' + nl +
            '\t\t\t\t}' + nl +
            '\t\t\t\t// GPU unavailable/special case: reconstruct only this mesh for the old path.' + nl +
            '\t\t\t\tif(BMDGpuBridge::IsActive(this))' + nl +
            '\t\t\t\t\tBMDGpuBridge::EnsureCpuVertices(this, i);' + nl + nl +
            '\t\t\t\tglBegin(GL_TRIANGLES);')
    fn = fn.replace(anchor, repl, 1)

text = text[:start] + fn + text[end:]
p.write_bytes(text.encode('latin1'))
(root / 'GPU-PHASE4-SHADOW-INTEGRATION.txt').write_text(
    'Main 5.2 Phase4 GPU shadow integration\n\n'
    '- RenderBodyShadow first attempts BMDGpuBridge::TryDrawShadow.\n'
    '- GPU shader performs skeletal skinning and the original planar shadow projection.\n'
    '- Legacy CPU VertexTransform shadow path is reconstructed lazily only as fallback.\n',
    encoding='utf-8')
print('Phase4 GPU shadow integration applied:', p)
