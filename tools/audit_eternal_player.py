"""Read-only Eternal player/hands contract discovery for the supported PE."""
import sys, struct, re, bisect, json
from pathlib import Path
from audit_vk3d_camera import PE
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
p=PE(r'D:\Games\dampf\steamapps\common\DOOMEternal\DOOMEternalx64vk.exe')
cs=Cs(CS_ARCH_X86,CS_MODE_64)
header=struct.unpack_from('<I',p.data,60)[0]
rva,size=struct.unpack_from('<II',p.data,header+24+112+3*8)
functions=[struct.unpack('<III',p.at(r,12))[:2] for r in range(rva,rva+size,12)]
starts=[f[0] for f in functions]
def owner(r):
 i=bisect.bisect_right(starts,r)-1
 return functions[i] if i>=0 and r<functions[i][1] else (r,r+128)
def refs(target):
 out=[]
 for va,n,raw,flags in p.sections:
  if not flags&0x20000000:continue
  data=p.data[raw:raw+n]
  for m in re.finditer(rb'[\x48\x4c][\x8b\x8d][\x05\x0d\x15\x1d\x25\x2d\x35\x3d]',data):
   o=m.start()
   if o+7<=len(data) and va+o+7+struct.unpack_from('<i',data,o+3)[0]==target:out.append(va+o)
 return out
def strings(text):
 b=text.encode()+b'\0';out=[]
 for va,n,raw,flags in p.sections:
  for m in re.finditer(re.escape(b),p.data[raw:raw+n]):out.append(va+m.start())
 return out
def dump(r,out):
 a,b=owner(r);Path(out).write_text('\n'.join(f'{i.address:#x}: {i.mnemonic} {i.op_str}' for i in cs.disasm(p.at(a,b-a),a)));return hex(a),hex(b)
if __name__=='__main__':
 out={}
 for text in ['idHands::UpdatePosition','idHands::GetWeaponFireInfo','idHands::FireWeapon','idPlayer::EvaluateControls','idPlayer::Move','idPlayer::UpdateView','hands_updatePos','hands_show','useMuzzleAsFireAxis']:
  rows=[]
  for s in strings(text):
   for r in refs(s):
    fn=owner(r); rows.append(dict(string=hex(s),ref=hex(r),function=[hex(x) for x in fn]));dump(r,Path('logs')/f'eternal-function-{fn[0]:x}.txt')
  out[text]=rows
 Path('docs/eternal-player-hands-audit.json').write_text(json.dumps(out,indent=2));print(json.dumps(out,indent=2))
