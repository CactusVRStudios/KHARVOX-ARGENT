"""Find exact captured material accesses to Eternal's shared light-list grids.

This produces a reviewable manifest only; it does not activate runtime rules.
The 256-pixel/24-depth-slice and optional 32-pixel lists share buffer indices.
Require an independently used screen-to-texture inverse viewport expression.
"""
import json
import pathlib
import re
from audit_vk3d_vr_profile import ROOT, CAPTURE, profile_hash


def inspect(source):
    matches = list(re.finditer(
        r'uvec2 (_\d+) = uvec2\((_\d+)\._m17.xy\);\s*'
        r'uint (_\d+) = \1\.x / 256u;\s*uint (_\d+) = \1\.y / 256u;', source))
    if len(matches) != 1:
        return None
    m = matches[0]
    tail = source[m.end():m.end()+420]
    depth = re.search(r'float (_\d+) = log2\(max\(1\.0, (_\d+) / (_\d+)\._m\d+\.z\)\)', tail)
    if not depth or '* 24u)' not in tail:
        return None
    common, material = depth[3], m[2]
    normalized = re.findall(
        r'(?:'+re.escape(material)+r'\._m17\.xy|vec4\(gl_FragCoord.xy, 1.0 - gl_FragCoord.z, gl_FragCoord.w\).xy)'
        r' \* ('+re.escape(common)+r'\._m\d+\.xy)', source)
    candidates = sorted(set(normalized))
    if len(candidates) != 1:
        return dict(status='unresolved inverse viewport', pixels=material+'._m17.xy')
    small = list(re.finditer(r'uvec2 (_\d+) = uvec2\('+re.escape(material)+r'\._m17.xy\);\s*uint (_\d+) = \1\.x / 32u;', source))
    if len(small) > 1:
        return None
    return dict(status='reviewed grid pattern', pixels=material+'._m17.xy', tileX=m[3], tileY=m[4],
                depth=depth[2], inverseViewport=candidates[0], anchor='float '+depth[1]+' = log2(',
                smallGrid=small[0][1] if small else '')


def main():
    rows=[]
    for p in sorted((CAPTURE/'fragment-originals').glob('*.frag')):
        rule=inspect(p.read_text())
        if rule:
            rows.append(dict(sha256=p.stem,runtimeHash=f'0x{profile_hash((CAPTURE/"spirv"/(p.stem+".spv")).read_bytes()):016x}',**rule))
    out=ROOT/'docs/eternal-light-grid-audit.json'
    out.write_text(json.dumps(dict(automaticActivation=False,method=__doc__,shaders=rows),indent=2))
    print(json.dumps(dict(total=len(rows),resolved=sum(r['status']=='reviewed grid pattern' for r in rows))))


if __name__=='__main__':
    main()
