from pathlib import Path
import shutil
import sys

# Usage:
#   python apply_phase5_effect_instancing.py <Source Main 5.2 root> <phase5 source dir>
root = Path(sys.argv[1])
phase5 = Path(sys.argv[2])
src = root / 'source'

for name in ('GpuSpriteBatch.cpp', 'GpuSpriteBatch.h'):
    shutil.copy2(phase5 / name, src / name)


def read_latin(path):
    return path.read_bytes().decode('latin1')


def write_latin(path, text):
    path.write_bytes(text.encode('latin1'))


# ---------------------------------------------------------------------------
# 1) Correct the Phase3/4 bone-palette cache key.
# A per-BMD serial alone can collide when switching between different BMDs.
# Keying by both palette pointer and serial keeps the optimization while
# preventing a stale palette from another model from being reused.
# ---------------------------------------------------------------------------
skin_path = src / 'GpuSkinningLegacy.cpp'
skin = read_latin(skin_path)
if 'gLastPaletteRows' not in skin:
    anchor = '    std::uint32_t gLastPaletteSerial = 0;'
    if anchor not in skin:
        raise SystemExit('Phase5: gLastPaletteSerial anchor not found')
    skin = skin.replace(anchor, anchor + '\r\n    const float* gLastPaletteRows = 0;', 1)

    cond = 'if (gLastPaletteSerial != s.paletteSerial)'
    if cond not in skin:
        raise SystemExit('Phase5: palette condition anchor not found')
    skin = skin.replace(cond, 'if (gLastPaletteSerial != s.paletteSerial || gLastPaletteRows != s.boneRows)', 1)

    serial_set = '            gLastPaletteSerial = s.paletteSerial;'
    if serial_set not in skin:
        raise SystemExit('Phase5: palette serial assignment anchor not found')
    skin = skin.replace(serial_set, serial_set + '\r\n            gLastPaletteRows = s.boneRows;', 1)

    reset = '        gLastPaletteSerial = 0;'
    if reset in skin:
        skin = skin.replace(reset, reset + '\r\n        gLastPaletteRows = 0;', 1)
write_latin(skin_path, skin)


# ---------------------------------------------------------------------------
# 2) Replace the tiny immediate-mode world-sprite loop with conservative
# consecutive-run instancing. Render order is preserved: only adjacent
# renderable sprites with the same texture + blend mode are combined.
# Unsupported GL/instancing automatically falls back to the original path.
# ---------------------------------------------------------------------------
sprite_path = src / 'zzzeffectsprite.cpp'
c = read_latin(sprite_path)

inc = '#include "WSClient.h"'
if '#include "GpuSpriteBatch.h"' not in c:
    if inc not in c:
        raise SystemExit('Phase5: WSClient include anchor not found')
    c = c.replace(inc, inc + '\r\n#include "GpuSpriteBatch.h"\r\n#include <vector>', 1)

start = c.find('void RenderSprites ( BYTE byRenderOneMore )')
if start < 0:
    start = c.find('void RenderSprites(BYTE byRenderOneMore)')
if start < 0:
    raise SystemExit('Phase5: RenderSprites start not found')
end = c.find('void CheckSprites()', start)
if end < 0:
    raise SystemExit('Phase5: CheckSprites anchor not found')

