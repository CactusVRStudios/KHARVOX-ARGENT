"""Generate only reviewed exact-identity edits, never complete shader swaps."""
import json,re,pathlib
from audit_vk3d_vr_profile import ROOT,CAPTURE
from audit_vk3d_materials import RESTORE

def main():
 rows=json.loads((ROOT/'docs/vk3d-material-audit.json').read_text())['shaders']
 light=json.loads((ROOT/'docs/vk3d-light-culling-audit.json').read_text())['shaders']
 rules={};fixtures={};profiles={}
 world='argentProjection.diagnostics.y > 0.5'
 def add(m,name,before,after,all=False,uniform=False):
  h=m['runtimeHash'];r=rules.setdefault(h,dict(world=False,edits=[]));r['world']|=uniform
  edit=(before,after,all)
  if edit not in r['edits']:r['edits'].append(edit)
  fixtures[h]=m['sha256'];profiles.setdefault(h,set()).add(name)
 for row in rows:
  name=row['profile'];kind='fragment' if name.endswith('.frag') else 'compute'
  profile=(ROOT/f'reference/eternal-vr-0.90/{kind}-mono'/name).read_text()
  for m in row['exactMatches']:
   original=(CAPTURE/f'{kind}-originals'/(m['sha256']+('.frag' if kind=='fragment' else '.comp'))).read_text()
   if m['sameNumericIds']:
    for array,index in re.findall(r'(_\d+)\[nonuniformEXT\((_\d+)\)\]',profile):
     add(m,name,f'{array}[{index}]',f'{array}[nonuniformEXT({index})]',True)
   if name in RESTORE:
    statement=RESTORE[name][0][1];assert original.count(statement)==1
    add(m,name,statement,f'if (!({world})) {{ {statement} }}',uniform=True)
   if name.startswith('477568079'):
    anchor='vec4 _282 = textureGather('
    add(m,name,anchor,f'if ({world}) _269.x = 0.0;\n    '+anchor,uniform=True)
   if name.startswith('f12fdb60'):
    add(m,name,'_438 = max(vec3(0.0), _438);',f'_438 = max(vec3(0.0), _438 * ({world} ? 0.25 : 1.0));',uniform=True)
   if name in ['20204ab2606da2c8_CS.comp','3346c26301ca4ff1_CS.comp','98b4463e74409334_CS.comp','cd58672b1a74bfa7_CS.comp']:
    found=re.findall(r'min\(0\.9900000095367431640625, (_\d+\._m6)\)',original);assert len(found)==1
    before=f'min(0.9900000095367431640625, {found[0]})'
    add(m,name,before,f'({world} ? {found[0]} : {before})',uniform=True)
   if name.startswith('65a505'):
    # Same workaround as Vk3D, gated to world rendering; original quad path retained.
    anchor='_172 = 1.0 - textureLod('
    add(m,name,anchor,f'if ({world}) _165 = vec2(0.0);\n        '+anchor,uniform=True)
    anchor='    groupMemoryBarrier();'
    add(m,name,anchor,f'    if ({world} && _119 == 32u) _172 = 1.0 - _172;\n'+anchor,uniform=True)
 for row in light:
  for m in row['exactMatches']:
   name=row['profile'];s=(CAPTURE/'compute-originals'/(m['sha256']+'.comp')).read_text()
   for gate in row['restoredGates']:
    before='if ('+gate+')'
    if row['kind'] in ('decals_coarse','decals_fine'):
     # ARGENT looks up shared decal lists in native center-camera coordinates.
     # Keep native volume/tile intersection and coarse depth-slice rejection:
     # bypassing them fills all 24 slices and overflows the 63-ID tile lists
     # (GPU capture 91938937). Fine eye-HiZ rejection remains disabled below.
     # Retain an identity edit to validate the exact original gates as well.
     add(m,name+' (native decal geometry retained)',before,before)
     continue
    if row['kind']=='lights_coarse' or (row['kind']=='lights_fine' and gate in ('_325 < 1000000015047466219876688855040.0','_975')):
     # Shared native-space light/reflection lists use the same center-camera
     # coordinates as decals. Preserve geometric intersection and depth slices.
     # Mode 5 is a live A/B of the previous broad light candidates only.
     add(m,name+' (native light geometry)',before,
         'if ((('+world+') && argentProjection.diagnostics.w == 5.0) || ('+gate+'))',uniform=True)
     continue
    if gate=='_996 == true':
     before=re.search(r'if \(_996 == true\)\s*\{\s*uint _2068',s)[0]
    add(m,name,before,before.replace('if ('+gate+')','if (('+world+') || ('+gate+'))'),uniform=True)
   if row['restoredDepthSamples']:
    variables=['_942','_962'] if row['kind']=='lights_fine' else ['_888','_908']
    for var,value in zip(variables,['0.0','1.0']):
     # Runtime has already promoted texelFetch to an array, so wrap the RHS
     # through an assignment after its declaration, with a stable next-line anchor.
     match=re.search(r'float '+var+r' = [^;]+;\s*([^\n]+)',s);assert match
     nextline=match[1].strip()
     if nextline.startswith('float '):nextline=nextline.split('=')[0]+'='
     # Shared center-grid producers must not use a single eye's HiZ bounds,
     # including stereo cinematics where gameplay effect overrides stay off.
     add(m,name,nextline,f'if (argentProjection.diagnostics.x > 0.5) {var} = {value};\n        '+nextline,uniform=True)
   if row['kind'].endswith('_bounds'):
    anchor='_84 = clamp(_84, vec2(0.0), vec2(1.0));'
    # Vk3D variant pads BEFORE clamping. Retain the original nonnegative clamp
    # to avoid unsigned tile coordinates wrapping below zero at the left edge.
    add(m,name,anchor,f'if ({world}) {{ _84.x -= 0.05; _87.x += 0.05; }}\n    '
        f'if ({world} && argentProjection.diagnostics.w == 4.0) {{ _84 = vec2(0.0); _87 = vec2(1.0); }}\n    '+anchor,uniform=True)
 for row in json.loads((ROOT/'docs/vk3d-vertex-complete-audit.json').read_text()):
  for m in row['exactMatches']:
   name=row['profile'];original=(CAPTURE/'vertex-originals'/(m['sha256']+'.vert')).read_text()
   if row['kind']=='particle_interpolation':
    found=re.findall(r'min\(0\.9900000095367431640625, (_\d+\._m6)\)',original);assert len(found)==1
    before=f'min(0.9900000095367431640625, {found[0]})'
    add(m,name,before,f'({world} ? {found[0]} : {before})',uniform=True)
   if row['kind']=='hud_depth':
    before='gl_Position.z = _12(_600, _602);'
    add(m,name,before,f'gl_Position.z = {world} ? 0.0 : _12(_600, _602);',uniform=True)
   if row['kind']=='disable_duplicate_glass':
    before='void main()\n{'
    add(m,name,before,before+f'\n    if ({world}) {{ gl_Position = vec4(2.0, 2.0, 2.0, 1.0); return; }}',uniform=True)
 # Vk3D marks divergent descriptor indices in its material replacements.
 # Apply the same descriptor contract to captured material permutations,
 # including those without a dedicated ShaderSwap file. Never decorate SSBO
 # indices or select shaders by source shape at runtime.
 material_rows=json.loads((ROOT/'docs/eternal-light-grid-audit.json').read_text())['shaders']
 for m in material_rows:
  source=(CAPTURE/'fragment-originals'/(m['sha256']+'.frag')).read_text()
  if m.get('smallGrid'):
   # The first dual-list selection is the decal list, before decal-volume
   # transforms and textureGrad. Keep the original default; diagnostics.z
   # permits an in-scene comparison with the conservative coarse list.
   start=source.index('uvec2 '+m['smallGrid']+' =')
   selection=re.search(r'if \((_\d+) < (_\d+)\)\s*\{\s*(_\d+) = \1;\s*(_\d+) = (_\d+);\s*(_\d+) = (_\d+);',source[start:])
   assert selection,m['runtimeHash']
   before='if ('+selection[1]+' < '+selection[2]+')'
   assert source.count(before)==1
   add(m,'Decal coarse-list diagnostic',before,'if ((argentProjection.diagnostics.y > 0.5 && argentProjection.diagnostics.z > 0.5) || ('+selection[1]+' < '+selection[2]+'))',uniform=True)
  arrays=re.findall(r'layout\([^\n]+\) uniform texture\w+ (_\d+)\[\d+\];',source)
  for array in arrays:
   for index in sorted(set(re.findall(re.escape(array)+r'\[(_\d+)\]',source))):
    add(m,'Vk3D material descriptor contract (captured permutation)',f'{array}[{index}]',f'{array}[nonuniformEXT({index})]',True)
 out=['// Generated by tools/generate_vk3d_rules.py; exact identities in docs/vk3d-*.json.']
 for h,r in sorted(rules.items()):
  out.append('// '+', '.join(sorted(profiles[h])))
  out.append('{'+h+'ull,'+str(r['world']).lower()+', {')
  for before,after,all in r['edits']:out.append(' {'+json.dumps(before)+','+json.dumps(after)+','+str(all).lower()+'},')
  out.append('}},')
 (ROOT/'src/sfs/EternalVk3dRules.inc').write_text('\n'.join(out)+'\n')
 (ROOT/'docs/vk3d-runtime-rules.json').write_text(json.dumps([dict(hash=h,sha256=fixtures[h],profiles=sorted(profiles[h]),**r) for h,r in sorted(rules.items())],indent=2))
 print(f'{len(rules)} exact modules, {sum(len(r["edits"]) for r in rules.values())} edits')

if __name__=='__main__':main()
