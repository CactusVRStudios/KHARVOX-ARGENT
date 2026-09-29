"""Read-only live CVar audit for the hash-verified Eternal executable.

Registration constructor 0x375c70 stores a pointer to CVar data in the
static object. Data offsets: current string 0, int 8, float 12, name 40.
Unlike DOOM 2016 these are indirect, not fields in the static wrapper.
"""
import ctypes as c
from ctypes import wintypes as w
import argparse, hashlib, json, pathlib, struct

ROOT = pathlib.Path(__file__).resolve().parents[1]
CVARS = {
    'r_hdrBloom': 0x66dd0e0,
    'r_guiLodBias': 0x66f0d20,
    'g_setting_hands_bob': 0x45f5100,
    'pm_noBob': 0x463a940,
    'r_skipFlares': 0x6681f80,
    'view_skipShakes': 0x468cbe0,
    'view_mpViewKick': 0x468ed60,
    'r_lensFlaresRatio': 0x66deb00,
    'r_cineLensflaresEnabled': 0x66dff60,
    'hands_fovScale': 0x4671260,
    'in_joystick': 0x6b4ab80,
    'in_MarkJoystickInactiveOnMouseInput': 0x46c6150,
    'g_fov': 0x194a48 + 7 + 0x4461131,
    'r_antialiasing': 0x244c6d + 7 + 0x644106c,
    'r_SSR': 0x24cefd + 7 + 0x643ae1c,
    'r_TAASafeMode': 0x25231d + 7 + 0x648c3fc,
    'r_motionblur': 0x253d0d + 7 + 0x648ae6c,
    'r_skipLightGPUCulling': 0x6683c00,
    'r_windowHeight': 0x25d34d + 7 + 0x648b19c,
    'r_windowWidth': 0x25d43d + 7 + 0x648b02c,
}

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--pid', type=int)
    parser.add_argument('--exe', default=r'D:\Games\dampf\steamapps\common\DOOMEternal\DOOMEternalx64vk.exe')
    args = parser.parse_args()
    launch = dict(pid=args.pid, exe=args.exe) if args.pid else json.loads((ROOT/'logs/last-launch.json').read_text(encoding='utf-8-sig'))
    digest = hashlib.sha256(pathlib.Path(launch['exe']).read_bytes()).hexdigest()
    if digest != '69dc13e88d1c19133ead7950dc64ebcbd4a5a3f6bd6f9c336ebffe56df6a1c11':
        raise ValueError('Unsupported executable')
    k = c.WinDLL('kernel32', use_last_error=True)
    ps = c.WinDLL('psapi', use_last_error=True)
    k.OpenProcess.argtypes = [w.DWORD,w.BOOL,w.DWORD]; k.OpenProcess.restype=w.HANDLE
    k.CloseHandle.argtypes=[w.HANDLE]
    k.ReadProcessMemory.argtypes=[w.HANDLE,c.c_void_p,c.c_void_p,c.c_size_t,c.POINTER(c.c_size_t)]
    ps.EnumProcessModulesEx.argtypes=[w.HANDLE,c.POINTER(w.HMODULE),w.DWORD,c.POINTER(w.DWORD),w.DWORD]
    k.QueryFullProcessImageNameW.argtypes=[w.HANDLE,w.DWORD,w.LPWSTR,c.POINTER(w.DWORD)]
    h=k.OpenProcess(0x410,False,launch['pid'])
    if not h: raise c.WinError(c.get_last_error())
    try:
        path=c.create_unicode_buffer(32768); n=w.DWORD(len(path))
        if not k.QueryFullProcessImageNameW(h,0,path,c.byref(n)): raise c.WinError(c.get_last_error())
        if pathlib.Path(path.value).resolve()!=pathlib.Path(launch['exe']).resolve(): raise ValueError('Process identity mismatch')
        modules=(w.HMODULE*1024)(); needed=w.DWORD()
        if not ps.EnumProcessModulesEx(h,modules,c.sizeof(modules),c.byref(needed),3): raise c.WinError(c.get_last_error())
        base=modules[0]
        def read(address,size):
            buf=c.create_string_buffer(size); got=c.c_size_t()
            if not k.ReadProcessMemory(h,address,buf,size,c.byref(got)) or got.value!=size: raise c.WinError(c.get_last_error())
            return buf.raw
        def ptr(address): return struct.unpack('<Q',read(address,8))[0]
        def string(address):
            value=bytearray()
            for i in range(256):
                ch=read(address+i,1)
                if ch==b'\0': return value.decode('utf-8')
                value.extend(ch)
            raise ValueError('Unterminated string')
        rows=[]
        for expected,rva in CVARS.items():
            data=ptr(base+rva); actual=string(ptr(data+0x28))
            if actual.lower()!=expected.lower(): raise ValueError(f'Name mismatch: {expected} / {actual}')
            rows.append(dict(name=actual,wrapperRva=hex(rva),value=string(ptr(data)),integer=struct.unpack('<i',read(data+8,4))[0],floating=struct.unpack('<f',read(data+12,4))[0],limits=struct.unpack('<ff',read(data+0x44,8))))
        report=dict(pid=launch['pid'],readOnly=True,exeSha256=digest,cvars=rows)
        print(json.dumps(report,indent=2))
        import time
        (ROOT/f"logs/cvars-{launch['pid']}-{time.time_ns()}.json").write_text(json.dumps(report,indent=2))
    finally: k.CloseHandle(h)

if __name__=='__main__': main()
