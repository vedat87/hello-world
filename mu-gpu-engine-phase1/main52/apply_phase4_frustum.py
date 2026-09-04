from pathlib import Path
import sys

root = Path(sys.argv[1])
p = root / 'source' / 'ZzzLodTerrain.cpp'
raw = p.read_bytes()
text = raw.decode('latin1')

old_width_a = 'Width = (float)GetScreenWidth() / 640.f * 9.1f * 0.404998f;'
old_width_b = 'Width = (float)GetScreenWidth()/640.f * 9.1f * 0.404998f;'
new_width = 'Width = (float)GetScreenWidth()/640.f * 1.1f;'
if new_width not in text:
    if old_width_a in text:
        text = text.replace(old_width_a, new_width, 1)
    elif old_width_b in text:
        text = text.replace(old_width_b, new_width, 1)
    else:
        raise SystemExit('Phase4: frustum Width anchor not found')

old_far = 'WidthFar = 1190.f * Width; // 1140.f'
old_near = 'WidthNear = 540.f * Width; // 540.f'
new_far = 'WidthFar = 1190.f * Width * sqrtf(CameraFOV / 33.f); // 1140.f'
new_near = 'WidthNear = 540.f * Width * sqrtf(CameraFOV / 33.f); // 540.f'
if new_far not in text:
    if old_far not in text:
        raise SystemExit('Phase4: WidthFar anchor not found')
    text = text.replace(old_far, new_far, 1)
if new_near not in text:
    if old_near not in text:
        raise SystemExit('Phase4: WidthNear anchor not found')
    text = text.replace(old_near, new_near, 1)

p.write_bytes(text.encode('latin1'))
(root / 'GPU-PHASE4-CULLING.txt').write_text(
    'MU Main 5.2 GPU Phase4\n\n'
    '- Keeps Phase3 GPU skeletal skinning + indexed VBO/IBO.\n'
    '- Tightens CreateFrustrum2D so objects outside the visible camera region are rejected earlier.\n'
    '- Width is corrected from the oversized legacy expression to screenWidth/640*1.1.\n'
    '- Near/Far width scales with sqrt(CameraFOV/33) to preserve FOV behavior.\n'
    '- Goal: reduce unnecessary character/monster/object render submission without changing visible quality.\n'
    '- Reference: sven-n/MuMain commit c67d5222290d4cf8b8f8906038bdc0e96001dba1.\n',
    encoding='utf-8')
print('Phase4 frustum culling patch applied:', p)
