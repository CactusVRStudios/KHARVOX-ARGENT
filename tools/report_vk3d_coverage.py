"""Account for every shipped replacement and rendering policy, including limits."""
import json
from audit_vk3d_vr_profile import ROOT,PROFILE

def main():
 files=['vk3d-vr-source-audit.json','vk3d-material-audit.json','vk3d-light-culling-audit.json','vk3d-vertex-complete-audit.json']
 associations={}
 for name in files:
  data=json.loads((ROOT/'docs'/name).read_text());rows=data['shaders'] if isinstance(data,dict) else data
  for row in rows:
   for m in row.get('exactMatches',[]):associations.setdefault(row['profile'],{})[m['sha256']]=m
 rules=json.loads((ROOT/'docs/vk3d-runtime-rules.json').read_text())
 covered={p for r in rules for p in r['profiles']}
 rows=[]
 for p in sorted(PROFILE.iterdir()):
  if not p.is_file():continue
  matches=list(associations.get(p.name,{}).values())
  if p.suffix=='.rgen':status='Reviewed: ray launch/image-array separation and fixed stereo constants; inactive because ray tracing is disabled and the SFS compiler does not transform ray stages.'
  elif p.name=='d9b3c836c4f2a473_e69aa3d20eee759f_CS.comp':status='Common texture-index and OpenXR reconstruction correction ported. Variant-only reflection suppression not globally applied: generic and variant share a captured module; pipeline association remains unresolved.'
  elif p.name in covered:status='Reviewed corrections ported as exact-identity edits; see runtime-rules manifest for OpenXR adaptations and quad gating.'
  elif p.suffix=='.vert':status='Reviewed explicit world/UI/shared policy; projection adapted to OpenXR instead of copying fixed display separation.'
  else:status='Reviewed automatic per-eye 2D image conversion or existing shared-volume correction; no additional source change required.'
  assert matches or p.suffix=='.rgen',p.name
  rows.append(dict(profile=p.name,stage=p.suffix[1:],status=status,exactMatches=matches))
 report=dict(total=len(rows),associatedNonRay=sum(bool(r['exactMatches']) for r in rows),rayInactive=3,shaderReplacements=rows,
  configuration={
   'singleFrameStereo':'Vulkan multiview; 2D images use eye layers; 3D images and SSBOs stay shared.',
   'computeDispatch':'Reviewed all six opaque Vk3D hashes. Their hash algorithm is not established; preserve verified shared-write dispatch-once policy instead of pretending our runtime hashes are equivalent.',
   'renderPass':'Only shipped entry has hash=0, all filters=-1, Execution=Mono/Creation=Stereo. Not treated as a global mono override without a matching runtime pass contract.',
   'scissor':'Shipped ScissorClipping section is empty.',
   'projection':'Generic vertex final-Y injection plus explicit overrides mapped to captured identities; OpenXR FOV/IPD replaces fixed separation/convergence.',
   'ui':'Menu uses correct sRGB OpenXR quad; known in-world UI uses screen projection. Vk3D custom_params HUD translations are display/controller-specific and are not copied as arbitrary offsets.',
   'camera':'Verified engine basis setter active; other binary hook fragments remain audited but uninstalled pending exact object/lifetime contracts.',
   'postprocessing':'OpenVR NIS 0.77 render scale/sharpening is a separate D3D11 compositor plugin; not a missing Vulkan light-list correction and not installed into OpenXR.',
   'performance':'r_skipLightGPUCulling stays 0. Conservative shader-local Vk3D culling workarounds can increase candidate lighting work; gameplay performance must be measured.',
   'validation':'Exact normalized source associations and real captured-module compilation do not prove pixel correctness in headset.'})
 (ROOT/'docs/vk3d-complete-coverage.json').write_text(json.dumps(report,indent=2))
 print(json.dumps({k:v for k,v in report.items() if k not in ['shaderReplacements','configuration']},indent=2))
if __name__=='__main__':main()
