"""Recover original compute identities by undoing reviewed Vk3D culling edits.

Only exact normalized equality after the explicitly listed reversals is accepted.
No fuzzy matches activate runtime patches.
"""
import json, re, subprocess
from audit_vk3d_vr_profile import ROOT, PROFILE, CAPTURE, identity, profile_hash

RULES = {
    '2ea7b4fffc960e98_CS.comp': ('lights_fine', ['_325 < 1000000015047466219876688855040.0', '_975', '_996 == true', '_2100']),
    'bc341bc0c92a5653_CS.comp': ('lights_coarse', ['_226 < 1000000015047466219876688855040.0', '_1005']),
    '6c5c991be2f5385c_CS.comp': ('decals_fine', ['_282 < 1000000015047466219876688855040.0', '_921']),
    'c1e8bf029b8d8542_CS.comp': ('decals_coarse', ['_226 < 1000000015047466219876688855040.0', '_997']),
    'a94f9b5ec2150c68_CS.comp': ('lights_bounds', []),
    'a94f9b5ec2150c68_f43f156b36d10dd9_CS.comp': ('lights_bounds', []),
    '5f263272c96df536_CS.comp': ('decals_bounds', []),
    '5f263272c96df536_95eb4dcb18b5edcb_CS.comp': ('decals_bounds', []),
}

def original_profile(name, gates):
    s = (PROFILE/name).read_text()
    for gate in gates:
        pattern = r'//\s*if \('+re.escape(gate)+r'\)'
        s, count = re.subn(pattern, 'if ('+gate+')', s)
        assert count == 1, (name, gate)
    s, samples = re.subn(r'([a-zA-Z_0-9]+ = )[01];//(TEXEL_FETCH\([^\n]+;)', r'\1\2', s)
    s = re.sub(r'_87\.x \+= 0\.05;|_84\.x -= 0\.05;', '', s)
    if name=='a94f9b5ec2150c68_f43f156b36d10dd9_CS.comp':
        s=re.sub(r'//if \(_87.x < 2000\)\s*\{\s*\}','',s)
    s = s.replace('_87 = clamp(_87, vec2(-1.0), vec2(1.0));', '_87 = clamp(_87, vec2(0.0), vec2(1.0));')
    s = re.sub(r'/\*.*?\*/|//[^\n]*', '', s, flags=re.S)
    s = re.sub(r'(#define\s+STEREO\s+)1', r'\g<1>0', s)
    s = re.sub(r'#define\s+TEXTURE_GATHER1\(Sampler,\s*uv,\s*comp\)\s+textureGather\(Sampler,\s*uv(?:,\s*comp)?\)', '#define TEXTURE_GATHER1(Sampler, uv) textureGather(Sampler, uv)', s)
    for macro, target in [('VIEW','0'),('NumWorkGroups','gl_NumWorkGroups'),('WorkGroupID','gl_WorkGroupID'),('GlobalInvocationID','gl_GlobalInvocationID')]:
        s = re.sub(r'^\s*#define\s+'+macro+r'\s+[^\n]*', f'#define {macro} {target}', s, flags=re.M)
    s = re.sub(r'struct\s+Vk3DStereo\s*\{.*?\}\s*;', '', s, flags=re.S)
    s = re.sub(r'layout\s*\([^)]*\)\s*uniform\s+Vk3DParams\s*\{.*?\}\s*;', '', s, flags=re.S)
    path = ROOT/'reference/eternal-vr-0.90/compute-restored'/name
    path.parent.mkdir(exist_ok=True)
    path.write_text(s)
    p = subprocess.run([r'D:\DoomVR\build\shader-tools\glslang-main\bin\glslang.exe', '-E', '-S', 'comp', str(path)], capture_output=True, text=True, check=True)
    path.write_text(p.stdout)
    return p.stdout, samples

def main():
    originals = {}
    for p in (CAPTURE/'compute-originals').glob('*.comp'):
        originals.setdefault(identity(p.read_text()), []).append(p.stem)
    passes = json.loads((CAPTURE/'render-analysis.json').read_text())['passes']
    rows = []
    for name, (kind, gates) in RULES.items():
        source, samples = original_profile(name, gates)
        matches = originals.get(identity(source), [])
        rows.append(dict(profile=name, kind=kind, restoredGates=gates, restoredDepthSamples=samples,
            exactMatches=[dict(sha256=sha, runtimeHash=f'0x{profile_hash((CAPTURE/"spirv"/(sha+".spv")).read_bytes()):016x}',
                submittedGroups=sum(p['shaders'].get('32')==sha for p in passes)) for sha in matches]))
    (ROOT/'docs/vk3d-light-culling-audit.json').write_text(json.dumps(dict(method=__doc__, shaders=rows), indent=2))
    print(json.dumps(rows, indent=2))

if __name__ == '__main__': main()
