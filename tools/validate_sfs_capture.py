"""Offline SFS feasibility audit; never installs transformed shaders."""
import argparse
import concurrent.futures
import json
import pathlib
import subprocess

def main():
    p=argparse.ArgumentParser()
    p.add_argument('capture',type=pathlib.Path)
    p.add_argument('--compiler',type=pathlib.Path,default=pathlib.Path('build/Release/ArgentSfsTranscode.exe'))
    p.add_argument('--glslang',type=pathlib.Path,default=pathlib.Path(r'D:\DoomVR\build\shader-tools\glslang-main\bin\glslang.exe'))
    p.add_argument('--in-process',action='store_true',help='Use the ported KHARVOX glslang backend')
    args=p.parse_args()
    audit=json.loads((args.capture/'audit.json').read_text())
    # Choose a globally unused slot from this capture, instead of assuming a
    # DOOM 2016 or provider binding. Pipeline layout compatibility is separate.
    trial_set=0
    trial_binding=1+max((b['binding'] for s in audit['shaders'] for b in s['bindings'] if b['set']==trial_set),default=-1)
    out=args.capture/('sfs-in-process' if args.in_process else 'sfs-offline');out.mkdir(exist_ok=True)
    def one(shader):
        sha=shader['sha256'];model=shader['entrypoints'][0]['model']
        if model not in (0,4,5):return {'sha':sha,'stage':model,'status':'unsupported_stage'}
        stage={0:'vert',4:'frag',5:'comp'}[model]
        source=out/(sha+('.spv' if args.in_process else '.'+stage))
        command=[str(args.compiler),str(args.capture/'spirv'/(sha+'.spv')),str(source)]
        if model==0:command+=[str(trial_set),str(trial_binding)]
        result=subprocess.run(command,capture_output=True,text=True,timeout=60)
        if result.returncode==0 and not args.in_process:
            result=subprocess.run([str(args.glslang),'-V','--target-env','vulkan1.1','-S',stage,'-o',str(out/(sha+'.spv')),str(source)],capture_output=True,text=True,timeout=60)
        status='compiled' if result.returncode==0 else 'failed'
        if status=='failed':(out/(sha+'.log')).write_text(result.stdout+result.stderr)
        return {'sha':sha,'stage':model,'status':status,'error':(result.stdout+result.stderr)[-1800:] if status=='failed' else ''}
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:rows=list(pool.map(one,audit['shaders']))
    counts={s:sum(r['status']==s for r in rows) for s in ('compiled','failed','unsupported_stage')}
    report={'counts':counts,'runtime_activation':False,'projection_trial':{'set':trial_set,'binding':trial_binding,'validated_for_game':False},'limitations':['Compilation only; no camera classification or resource parity proof.','Compute trial assumes doubled Z dispatch; shared SSBO/atomic writes need separate policy.','Ray tracing stages are unsupported.'],'shaders':rows}
    (out/'report.json').write_text(json.dumps(report,indent=2))
    print(json.dumps(counts,indent=2))

if __name__=='__main__':main()
