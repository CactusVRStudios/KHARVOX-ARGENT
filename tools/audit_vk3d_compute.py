"""Read-only correspondence audit of Vk3D compute macro wrappers.

Expands the profile's own MONO macro branch and removes single-line eye
adjustments. Only exact normalized source equality is reported, never fuzzy
matches or automatic shader activation.
"""
import concurrent.futures, json, pathlib, re, subprocess
from audit_vk3d_vr_profile import ROOT, PROFILE, CAPTURE, identity, profile_hash

def main(stage='comp'):
    kind='fragment' if stage=='frag' else 'compute'
    out=ROOT/f'reference/eternal-vr-0.90/{kind}-mono';out.mkdir(exist_ok=True)
    original_dir=CAPTURE/f'{kind}-originals';original_dir.mkdir(exist_ok=True)
    audit=json.loads((CAPTURE/'audit.json').read_text())
    def decompile(shader):
        if not any(e['model']==(4 if stage=='frag' else 5) for e in shader['entrypoints']):return
        path=original_dir/(shader['sha256']+'.'+stage)
        if path.exists():return
        subprocess.run([str(ROOT/'build/spirv-cross/Release/spirv-cross.exe'),str(CAPTURE/'spirv'/(shader['sha256']+'.spv')),'--vulkan-semantics','--output',str(path)],check=True,capture_output=True,text=True,timeout=30)
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:list(pool.map(decompile,audit['shaders']))
    originals={}
    for p in original_dir.glob('*.'+stage):
        originals.setdefault(identity(p.read_text()),[]).append(p.stem)
    def run(p):
        s=p.read_text()
        s=re.sub(r'/\*.*?\*/|//[^\n]*','',s,flags=re.S)
        s=re.sub(r'(#define\s+STEREO\s+)1',r'\g<1>0',s)
        # The shipped, normally inactive mono macro has an unused third formal
        # parameter while every call and the stereo branch use two arguments.
        s=re.sub(r'#define\s+TEXTURE_GATHER1\(Sampler,\s*uv,\s*comp\)\s+textureGather\(Sampler,\s*uv(?:,\s*comp)?\)', '#define TEXTURE_GATHER1(Sampler, uv) textureGather(Sampler, uv)', s)
        for macro,target in [('VIEW','0'),('NumWorkGroups','gl_NumWorkGroups'),('WorkGroupID','gl_WorkGroupID'),('GlobalInvocationID','gl_GlobalInvocationID')]:
            s=re.sub(r'^\s*#define\s+'+macro+r'\s+[^\n]*',f'#define {macro} {target}',s,flags=re.M)
        s=re.sub(r'struct\s+Vk3DStereo\s*\{.*?\}\s*;','',s,flags=re.S)
        s=re.sub(r'layout\s*\([^)]*\)\s*uniform\s+Vk3DParams\s*\{.*?\}\s*;','',s,flags=re.S)
        adjustments=[]
        def remove(m): adjustments.append(m[0].strip());return ''
        s=re.sub(r'^[^\n]*vk3d_params[^\n]*;',remove,s,flags=re.M)
        path=out/p.name;path.write_text(s)
        r=subprocess.run([r'D:\DoomVR\build\shader-tools\glslang-main\bin\glslang.exe','-E','-S',stage,str(path)],capture_output=True,text=True)
        if r.returncode:return dict(profile=p.name,error=r.stderr)
        path.write_text(r.stdout)
        matches=originals.get(identity(r.stdout),[])
        return dict(profile=p.name,removedEyeAdjustments=adjustments,exactMatches=[dict(sha256=sha,runtimeHash=f'0x{profile_hash((CAPTURE/"spirv"/(sha+".spv")).read_bytes()):016x}') for sha in matches])
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:rows=list(pool.map(run,sorted(PROFILE.glob('*.'+stage))))
    result=dict(automaticActivation=False,method=__doc__,shaders=rows)
    (ROOT/f'docs/vk3d-{kind}-source-audit.json').write_text(json.dumps(result,indent=2))
    print(json.dumps([r for r in rows if r.get('exactMatches') or r.get('error')],indent=2))
if __name__=='__main__':
    import argparse
    parser=argparse.ArgumentParser();parser.add_argument('--stage',choices=['comp','frag'],default='comp');main(parser.parse_args().stage)
