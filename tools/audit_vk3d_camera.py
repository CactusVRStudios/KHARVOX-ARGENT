"""Read-only reconstruction of Eternal VR 0.90 camera injection contracts.

Patterns and payload RVAs are recovered from Vk3DVisionVR.dll's initializer
(RVA 0x1c711 onward). Never executes the DLL or patches the game.
"""
import hashlib, json, pathlib, re, struct
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

ROOT = pathlib.Path(__file__).resolve().parents[1]

class PE:
    def __init__(self, path):
        self.path = pathlib.Path(path)
        self.data = self.path.read_bytes()
        p = struct.unpack_from('<I', self.data, 60)[0]
        if self.data[p:p+4] != b'PE\0\0': raise ValueError('Not PE')
        count = struct.unpack_from('<H', self.data, p+6)[0]
        optional = struct.unpack_from('<H', self.data, p+20)[0]
        self.base = struct.unpack_from('<Q', self.data, p+24+24)[0]
        self.sections = []
        for i in range(count):
            o = p+24+optional+i*40
            vs, rva, size, raw = struct.unpack_from('<IIII', self.data, o+8)
            flags = struct.unpack_from('<I', self.data, o+36)[0]
            self.sections.append((rva, size, raw, flags))
    def at(self, rva, size):
        for va, length, raw, _ in self.sections:
            if va <= rva and rva+size <= va+length:
                return self.data[raw+rva-va:raw+rva-va+size]
        raise ValueError('RVA outside file-backed section')
    def matches(self, pattern):
        rx = re.compile(b''.join(b'.' if x == '??' else re.escape(bytes.fromhex(x)) for x in pattern.split()), re.S)
        return [va+m.start() for va,n,raw,flags in self.sections if flags & 0x20000000
                for m in rx.finditer(self.data[raw:raw+n])]

def main():
    dll = PE(ROOT/'reference/eternal-vr-0.90/Profiles/Doom Eternal -VR/VR/Vk3DVisionVR.dll')
    game = PE(r'D:\Games\dampf\steamapps\common\DOOMEternal\DOOMEternalx64vk.exe')
    if hashlib.sha256(dll.data).hexdigest() != 'ff680e8e8d5a250406839253d213808ae2c0a572665c5151df3b79dd24e263db':
        raise ValueError('Reference DLL changed; template RVAs must be re-audited')
    specs = [
        ('camera_position_basis', 'f2 0f 10 02 f2 0f 11 81 24 01 00 00', 0x3f5c0, 206),
        ('hook_2', '89 41 ?? 48 8b 0e f3', 0x3f698, 29),
        ('hook_3', '88 87 ?? ?? ?? ?? 48 8b 83 ?? ?? ?? ?? 48 8b 5c', 0x3f6b8, 15),
        ('basis_copy', '0f 11 06 0f 11 4e 10 89 46 20 48 8b 07', 0x3f6d8, 56),
        ('three_float_store', 'f3 41 0f 11 06 f3 41 0f 11 56 04 f3 41 0f 11 4e 08', 0x3f720, 65),
    ]
    decoder = Cs(CS_ARCH_X86, CS_MODE_64)
    def disasm(data, address):
        return [f'{i.address:#x}: {i.mnemonic} {i.op_str}' for i in decoder.disasm(data,address)]
    rows=[]
    for name,pattern,rva,length in specs:
        matches=game.matches(pattern)
        rows.append(dict(name=name, pattern=pattern, matches=[hex(m) for m in matches],
                         gameInstructions={hex(m):disasm(game.at(m,128),game.base+m) for m in matches},
                         templateRva=hex(rva),templateInstructions=disasm(dll.at(rva,length),dll.base+rva) if length else [],
                         validatedForHook=False))
    report=dict(readOnly=True,gameSha256=hashlib.sha256(game.data).hexdigest(),
                referenceSha256=hashlib.sha256(dll.data).hexdigest(),hooks=rows,
                note='Unique signatures establish locations, not complete calling contracts. No injection performed. Template relocation placeholders are not runtime addresses.')
    (ROOT/'docs/vk3d-camera-contract.json').write_text(json.dumps(report,indent=2))
    print(json.dumps([dict(name=r['name'],matches=r['matches']) for r in rows],indent=2))

if __name__ == '__main__': main()
