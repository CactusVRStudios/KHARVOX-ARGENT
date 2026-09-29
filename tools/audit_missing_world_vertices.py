"""Audit captured common-camera variants omitted by the explicit ARGENT list.
Vk3D generic injection applies unless a ShaderSwap replacement opts out.
No runtime fuzzy matching: output rules use exact captured module hashes.
"""
import json,re
from audit_vk3d_vr_profile import ROOT,CAPTURE,profile_hash

def main():
 active={int(x,16) for x in re.findall(r'0x([0-9a-f]+)ull',(ROOT/'src/sfs/EternalProfile.h').read_text())}
 audit={r['sha256']:r for r in json.loads((ROOT/'docs/vk3d-generic-vertex-audit.json').read_text())['shaders']}
 selected=[];excluded=[]
 for path in sorted((CAPTURE/'vertex-originals').glob('*.vert')):
  source=path.read_text()
  ubo=re.search(r'layout\(set = 0, binding = 0, std140\) uniform \w+\s*\{.*?\}\s*(\w+);',source,re.S)
  if not ubo:continue
  variable=ubo[1];anchor='mat4('+', '.join(f'vec4({variable}._m{i})' for i in range(3,7))+')'
  if anchor not in source or 'gl_Position.y = -gl_Position.y;' not in source:continue
  value=profile_hash((CAPTURE/'spirv'/(path.stem+'.spv')).read_bytes())
  if value in active:continue
  if value == 0x39d5b4f395660d17:
   excluded.append(dict(sha256=path.stem,runtimeHash=hex(value),reason='r216 frame 7200: shadow atlas 4096x2048; eye projection displaces the caster'))
   continue
  row=dict(sha256=path.stem,runtimeHash=hex(value),common=variable,anchor=anchor)
  match=audit.get(path.stem,{});override=match.get('profileOverride');tail=match.get('overrideTail','') or ''
  if override and 'gl_Position.x +=' not in tail:excluded.append(dict(**row,reason='Explicit Vk3D no-projection replacement',profile=override));continue
  selected.append(dict(**row,policy='Explicit Vk3D stereo' if override else 'Vk3D generic world projection',profile=override))
 (ROOT/'docs/eternal-missing-world-vertices.json').write_text(json.dumps(dict(selected=selected,excluded=excluded),indent=2))
 print(f'{len(selected)} world variants; {len(excluded)} explicit no-projection exceptions')
if __name__=='__main__':main()
