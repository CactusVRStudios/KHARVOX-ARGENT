"""Compare captured native decal-list occupancy with projected input rectangles.

Only fully front-facing volumes through the largest retained decal ID are counted.
The rectangle count is a lower bound for the old bypassed-culling path, not a
prediction of the restored polygon/depth tests. Probe files span submissions.
"""
import argparse
import collections
import json
import math
import pathlib
import struct

from analyze_decal_inputs import decode, reflect


def analyze(manifest):
    rows = [line.split('\t') for line in manifest.read_text(encoding='utf-8').splitlines()]

    def data(label, binding):
        row = next(r for r in rows if r[0] == label and r[3] == str(binding)
                   and (manifest.parent / r[9]).exists())
        return (manifest.parent / row[9]).read_bytes()

    constants = decode(data('decal-coarse', 0), reflect('decal-coarse'))
    bounds = decode(data('decal-bounds', 0), reflect('decal-bounds'))
    width = constants['_m29']['uints'][0]
    height = constants['_m30']['uints'][0]
    fine_base = constants['_m32']['uints'][0]
    if width * height != fine_base or not 0 < fine_base < 100000:
        raise ValueError('Unexpected native tile dimensions')
    scale_x = bounds['_m25']['floats'][0]
    scale_y = bounds['_m26']['floats'][0]
    lists = data('world-material', 15)
    if len(lists) % 256:
        raise ValueError('Incomplete tile list')
    tiles = list(struct.iter_unpack('<64I', lists))
    counts = [t[0] & 255 for t in tiles]
    if any(n > 63 for n in counts):
        raise ValueError('Unexpected list header')
    retained = [i for t, n in zip(tiles, counts) for i in t[1:1+n]]
    maximum_id = max(retained, default=-1)
    corners = data('decal-bounds', 2)
    if (maximum_id + 1) * 128 > len(corners):
        raise ValueError('Corner buffer is too small for retained IDs')
    candidates = [0] * fine_base
    accepted = 0
    for i in range(maximum_id + 1):
        vertices = list(struct.iter_unpack('<4f', corners[i*128:(i+1)*128]))
        if not all(all(math.isfinite(x) for x in v) and v[3] > 0 and v[2] < v[3]
                   for v in vertices):
            continue
        accepted += 1
        xs = [(v[0] / v[3] + 1) * .5 for v in vertices]
        ys = [(1 - v[1] / v[3]) * .5 for v in vertices]
        clamp = lambda x: max(0, min(1, x))
        x0 = math.floor(clamp(min(xs) - .05) * scale_x)
        x1 = math.ceil(clamp(max(xs) + .05) * scale_x)
        y0 = math.floor(clamp(min(ys)) * scale_y)
        y1 = math.ceil(clamp(max(ys)) * scale_y)
        for y in range(y0, y1):
            for x in range(x0, x1):
                candidates[y*width+x] += 1
    saturated = {i for i, n in enumerate(counts[:fine_base]) if n == 63}
    exceeding = {i for i, n in enumerate(candidates) if n > 63}
    return dict(manifest=str(manifest), nativeTiles=[width, height],
                maximumRetainedId=maximum_id, fullyFrontFacingVolumes=accepted,
                fineSaturated=len(saturated), coarseSaturated=counts[fine_base:].count(63),
                fineHistogram=dict(sorted(collections.Counter(counts[:fine_base]).items())),
                coarseHistogram=dict(sorted(collections.Counter(counts[fine_base:]).items())),
                oldBypassCandidateMaximum=max(candidates), oldBypassTilesOverCapacity=len(exceeding),
                oldBypassExceedingMatchesSaturated=exceeding == saturated,
                fineCounts=counts[:fine_base], coarseCounts=counts[fine_base:],
                oldBypassCandidateCounts=candidates)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('manifest', type=pathlib.Path)
    args = parser.parse_args()
    result = analyze(args.manifest)
    output = args.manifest.with_name(args.manifest.stem + '-capacity.json')
    output.write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(json.dumps({k: v for k, v in result.items() if not k.endswith('Counts')}, indent=2))
    print(output)
