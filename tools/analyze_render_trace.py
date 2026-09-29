"""Join submitted command samples to actual shader and attachment identities.

Pass labels are investigation candidates, never automatic stereo policies.
"""
import argparse
import collections
import json
import pathlib
import struct

def analyze(root):
    objects=collections.defaultdict(list)
    for line in (root/'render-objects.tsv').read_text().splitlines():
        c=line.split('\t')
        if len(c)!=10:continue
        frame,device,kind,handle=int(c[0]),int(c[1]),c[2],int(c[3])
        objects[(kind,handle)].append((frame,[int(x) for x in c[4:]]))
    def obj(kind,handle,frame):
        rows=objects.get((kind,handle),[])
        return next((args for f,args in reversed(rows) if f<=frame),None)
    pipelines=collections.defaultdict(list)
    for kind,name in [('graphics','graphics.tsv'),('compute','compute.tsv')]:
        for line in (root/name).read_text().splitlines():
            c=line.split('\t')
            if kind=='graphics' and len(c)>=10:handle,frame,stage=int(c[8]),int(c[9]),int(c[4])
            elif kind=='compute' and len(c)>=6:handle,frame,stage=int(c[4]),int(c[5]),32
            else:continue
            pipelines[handle].append((frame,stage,c[1]))
    def shaders(handle,frame):
        selected={}
        for f,stage,sha in pipelines.get(handle,[]):
            if f<=frame:selected[stage]=sha
        return selected
    states={};passes={};frames=set();issues=collections.Counter();counts=collections.Counter()
    for line in (root/'render-trace.tsv').read_text().splitlines():
        c=line.split('\t')
        if len(c)!=11:continue
        frame,queue,cmd,epoch=map(int,c[:4]);event=c[4];a=list(map(int,c[5:]));frames.add(frame);counts[event]+=1
        key=(frame,queue,cmd,epoch)
        state=states.setdefault(key,{'pass':0,'fb':0,'width':0,'height':0,'graphics':0,'compute':0,'subpass':0})
        if event in ('pass','inherit'):
            state.update({'pass':a[0],'fb':a[1],'subpass':a[2] if event=='inherit' else 0})
            if event=='pass':state.update(width=a[2],height=a[3])
            elif (fb:=obj('framebuffer',a[1],frame)):state.update(width=fb[1],height=fb[2])
        elif event=='viewport' and a[0]==0:state['viewport']=[struct.unpack('<f',struct.pack('<I',v))[0] for v in a[1:5]]
        elif event=='scissor' and a[0]==0:state['scissor']=[v-(1<<64) if v>=(1<<63) else v for v in a[1:3]]+a[3:5]
        elif event=='end_pass':state.update({'pass':0,'fb':0,'subpass':0,'width':0,'height':0})
        elif event=='next_subpass':state['subpass']+=1
        elif event=='pipeline':state['compute' if a[0]==1 else 'graphics']=a[1]
        elif event in ('missing_command','truncated'):issues[event]+=1
        elif event.startswith('draw') or event.startswith('dispatch'):
            compute=event.startswith('dispatch');pipeline=state['compute' if compute else 'graphics'];ss=shaders(pipeline,frame)
            if not ss:issues['unmapped_pipeline']+=1
            group=(frame,state['pass'],state['fb'],state['subpass'],state['width'],state['height'],tuple(sorted(ss.items())),compute)
            if group not in passes:
                attachments=[];fb=state['fb']
                # Framebuffer attachment rows can repeat on recreation: use the
                # newest creation frame and the newest row for each slot.
                fb_args=obj('framebuffer',fb,frame);slots={}
                for f,args in objects.get(('attachment',fb),[]):
                    if f<=frame:slots[args[0]]=args[1]
                for slot,view in slots.items():
                    if fb_args and slot>=fb_args[4]:continue
                    v=obj('view',view,frame);im=obj('image',v[0],frame) if v else None
                    attachments.append({'slot':slot,'view':view,'image':v[0] if v else None,'format':v[2] if v else None,'width':im[0] if im else None,'height':im[1] if im else None,'layers':im[4] if im else None,'usage':im[5] if im else None,'aspect':v[5] if v else None})
                color=any(x['aspect'] is not None and x['aspect']&1 for x in attachments)
                depth=any(x['aspect'] is not None and x['aspect']&6 for x in attachments)
                label='compute_requires_resource_analysis' if compute else 'color_and_depth_candidate' if color and depth else 'depth_only_candidate' if depth else 'color_only_candidate' if color else 'unknown_attachments'
                passes[group]={'frame':frame,'render_pass':state['pass'],'framebuffer':fb,'subpass':state['subpass'],'extent':[state['width'],state['height']],'shaders':ss,'attachments':attachments,'candidate':label,'draw_or_dispatch_calls':0,'direct_work_items':0}
            passes[group]['draw_or_dispatch_calls']+=1
            if not compute:
                rectangles=passes[group].setdefault('dynamic_rectangles',[])
                rect={k:state[k] for k in ('viewport','scissor') if k in state}
                if rect and rect not in rectangles:rectangles.append(rect)
            if event in ('draw','draw_indexed'):passes[group]['direct_work_items']+=a[0]*a[1]
            if event=='dispatch':passes[group]['direct_work_items']+=a[0]*a[1]*a[2]
    return {'summary':{'sampled_present_indices':sorted(frames),'events':dict(counts),'issues':dict(issues),'pass_shader_groups':len(passes),'policy_activation_allowed':False},'passes':list(passes.values()),'limitations':['Sampled CPU submission trace, not GPU timestamps or pixel proof.','Candidate labels do not distinguish world, HUD, shadows or cinematics semantically.','Only intercepted command APIs are covered; missing or unsupported paths must be audited.']}

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('capture',type=pathlib.Path);args=p.parse_args();report=analyze(args.capture)
    (args.capture/'render-analysis.json').write_text(json.dumps(report,indent=2));print(json.dumps(report['summary'],indent=2))
