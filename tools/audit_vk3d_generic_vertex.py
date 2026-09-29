"""Audit Vk3D's configured generic injection against captured Eternal draws.

Produces evidence only. A clip-Y flip alone does not classify shadow, UI or
world geometry and must not activate a new shader at runtime.
"""
import json, re
from audit_vk3d_vr_profile import ROOT, CAPTURE, profile_hash

def main():
    config=ROOT/'reference/eternal-vr-0.90/Profiles/Doom Eternal -VR/Vk3DVision.ini'
    text=config.read_text()
    if 'ShaderInjectionPoint1 = "gl_Position.y = -gl_Position.y"' not in text or 'ShaderType1 = Vertex' not in text:
        raise ValueError('Reference generic injection changed')
    reference=json.loads((ROOT/'docs/vk3d-vr-source-audit.json').read_text())['shaders']
    overrides={m['sha256']:row for row in reference for m in row['exactMatches']}
    profile=(ROOT/'src/sfs/EternalProfile.h').read_text()
    active={int(h,16) for h in re.findall(r'0x([0-9a-f]+)ull',profile)}
    submitted={}
    for row in json.loads((CAPTURE/'render-analysis.json').read_text())['passes']:
        if row['frame']>=1800 and '1' in row['shaders']:
            submitted.setdefault(row['shaders']['1'],[]).append({k:row[k] for k in ('frame','extent','candidate','draw_or_dispatch_calls')})
    rows=[]
    for path in sorted((CAPTURE/'vertex-originals').glob('*.vert')):
        source=path.read_text();flip='gl_Position.y = -gl_Position.y;'
        if flip not in source:continue
        h=profile_hash((CAPTURE/'spirv'/(path.stem+'.spv')).read_bytes())
        override=overrides.get(path.stem)
        rows.append(dict(sha256=path.stem,runtimeHash=hex(h),explicitArgentPolicy=h in active,
            profileOverride=override['profile'] if override else None,
            overrideTail=override['injection'] if override else None,
            clipYFlips=source.count(flip),tail=source[source.rfind(flip):].strip(),
            submitted=submitted.get(path.stem,[])))
    report=dict(automaticActivation=False,method=__doc__,shaders=rows)
    (ROOT/'docs/vk3d-generic-vertex-audit.json').write_text(json.dumps(report,indent=2))
    print(json.dumps(dict(matchingCapturedVertices=len(rows),explicitArgentPolicies=sum(r['explicitArgentPolicy'] for r in rows),submittedWithoutExplicitPolicy=sum(bool(r['submitted']) and not r['explicitArgentPolicy'] for r in rows))))

if __name__=='__main__':main()
