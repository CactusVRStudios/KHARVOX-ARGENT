"""Read-only Eternal SPIR-V audit. Provider hashes are candidates, never activation keys."""
import argparse, collections, hashlib, json, pathlib, re, struct

def reflect(data):
    if len(data) < 20 or len(data) % 4:
        raise ValueError('SPIR-V size')
    words = struct.unpack('<%dI' % (len(data)//4), data)
    if words[0] != 0x07230203:
        raise ValueError('SPIR-V magic')
    names, decorations, types, constants, variables, members, entries = {}, {}, {}, {}, [], {}, []
    def string(ws):
        return struct.pack('<%dI' % len(ws), *ws).split(b'\0')[0].decode('utf-8', errors='replace')
    index = 5
    while index < len(words):
        size, opcode = words[index] >> 16, words[index] & 65535
        if size == 0 or index + size > len(words):
            raise ValueError('SPIR-V instruction bounds')
        a = words[index+1:index+size]
        if opcode == 5: names[a[0]] = string(a[1:])
        elif opcode == 15: entries.append({'model': a[0], 'name': string(a[2:])})
        elif opcode == 71: decorations.setdefault(a[0], {})[a[1]] = list(a[2:])
        elif opcode == 72: members.setdefault(a[0], {}).setdefault(a[1], {})[a[2]] = list(a[3:])
        elif 19 <= opcode <= 33: types[a[0]] = (opcode, a[1:])
        elif opcode == 43 and len(a) >= 3: constants[a[1]] = a[2]
        elif opcode == 59: variables.append(a[:3])
        index += size
    def describe(t, seen=()):
        if t in seen: return {'recursive': True}
        op, a = types.get(t, (0, ()))
        if op == 32: return describe(a[1], seen+(t,))
        if op == 21: return {'type':'int', 'bits':a[0], 'signed':bool(a[1])}
        if op == 22: return {'type':'float', 'bits':a[0]}
        if op == 23: return {'type':'vector', 'count':a[1], 'element':describe(a[0], seen+(t,))}
        if op == 24: return {'type':'matrix', 'columns':a[1], 'element':describe(a[0], seen+(t,))}
        if op in (28,29): return {'type':'array', 'count':constants.get(a[1]) if op == 28 else None, 'stride':decorations.get(t,{}).get(6), 'element':describe(a[0], seen+(t,))}
        if op == 30: return {'type':'struct', 'members':[{'offset':members.get(t,{}).get(i,{}).get(35), 'value':describe(child,seen+(t,))} for i,child in enumerate(a)]}
        if op == 25: return {'type':'image','dim':a[1],'depth':a[2],'arrayed':a[3],'multisampled':a[4],'sampled':a[5],'format':a[6]}
        if op == 27: return {'type':'sampled_image','image':describe(a[0],seen+(t,))}
        if op == 26: return {'type':'sampler'}
        return {'opcode':op}
    bindings = []
    for t,v,storage in variables:
        dec = decorations.get(v,{})
        if 33 in dec and 34 in dec:
            bindings.append({'set':dec[34][0],'binding':dec[33][0],'name':names.get(v,''),'storage':storage,'shape':describe(t)})
    return {'entrypoints':entries,'bindings':bindings,'has_view_index':any(d.get(11)==[4440] for d in decorations.values())}

def audit(capture, profile):
    captured = {}
    for line in (capture/'shaders.tsv').read_text().splitlines():
        sha,candidate,size = line.split('\t')
        data = (capture/'spirv'/(sha+'.spv')).read_bytes()
        if hashlib.sha256(data).hexdigest() != sha:
            raise ValueError('SHA-256 mismatch: '+sha)
        captured[sha] = {'sha256':sha,'provider_candidate':candidate,'size':int(size),**reflect(data)}
    variants = set()
    graphics = capture/'graphics.tsv'
    if graphics.exists():
        for line in graphics.read_text().splitlines():
            cols=line.split('\t')
            if len(cols)>=4: variants.add((cols[2],cols[3]))
    candidate_hashes={s['provider_candidate'] for s in captured.values()}
    profile_results=[]
    for file in sorted((profile/'ShaderSwap').iterdir()):
        match=re.match(r'^([0-9a-f]+)(?:_([0-9a-f]+))?',file.stem)
        if not match: continue
        base,variant=match.groups()
        profile_results.append({'file':file.name,'base_candidate_match':base in candidate_hashes,'variant_candidate_match':bool(variant and (base,variant) in variants)})
    histogram=collections.Counter()
    for s in captured.values():
        for e in s['entrypoints']:histogram[e['model']]+=1
    summary={'unique_shaders':len(captured),'execution_models':dict(histogram),'profile_files':len(profile_results),'base_candidate_matches':sum(p['base_candidate_match'] for p in profile_results),'variant_candidate_matches':sum(p['variant_candidate_match'] for p in profile_results),'profile_activation_allowed':False,'note':'Hash matches alone do not validate descriptor layouts, camera projection or resources. No replacement is activated.'}
    return {'summary':summary,'profile':profile_results,'shaders':list(captured.values())}

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('capture',type=pathlib.Path);parser.add_argument('--profile',type=pathlib.Path,default=pathlib.Path(r'D:\DoomVR\vk3\Profiles\Doom Eternal'));parser.add_argument('--output',type=pathlib.Path)
    args=parser.parse_args();result=audit(args.capture,args.profile)
    output=args.output or args.capture/'audit.json';output.write_text(json.dumps(result,indent=2),encoding='utf-8');print(json.dumps(result['summary'],indent=2))
