"""Decode opt-in mapped input snapshots using the actual SPIR-V member offsets.

This reports inputs, not GPU list contents or proof of a visual correction.
"""
import argparse
import json
import pathlib
import struct
import subprocess
import collections

ROOT = pathlib.Path(__file__).resolve().parents[1]
SHADERS = {
    'world-material': '6378d9197bc1fbf36aefcef4a64e876f9cb16ce583833718e15372326ad93570',
    'decal-bounds': 'fdf9deea0da0466a80972d8d1c8c7bd8ee8acab749c1046b114a93ca273c301a',
    'decal-coarse': '526d84676aeb069214ffdb18885420888f0e8f2f112588c1748e01154b916b61',
    'decal-fine': '125ee2f9bca68c76b1b72203dcdbee998a5e1fde3ed9d0f5e1a31a5bb9a026c8',
    'light-bounds': '477234f2c7e66623eecfe6d65fd41b82ce537139a070c9ffd6472dbc3ef0b74a',
}


def reflect(label):
    spv = ROOT / 'captures/20260919-152320/spirv' / (SHADERS[label] + '.spv')
    exe = ROOT / 'build/spirv-cross/Release/spirv-cross.exe'
    return json.loads(subprocess.run([str(exe), str(spv), '--reflect'],
                                    check=True, capture_output=True, text=True).stdout)


def decode(data, reflection):
    block = next(u for u in reflection['ubos'] if u['set'] == 0 and u['binding'] == 0)
    members = reflection['types'][block['type']]['members']
    result = {}
    for m in members:
        if 'array' in m or m['type'] not in ['float', 'uint', 'vec2', 'vec3', 'vec4']:
            continue
        n = int(m['type'][-1]) if m['type'].startswith('vec') else 1
        if m['offset'] + n * 4 > len(data):
            continue
        result[m['name']] = dict(offset=m['offset'],
            floats=struct.unpack_from('<' + 'f' * n, data, m['offset']),
            uints=struct.unpack_from('<' + 'I' * n, data, m['offset']))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('manifest', type=pathlib.Path)
    args = parser.parse_args()
    rows = []
    for line in args.manifest.read_text(encoding='utf-8').splitlines():
        label, pipeline, descriptor, slot, buffer, offset, extent, size, available, filename = line.split('\t')
        row = dict(shader=label, pipeline=pipeline, descriptorSet=descriptor, binding=int(slot),
                   buffer=buffer, offset=int(offset), range=int(extent), bytes=int(size),
                   mapped=available == '1')
        path = args.manifest.parent / filename
        if available == '1' and slot == '0':
            row['members'] = decode(path.read_bytes(), reflect(label))
        if slot in ('15', '27', '35') and path.exists():
            data = path.read_bytes()
            # Each native tile uses 64 uints, header then at most 63 indices.
            headers = [struct.unpack_from('<I', data, o)[0] for o in range(0, len(data) - 255, 256)]
            counts = [h & 255 for h in headers]
            row['gpuLists'] = dict(tileCount=len(counts), histogram=dict(sorted(collections.Counter(counts).items())),
                                   headers=headers, counts=counts)
            print(filename, 'tile count histogram', row['gpuLists']['histogram'])
        rows.append(row)
    output = args.manifest.with_suffix('.json')
    output.write_text(json.dumps(rows, indent=2), encoding='utf-8')
    print(output)
    for row in rows:
        print(row['shader'], 'binding', row['binding'], 'mapped', row['mapped'])
        for name, m in row.get('members', {}).items():
            print(' ', name, 'offset', m['offset'], 'float', m['floats'], 'bits', m['uints'])


if __name__ == '__main__':
    main()