replacement = r'''namespace
{
    enum { GPU_SPRITE_MIN_BATCH = 3 };

    int Phase5SpriteBlendKey(const OBJECT* o)
    {
        if (o->Type == BITMAP_FORMATION_MARK)
            return 100;
        if (o->SubType >= 0 && o->SubType <= 3)
            return o->SubType;
        return -1;
    }

    void Phase5ApplySpriteBlend(const OBJECT* o)
    {
        if (o->Type == BITMAP_FORMATION_MARK)
        {
            EnableAlphaTest();
            return;
        }

        switch (o->SubType)
        {
        case 0: EnableAlphaBlend();      break;
        case 1: EnableAlphaBlendMinus(); break;
        case 2: EnableAlphaTest();       break;
        case 3: EnableAlphaBlend2();     break;
        default:                         break;
        }
    }

    bool Phase5PassAllowsSprite(OBJECT* o, BYTE pass, bool mutate)
    {
        if (pass == 1 && o->Position[2] > 350.f)
            return false;

        if (pass == 2 && o->Position[2] <= 300.f)
        {
            if (mutate)
                o->Live = false;
            return false;
        }

        return o->Live ? true : false;
    }

    void Phase5FinishSprite(OBJECT* o, BYTE pass)
    {
        if (pass == 0 || pass == 2)
            o->Live = false;
    }

    void Phase5GetSpriteDrawData(OBJECT* o, bool advanceAnimation,
                                 float& width, float& height,
                                 float& u, float& v, float& uw, float& vh)
    {
        if (advanceAnimation)
        {
            if (o->Visible)
            {
                o->AnimationFrame += 0.1f;
                if (o->AnimationFrame > 1.f)
                    o->AnimationFrame = 1.f;
            }
            else
            {
                o->AnimationFrame -= 0.1f;
                if (o->AnimationFrame < 0.2f)
                    o->AnimationFrame = 0.2f;
            }
        }

        const float scale = o->AnimationFrame * o->Scale;
        BITMAP_t* pBitmap = Bitmaps.GetTexture(o->Type);
        width = pBitmap->Width * scale;
        height = pBitmap->Height * scale;
        u = 0.f;
        v = 0.f;
        uw = 1.f;
        vh = 1.f;

        if (o->Type == BITMAP_FORMATION_MARK)
        {
            width = 64.f;
            height = 64.f;
            uw = 0.33f;
            vh = 0.33f;

            switch (o->SubType)
            {
            case 0: u = 0.f;   v = 0.f;   break;
            case 1: u = 0.33f; v = 0.f;   break;
            case 2: u = 0.66f; v = 0.f;   break;
            case 3: u = 0.f;   v = 0.33f; break;
            case 4: u = 0.33f; v = 0.33f; break;
            case 5: u = 0.66f; v = 0.33f; break;
            case 6: u = 0.f;   v = 0.66f; break;
            case 7: u = 0.33f; v = 0.66f; break;
            default:                         break;
            }
        }
    }

    void Phase5BuildSpriteInstance(OBJECT* o, MuGpuSprite::SpriteInstance& out)
    {
        float width, height, u, v, uw, vh;
        Phase5GetSpriteDrawData(o, true, width, height, u, v, uw, vh);

        vec3_t center;
        VectorTransform(o->Position, CameraMatrix, center);
        out.center[0] = center[0];
        out.center[1] = center[1];
        out.center[2] = center[2];
        out.size[0] = width;
        out.size[1] = height;
        out.rotationDeg = o->Angle[2];
        out.color[0] = o->Light[0];
        out.color[1] = o->Light[1];
        out.color[2] = o->Light[2];

        BITMAP_t* pBitmap = Bitmaps.GetTexture(o->Type);
        if (pBitmap->Components == 3 || o->Type == BITMAP_BLOOD+1 || o->Type == BITMAP_FONT_HIT)
            out.color[3] = 1.f;
        else
            out.color[3] = o->Light[0];

        out.uvRect[0] = u;
        out.uvRect[1] = v;
        out.uvRect[2] = uw;
        out.uvRect[3] = vh;
    }

    void Phase5RenderSpriteNoAdvance(OBJECT* o)
    {
        float width, height, u, v, uw, vh;
        Phase5GetSpriteDrawData(o, false, width, height, u, v, uw, vh);
        RenderSprite(o->Type, o->Position, width, height, o->Light, o->Angle[2], u, v, uw, vh);
    }
}

void RenderSprites ( BYTE byRenderOneMore )
{
    int i = 0;
    while (i < MAX_SPRITES)
    {
        OBJECT* o = &Sprites[i];

        if (!Phase5PassAllowsSprite(o, byRenderOneMore, true))
        {
            ++i;
            continue;
        }

        const int key = Phase5SpriteBlendKey(o);
        if (key < 0 || !MuGpuSprite::IsAvailable())
        {
            Phase5ApplySpriteBlend(o);
            RenderSprite(o, o->Owner);
            Phase5FinishSprite(o, byRenderOneMore);
            ++i;
            continue;
        }

        const int texture = o->Type;
        std::vector<int> run;
        int j = i;
        for (; j < MAX_SPRITES; ++j)
        {
            OBJECT* candidate = &Sprites[j];
            if (!Phase5PassAllowsSprite(candidate, byRenderOneMore, true))
                continue;

            if (candidate->Type != texture || Phase5SpriteBlendKey(candidate) != key)
                break;

            run.push_back(j);
        }

        Phase5ApplySpriteBlend(o);

        if (run.size() >= GPU_SPRITE_MIN_BATCH)
        {
            std::vector<MuGpuSprite::SpriteInstance> instances(run.size());
            for (std::size_t n = 0; n < run.size(); ++n)
                Phase5BuildSpriteInstance(&Sprites[run[n]], instances[n]);

            BindTexture(texture);
            if (!MuGpuSprite::Draw(&instances[0], instances.size()))
            {
                // Runtime extension/shader failure: preserve visuals and continue
                // through the old renderer without advancing AnimationFrame twice.
                for (std::size_t n = 0; n < run.size(); ++n)
                    Phase5RenderSpriteNoAdvance(&Sprites[run[n]]);
            }

            for (std::size_t n = 0; n < run.size(); ++n)
                Phase5FinishSprite(&Sprites[run[n]], byRenderOneMore);
        }
        else
        {
            for (std::size_t n = 0; n < run.size(); ++n)
            {
                OBJECT* legacy = &Sprites[run[n]];
                RenderSprite(legacy, legacy->Owner);
                Phase5FinishSprite(legacy, byRenderOneMore);
            }
        }

        i = (j > i) ? j : (i + 1);
    }
}

'''.replace('\n', '\r\n')

