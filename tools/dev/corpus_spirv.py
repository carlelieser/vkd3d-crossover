import os, subprocess, sys
C=os.path.expanduser('~/Library/Application Support/CrossOver/Bottles/vkd3d-test/drive_c/corpus/out')
D='build/dxil-cli/dxil-spirv'; O='build/corpus_spv'
tags={'.root-constant.':['--root-constant','0','0','4','12'],'.demote-to-helper.':['--enable-shader-demote'],'.i8dot.':['--enable-shader-i8-dot'],
 '.dual-source-blending.':['--enable-dual-source-blending'],'.input-attachment.':['--input-attachments'],'.typed-uav-without-format.':['--typed-uav-read-without-format'],
 '.native-fp16.':['--min-precision-native-16bit'],'.16bit-io.':['--storage-input-output-16bit'],'.invariant.':['--invariant-position'],
 '.root-descriptor.':['--root-descriptor','cbv','0','0','--root-descriptor','srv','0','0','--root-descriptor','uav','0','0','--root-descriptor','uav','0','1'],
 '.inline-ubo.':['--root-constant-inline-ubo','6','1'],'.cbv-as-ssbo.':['--bindless-cbv-as-ssbo'],'.uav-counter-texel-buffer.':['--uav-counter-force-texel-buffer'],
 '.uav-counter-ssbo.':['--uav-counter-force-ssbo'],'.noderivs.':['--no-compute-shader-derivatives'],'.extended-robustness.':['--extended-robustness'],
 '.view-instancing.':['--view-instancing','--meta-descriptor','2','3','10','22'],'.heap-robustness.':['--descriptor-heap-robustness'],
 '.stream-out.':['--stream-output','SV_Position','0','16','32','1']}
fails=[]
for f in sorted(os.listdir(C)):
    args=[D,'--output',f'{O}/{f}.spv',f'{C}/{f}','--vertex-input','ATTR','0','--allow-arithmetic-relaxed-precision','--subgroup-size','32','32','--validate','--ssbo-uav','--ssbo-srv','--enable-shader-demote']
    if '.bindless.' in f or '.sm66.' in f or 'heap' in f: args.append('--bindless')
    for t,a in tags.items():
        if t in f: args+=a
    r=subprocess.run(args,capture_output=True,text=True,timeout=60)
    if r.returncode!=0 or not os.path.exists(f'{O}/{f}.spv'):
        msg=(r.stderr+r.stdout).strip().splitlines()
        fails.append((f, ' | '.join(l for l in msg if l)[:300]))
for f,m in fails: print(f'FAIL {f}: {m}')
print(f'{len(os.listdir(C))} shaders, {len(fails)} dxil-spirv failures')
