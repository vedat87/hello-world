from pathlib import Path
p = Path('out/GpuSkinningLegacy.cpp')
c = p.read_text(encoding='utf-8')
bad = '''            for (auto& kv : gMeshes)
                if (kv.second.ibo) glDeleteBuffers_(1, &kv.second.ibo);
                if (kv.second.vbo) glDeleteBuffers_(1, &kv.second.vbo);'''
good = '''            for (auto& kv : gMeshes)
            {
                if (kv.second.ibo) glDeleteBuffers_(1, &kv.second.ibo);
                if (kv.second.vbo) glDeleteBuffers_(1, &kv.second.vbo);
            }'''
if bad not in c:
    raise SystemExit('shutdown cleanup pattern not found')
c = c.replace(bad, good, 1)
p.write_text(c, encoding='utf-8')
