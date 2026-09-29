"""Close the eight remaining vertex associations by reversing reviewed edits."""
import json,re
from audit_vk3d_vr_profile import ROOT,PROFILE,CAPTURE,identity,profile_hash
from audit_vk3d_materials import normalize

def main():
 rows=[]
 existing=json.loads((ROOT/'docs/vk3d-vr-source-audit.json').read_text())['shaders']
 for row in existing:
  if row['exactMatches']:continue
  name=row['profile'];s=(PROFILE/name).read_text()
  s=re.sub(r'/\*.*?\*/|//[^\n]*','',s,flags=re.S)
  s=re.sub(r'struct\s+Vk3DStereo\s*\{.*?\}\s*;','',s,flags=re.S)
  s=re.sub(r'layout\s*\([^)]*\)\s*uniform\s+Vk3DParams\s*\{.*?\}\s*;','',s,flags=re.S)
  flips=list(re.finditer(r'gl_Position\.y\s*=\s*-gl_Position\.y\s*;',s));s=s[:flips[-1].end()]+'\n}\n'
  kind='projection'
  if name.startswith(('37e1','756a','d38e','ef9c')):
   s,n=re.subn(r'(float _\d+ = )(_\d+\._m6);',r'\1min(0.9900000095367431640625, \2);',s);assert n==1
   kind='particle_interpolation'
  elif name.startswith(('3b145','44cc')):
   s,n=re.subn(r'gl_Position.x \+= vk3d_params\[VIEW\].stereo.x \* \(gl_Position.w - vk3d_params\[VIEW\].stereo.y\);','',s);assert n==1
  elif name.startswith('6a46'):
   s,n=re.subn(r'(gl_Position.z = _12\(_600, _602\))\s*\*\s*0;',r'\1;',s);assert n==1;kind='hud_depth'
  elif name.startswith('7c38'):
   s,n=re.subn(r'(void main\(\)\s*\{)\s*return;',r'\1',s);assert n==1;kind='disable_duplicate_glass'
  else:raise ValueError(name)
  matches=[]
  for p in (CAPTURE/'vertex-originals').glob('*.vert'):
   if identity(normalize(p.read_text()))==identity(normalize(s)):
    matches.append(dict(sha256=p.stem,runtimeHash=f'0x{profile_hash((CAPTURE/"spirv"/(p.stem+".spv")).read_bytes()):016x}'))
  assert matches,name
  rows.append(dict(profile=name,kind=kind,exactMatches=matches))
 (ROOT/'docs/vk3d-vertex-complete-audit.json').write_text(json.dumps(rows,indent=2))
 print(json.dumps(rows,indent=2))
if __name__=='__main__':main()
