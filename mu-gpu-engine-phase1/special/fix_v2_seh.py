from pathlib import Path
p=Path('out/SpecialMainBridge.cpp')
s=p.read_text(encoding='utf-8')
needle='''    void __fastcall HookTransform(BMDSpecial* self, void*, float (*bones)[3][4], float* bbMin, float* bbMax, void* obb, bool translate, float restScale)\n    {'''
helper='''    float ReadBoneScaleSafe()\n    {\n        __try { return *reinterpret_cast<volatile float*>(kBoneScale); }\n        __except(EXCEPTION_EXECUTE_HANDLER) { return 1.f; }\n    }\n\n    void __fastcall HookTransform(BMDSpecial* self, void*, float (*bones)[3][4], float* bbMin, float* bbMax, void* obb, bool translate, float restScale)\n    {'''
assert needle in s
s=s.replace(needle,helper,1)
old='''        __try { r.boneScale=*reinterpret_cast<volatile float*>(kBoneScale); }\n        __except(EXCEPTION_EXECUTE_HANDLER) { r.boneScale=1.f; }'''
assert old in s
s=s.replace(old,'        r.boneScale=ReadBoneScaleSafe();',1)
p.write_text(s,encoding='utf-8')
print('v2 SEH helper applied')
