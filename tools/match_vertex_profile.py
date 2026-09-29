"""Find exact vertex-source identities after removing documented stereo additions.

No fuzzy similarity, identifier renaming, texture rewriting or runtime activation.
"""
import argparse, concurrent.futures, hashlib, json, pathlib, re, subprocess

def canonical(source,profile=False):
    source=re.sub(r'/\*.*?\*/|//[^\n]*','',source,flags=re.S)
    source=re.sub(r'^\s*#version[^\n]*','',source,flags=re.M)
    if profile:
        source=re.sub(r'^\s*#extension\s+GL_EXT_multiview\s*:\s*(?:enable|require)\s*$','',source,flags=re.M)
        source=re.sub(r'^\s*#define\s+VIEW\s+gl_ViewIndex\s*$','',source,flags=re.M)
        source=re.sub(r'struct\s+Vk3DStereo\s*\{\s*vec4\s+stereo\s*;\s*vec4\s+custom_params\s*;\s*\}\s*;','',source)
        source=re.sub(r'layout\s*\(\s*set\s*=\s*0\s*,\s*binding\s*=\s*\d+\s*,\s*std140\s*\)\s*uniform\s+Vk3DParams\s*\{\s*Vk3DStereo\s+vk3d_params\s*\[\s*2\s*\]\s*;\s*\}\s*;','',source)
        source=re.sub(r'gl_Position\.x\s*\+=\s*vk3d_params\[VIEW\]\.stereo\.x\s*\*\s*\(gl_Position\.w\s*-\s*vk3d_params\[VIEW\]\.stereo\.y\)\s*;','',source)
        # Anything not covered by the narrow removal above must not match.
        if 'vk3d_params' in source or 'Vk3D' in source:return None
    # Preserve GLSL token boundaries: `a + + b` must not equal `a++b`.
    tokens=re.findall(r'[A-Za-z_]\w*|(?:0[xX][0-9A-Fa-f]+|(?:\d+\.?\d*|\.\d+)(?:[eE][+-]?\d+)?)[uUfFlL]*|<<=|>>=|\+\+|--|&&|\|\||==|!=|<=|>=|<<|>>|[+*/%&|^!-]=|\S',source)
    return '\x1f'.join(tokens)

def main():
    p=argparse.ArgumentParser();p.add_argument('capture',type=pathlib.Path)
    p.add_argument('--profile',type=pathlib.Path,default=pathlib.Path(r'D:\DoomVR\vk3\Profiles\Doom Eternal\ShaderSwap'))
    p.add_argument('--cross',type=pathlib.Path,default=pathlib.Path('build/spirv-cross/Release/spirv-cross.exe'))
    args=p.parse_args();audit=json.loads((args.capture/'audit.json').read_text())
    profile={}
    for f in args.profile.glob('*.vert'):
        key=canonical(f.read_text(),True)
        if key:profile.setdefault(key,[]).append(f.name)
    out=args.capture/'vertex-originals';out.mkdir(exist_ok=True)
    def one(shader):
        if not any(e['model']==0 for e in shader['entrypoints']):return None
        sha=shader['sha256'];path=out/(sha+'.vert')
        r=subprocess.run([str(args.cross),str(args.capture/'spirv'/(sha+'.spv')),'--vulkan-semantics','--output',str(path)],capture_output=True,text=True,timeout=30)
        if r.returncode:return {'sha256':sha,'error':r.stderr}
        key=canonical(path.read_text());return {'sha256':sha,'exact_source_matches':profile.get(key,[]),'canonical_sha256':hashlib.sha256(key.encode()).hexdigest()}
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:rows=[x for x in pool.map(one,audit['shaders']) if x]
    result={'vertex_modules':len(rows),'matched_modules':sum(bool(x.get('exact_source_matches')) for x in rows),'activation_allowed':False,'normalization':'Whitespace/comments/version only; profile side also removes exactly recognized Vk3D uniform and X separation injection. No fuzzy matching.','matches':rows}
    (args.capture/'vertex-profile-matches.json').write_text(json.dumps(result,indent=2));print(json.dumps({k:v for k,v in result.items() if k!='matches'},indent=2))

if __name__=='__main__':main()
