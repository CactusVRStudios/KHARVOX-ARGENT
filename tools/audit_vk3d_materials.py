"""Audit every shipped fragment/compute replacement, retaining a complete ledger.

Exact equality after enumerated patch reversal and semantics-preserving GLSL
normalization is required. Candidate similarity is never an activation rule.
"""
import json, re
from audit_vk3d_vr_profile import ROOT, CAPTURE, PROFILE, identity, profile_hash

RESTORE = {
 '9a21f460a829dd98_PS.frag': [('break;', '_4450 *= dot(_5302, vec3(0.333000004291534423828125));')],
 'f93799f601a997f8_PS.frag': [('break;', '_4556 *= dot(_5408, vec3(0.333000004291534423828125));')],
 'bf1f5ef4cb05c106_PS.frag': [('break;', '_2220 = 0.0;')],
 'c24c34e0950b5324_PS.frag': [('break;', '_2505 = 0.0;')],
}

def normalize(s):
    # Index decorations are recorded and ported separately, not discarded at runtime.
    s = re.sub(r'nonuniformEXT\s*\(\s*(_\d+)\s*\)', r'\1', s)
    # Fold only an adjacent vec4 temporary with exactly one use in the entire module.
    pattern = r'vec4 (_\d+) = ([^;]+);\s*vec4 (_\d+) = \1;'
    def fold(m):
        if len(re.findall(r'\b'+m[1]+r'\b',s)) != 2: return m[0]
        return 'vec4 '+m[3]+' = '+m[2]+';'
    s = re.sub(pattern, fold, s)
    # uvec4(bvec4) and mix(0u, 1u, bvec4) both produce componentwise 0/1.
    s = re.sub(r'mix\s*\(\s*uvec4\s*\(\s*0u\s*\)\s*,\s*uvec4\s*\(\s*1u\s*\)\s*,', 'uvec4(', s)
    s = re.sub(r'!\s*\(\s*(\(\s*_\d+\.\w+\s*\*\s*_\d+\.\w+\s*\))\s*==\s*0\.0\s*\)',r'\1 != 0.0',s)
    return s

def restore(name,s):
    if name in RESTORE:
        # Restore from the original comment at its exact location before preprocessing.
        # Preprocessed module has lost the comment, so locate the adjacent block.
        original=(PROFILE/name).read_text()
        statement=RESTORE[name][0][1]
        match=re.search(r'//\s*'+re.escape(statement),original)
        assert match
        prior=[l.strip() for l in original[:match.start()].splitlines() if l.strip() and not l.strip().startswith('//')]
        before=prior[-1]
        if before=='{':
            # Unique terminal shadow switch case, identified by its original comment.
            tail=prior[-2]
            anchor=tail+'\n'
            # GLSL preprocessor preserves this case label; whitespace is flexible.
            pattern=re.escape(tail)+r'\s*\{\s*break;'
            s,n=re.subn(pattern,tail+'\n{\n'+statement+'\nbreak;',s)
            assert n==1,(name,tail,n)
        else:
            # normalize preprocessing whitespace only for matching the anchor.
            compact=lambda x: re.sub(r'\s+','',x)
            lines=s.splitlines();matches=[i for i,l in enumerate(lines) if compact(l)==compact(before)]
            assert len(matches)==1,(name,before,matches)
            lines.insert(matches[0]+1,statement);s='\n'.join(lines)
    if name.startswith('477568079d7c9307'):s=re.sub(r'_269\.x\s*=\s*0\s*;','',s)
    if name.startswith('f12fdb60a4168643'):s=s.replace('_438 * 0.25','_438')
    if name in ['20204ab2606da2c8_CS.comp','3346c26301ca4ff1_CS.comp','98b4463e74409334_CS.comp','cd58672b1a74bfa7_CS.comp']:
        s,n=re.subn(r'(float _\d+ = )(_\d+\._m6);',r'\1min(0.9900000095367431640625, \2);',s)
        assert n==1
    if name=='65a505641f1da7be_33261b19865c1dca_CS.comp':
        s=re.sub(r'_165\.[xy]\s*=\s*0\s*;','',s)
        s=s.replace('_172 = textureLod(', '_172 = 1.0 - textureLod(').replace('vec4(0)', 'vec4(0.0)')
    if name=='7a6052a965cae7d8_CS.comp':s=re.sub(r'if\s*\(\s*0\s*==\s*0\s*\)','',s)
    if name=='d9b3c836c4f2a473_e69aa3d20eee759f_CS.comp':
        statements=['_4699 += (_1547._m20 * mix(_4419 * (float(!_2448) * _4702), vec3(1.0), vec3(_2563)));', '_4699 += ((mix(_1547._m23, _4521, vec3(_4738)) * _1547._m24) * mix(_4419, vec3(1.0), vec3(_2563)));']
        s=s.replace('_4702 *= _4702;', '_4702 *= _4702;\n'+statements[0])
        s=s.replace('float _4738 = _10(_4746) * 1;', 'float _4738 = _10(_4746) * _4520;\n'+statements[1])
    return s

def main():
    rows=[]
    for kind,ext in [('fragment','frag'),('compute','comp')]:
        originals={}
        for p in (CAPTURE/f'{kind}-originals').glob('*.'+ext):
            originals.setdefault(identity(normalize(p.read_text())),[]).append(p)
        for p in sorted((ROOT/f'reference/eternal-vr-0.90/{kind}-mono').glob('*.'+ext)):
            s=restore(p.name,p.read_text())
            found=originals.get(identity(normalize(s)),[])
            indices=sorted(set(re.findall(r'nonuniformEXT\s*\(\s*(_\d+)\s*\)',s)))
            matches=[]
            for f in found:
                # Index names must be identical in the normalized module as well;
                # bijective identity alone is insufficient for reusing numeric IDs.
                from match_vertex_profile import canonical
                def named(x):
                    x=normalize(x)
                    x=re.sub(r'^\s*#.*$|\bSPIRV_CROSS_\w+\b','',x,flags=re.M)
                    atom=r'(?:\w+(?:\.\w+)*|(?:\(\s*)?-?\s*\d+\.\d+(?:\s*\))?)'
                    x=re.sub(r'!\s*\(\s*('+atom+r')\s*==\s*('+atom+r')\s*\)',r'\1 != \2',x)
                    return canonical(x)
                same=named(s)==named(f.read_text())
                matches.append(dict(sha256=f.stem,runtimeHash=f'0x{profile_hash((CAPTURE/"spirv"/(f.stem+".spv")).read_bytes()):016x}',sameNumericIds=same,nonuniformIndices=indices if same else []))
            rows.append(dict(profile=p.name,exactMatches=matches,reviewedRestoration=p.name in RESTORE or p.name.startswith(('477568079','f12fdb60'))))
    (ROOT/'docs/vk3d-material-audit.json').write_text(json.dumps(dict(method=__doc__,shaders=rows),indent=2))
    print(json.dumps(rows,indent=2))

if __name__=='__main__':main()
