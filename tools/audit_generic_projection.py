"""Audit Vk3D generic injection with ARGENT's reviewed projection exceptions.

Vk3D's horizontal offset must not become a FOV/metric eye transform on
vertices which already describe a screen or procedural texture surface.
"""
import json,re
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
SCREEN_SPACE={
 0xe3881ca9ca28b278: 'Screen depth copy: input clip position, fragment gl_FragCoord depth fetch',
 0xaef5cc288c7813f2: 'Screen color copy: input clip position, fragment gl_FragCoord color fetch',
 0x70903b1635c3f665: 'Procedural caustic texture grid: UV-derived clip position, w=1',
}
def main():
 profile=(ROOT/'src/sfs/EternalProfile.h').read_text()
 active={int(x,16) for x in re.findall(r'0x([0-9a-f]+)ull',profile)}
 selected=[];excluded=[]
 for r in json.loads((ROOT/'docs/vk3d-generic-vertex-audit.json').read_text())['shaders']:
  if int(r['runtimeHash'],16) in active:continue
  row={k:r[k] for k in ['sha256','runtimeHash','profileOverride','clipYFlips']}
  if int(r['runtimeHash'],16) in SCREEN_SPACE:
   row['argentReason']=SCREEN_SPACE[int(r['runtimeHash'],16)];excluded.append(row)
  elif r.get('profileOverride') and 'gl_Position.x +=' not in (r.get('overrideTail') or ''):excluded.append(row)
  else:selected.append(row)
 expected={int(r['runtimeHash'],16) for r in selected}
 actual={int(x,16) for x in re.findall(r'0x([0-9a-f]+)ull',(ROOT/'src/sfs/EternalWorldVariants.inc').read_text())}
 assert actual==expected, 'Generic projection table does not match reviewed Vk3D adaptation'
 (ROOT/'docs/eternal-generic-projection-coverage.json').write_text(json.dumps(dict(selected=selected,excluded=excluded),indent=2)+'\n')
 print(f'{len(selected)} exact generic variants; {len(excluded)} explicit exceptions; table matches')
if __name__=='__main__':main()
