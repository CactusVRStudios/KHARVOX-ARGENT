"""Summarize a recent log interval; wall-time waits are not CPU execution time."""
import argparse
import collections
import json
import pathlib
import re
import statistics

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('log', type=pathlib.Path)
parser.add_argument('--seconds', type=float, default=20)
parser.add_argument('--end-tick', type=int, help='End before a menu/loading transition in an existing log')
parser.add_argument('--output', type=pathlib.Path)
args = parser.parse_args()
lines = args.log.read_text(encoding='utf-8', errors='replace').splitlines()
end = args.end_tick if args.end_tick is not None else max(int(m[1]) for line in lines if (m := re.match(r'(\d+) pid=', line)))
start = end - int(args.seconds * 1000)
stats = collections.defaultdict(list)
maxima = collections.defaultdict(list)
gpu_stats = collections.defaultdict(list)
frames = []
world = menus = pose_matches = pose_misses = 0
for line in lines:
    t = re.match(r'(\d+) pid=', line)
    if not t or not start <= int(t[1]) <= end:
        continue
    if m := re.search(r'FRAME_CPU section=(\w+) samples=\d+ meanMs=([\d.]+)', line):
        stats[m[1]].append(float(m[2]))
        if peak := re.search(r'\bmaxMs=([\d.]+)', line):
            maxima[m[1]].append(float(peak[1]))
    if m := re.search(r'FRAME_GPU section=(\w+) samples=\d+ meanMs=([\d.]+)', line):
        context = re.search(r'\bcontext=(\w+)', line)
        section = m[1] + ('.' + context[1] if context else '')
        gpu_stats[section].append(float(m[2]))
    if m := re.search(r'estimatedAggregateHookMsPerFrame=([\d.]+)', line):
        stats['aggregateCommandHooksEstimate'].append(float(m[1]))
    if m := re.search(r'sampledLookupLockMeanUs=([\d.]+)', line):
        stats['sampledLookupLockMs'].append(float(m[1]) / 1000)
    if m := re.search(r'sampledHookBodyMeanUs=([\d.]+)', line):
        stats['sampledHookBodyMs'].append(float(m[1]) / 1000)
    if m := re.search(r'SFS_GAME_PRESENT count=(\d+).*XRcopied=(\d)', line):
        if m[2] == '1':
            frames.append((int(t[1]), int(m[1])))
    world += 'STEREO_PROJECTION_SUBMITTED' in line
    menus += 'MENU_QUAD_SUBMITTED' in line
    pose_matches += 'STEREO_RENDER_POSE' in line and 'matched=1' in line
    pose_misses += 'STEREO_RENDER_POSE' in line and 'matched=0' in line
result = dict(log=str(args.log), startTick=start, endTick=end, seconds=args.seconds,
              worldReports=world, menuReports=menus, poseMatches=pose_matches, poseMisses=pose_misses,
              sections={k: dict(windows=len(v), meanMs=statistics.mean(v), medianMs=statistics.median(v))
                        for k, v in stats.items()})
for section, values in maxima.items():
    result['sections'][section]['maxReportedMs'] = max(values)
    result['sections'][section]['p95WindowMaximumMs'] = sorted(values)[min(len(values)-1, int(len(values)*.95))]
result['interpretation'] = 'Means are means of reported windows. Maxima capture section stalls, not full frame times. Hook estimates sum parallel threads; fence waits include GPU completion and scheduling. Simulator xrEndFrame is not Virtual Desktop latency.'
result['gpuSections'] = {k: dict(windows=len(v), meanMs=statistics.mean(v), medianMs=statistics.median(v))
                         for k, v in gpu_stats.items()}
if len(frames) > 1 and frames[-1][0] > frames[0][0]:
    result['presentRateFps'] = (frames[-1][1] - frames[0][1]) * 1000 / (frames[-1][0] - frames[0][0])
output = json.dumps(result, indent=2)
print(output)
if args.output:
    args.output.write_text(output, encoding='utf-8')