c = c[:start] + replacement + c[end:]
write_latin(sprite_path, c)


# ---------------------------------------------------------------------------
# 3) Add Phase5 compilation units to the project.
# ---------------------------------------------------------------------------
proj_path = root / 'Main.vcxproj'
proj = proj_path.read_text(encoding='utf-8-sig')

if 'source\\GpuSpriteBatch.cpp' not in proj:
    anchor = '    <ClCompile Include="source\\ZzzBMD.cpp" />'
    if anchor not in proj:
        raise SystemExit('Phase5: vcxproj compile anchor not found')
    item = ('    <ClCompile Include="source\\GpuSpriteBatch.cpp">\n'
            '      <PrecompiledHeader>NotUsing</PrecompiledHeader>\n'
            '    </ClCompile>\n')
    proj = proj.replace(anchor, item + anchor, 1)

if 'source\\GpuSpriteBatch.h' not in proj:
    anchor = '    <ClInclude Include="source\\BaseCls.h" />'
    if anchor not in proj:
        raise SystemExit('Phase5: vcxproj header anchor not found')
    proj = proj.replace(anchor, '    <ClInclude Include="source\\GpuSpriteBatch.h" />\n' + anchor, 1)

proj_path.write_text(proj, encoding='utf-8-sig')

(root / 'GPU-PHASE5-EFFECT-INSTANCING.txt').write_text(
    'MU Main 5.2 GPU Phase5 - Effect Instancing\n\n'
    '- Keeps Phase3 GPU skeletal skinning + indexed VBO/IBO.\n'
    '- Keeps Phase4 GPU character/monster shadow projection.\n'
    '- Adds source-level GPU instancing for consecutive world sprite/effect runs.\n'
    '- One static quad is reused; position/size/rotation/color/UV are per-instance attributes.\n'
    '- Render order is preserved: only adjacent renderable sprites with the same texture and blend mode are batched.\n'
    '- Formation-mark atlas UV behavior is preserved.\n'
    '- Unsupported OpenGL instancing/shaders automatically fall back to the original immediate-mode renderer.\n'
    '- Fixes the bone-palette upload cache key to use both palette pointer and transform serial.\n'
    '- Goal: reduce effect-heavy Aida/crowd draw-call pressure without removing effects or changing assets.\n',
    encoding='utf-8')

print('Phase5 GPU effect instancing applied to', root)
