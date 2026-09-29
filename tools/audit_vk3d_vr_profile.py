"""Recover exact source associations from the local Eternal VR profile.

Removing only the stereo declarations and the appended injection after the
final clip-Y flip preserves every original shader token. No fuzzy association
is allowed to activate a shader. The extracted tail is retained for review.
"""
import hashlib, json, pathlib, re
from match_vertex_profile import canonical

ROOT = pathlib.Path(__file__).resolve().parents[1]
PROFILE = ROOT / 'reference/eternal-vr-0.90/Profiles/Doom Eternal -VR/ShaderSwap'
CAPTURE = ROOT / 'captures/20260919-152320'

def identity(source):
    # SPIRV-Cross may emit optional control-flow hints; they do not change
    # instructions, descriptor bindings, constants or data dependencies.
    source = re.sub(r'^\s*#.*$', '', source, flags=re.M)
    source = re.sub(r'\bSPIRV_CROSS_(?:BRANCH|FLATTEN|UNROLL|LOOP)\b', '', source)
    # SPIRV-Cross versions spell scalar inequality either != or !(a == b).
    # Restrict operands to plain names/member accesses and numeric literals;
    # never rewrite relational comparisons, arithmetic, calls or side effects.
    atom = r'(?:\w+(?:\.\w+)*|(?:\(\s*)?-?\s*\d+\.\d+(?:\s*\))?)'
    source = re.sub(r'!\s*\(\s*('+atom+r')\s*==\s*('+atom+r')\s*\)', r'\1 != \2', source)
    names = {}
    source = re.sub(r'\b_\d+(?:_\d+)?\b', lambda m: names.setdefault(m[0], f'anon{len(names)}'), source)
    return canonical(source)

def base_and_tail(source):
    source = re.sub(r'/\*.*?\*/|//[^\n]*', '', source, flags=re.S)
    source = re.sub(r'struct\s+Vk3DStereo\s*\{.*?\}\s*;', '', source, flags=re.S)
    source = re.sub(r'layout\s*\([^)]*\)\s*uniform\s+Vk3DParams\s*\{.*?\}\s*;', '', source, flags=re.S)
    source = re.sub(r'^\s*#(?:extension|define)[^\n]*', '', source, flags=re.M)
    flips = list(re.finditer(r'gl_Position\.y\s*=\s*-gl_Position\.y\s*;', source))
    if not flips:
        return None, ''
    cut = flips[-1].end()
    tail = source[cut:].strip()
    # Original main must end immediately after the flip. Equality below rejects
    # profiles that have original output writes or modified code elsewhere.
    base = source[:cut] + '\n}\n'
    if 'vk3d_params' in base:
        return None, tail
    return identity(base), tail

def profile_hash(data):
    m, mask = 0x5bd1e995, (1 << 64)-1
    h = 0x1000193 ^ (len(data)*m)
    end = len(data)//8*8
    for pos in range(0,end,8):
        k = int.from_bytes(data[pos:pos+8], 'little')*m & mask
        k ^= k >> 47
        h = ((h ^ (k*m & mask))*m) & mask
    if end != len(data):
        h = ((h ^ int.from_bytes(data[end:], 'little'))*m) & mask
    h ^= h >> 47
    h = h*m & mask
    return h ^ (h >> 47)

def main():
    originals = {}
    for path in (CAPTURE/'vertex-originals').glob('*.vert'):
        originals.setdefault(identity(path.read_text()), []).append(path.stem)
    rows = []
    for path in sorted(PROFILE.glob('*.vert')):
        source = path.read_text()
        key, tail = base_and_tail(source)
        matches = originals.get(key, []) if key else []
        rows.append(dict(profile=path.name, sourceSha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                         exactMatches=[dict(sha256=sha, runtimeHash=f'0x{profile_hash((CAPTURE/"spirv"/(sha+".spv")).read_bytes()):016x}') for sha in matches],
                         injection=tail, label=source.splitlines()[0]))
    result = dict(profile=str(PROFILE), method='Exact token identity after removing declarations, appended stereo tail, optional control-flow hints and bijective anonymous-ID renaming. Original instructions, constants, interfaces and dependencies preserved.',
                  automaticActivation=False, shaders=rows)
    output = ROOT/'docs/vk3d-vr-source-audit.json'
    output.write_text(json.dumps(result,indent=2))
    print(json.dumps([r for r in rows if r['exactMatches']],indent=2))

if __name__ == '__main__': main()
