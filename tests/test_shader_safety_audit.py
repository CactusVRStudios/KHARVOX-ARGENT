"""Check the audit's positive/negative controls, not the game's safety."""
import importlib.util
import pathlib
import struct
import unittest

spec=importlib.util.spec_from_file_location('audit',pathlib.Path(__file__).parents[1]/'tools/audit_shader_safety.py')
audit=importlib.util.module_from_spec(spec);spec.loader.exec_module(audit)


class AuditTests(unittest.TestCase):
    def module(self,index):
        words=[0x07230203,0x10000,0,20,0]
        def op(code,*args):words.extend([((len(args)+1)<<16)|code,*args])
        op(21,1,32,0);op(43,1,2,4);op(28,3,1,2);op(32,4,4,3)
        op(59,4,5,4);op(43,1,6,index);op(32,7,4,1);op(65,7,8,5,6)
        return struct.pack('<%dI'%len(words),*words)

    def test_last_and_one_past_end(self):
        self.assertEqual(audit.inspect_spirv(self.module(3))['constant_oob'],[])
        self.assertEqual(audit.inspect_spirv(self.module(4))['constant_oob'][0]['index'],4)
        self.assertTrue(audit.inspect_spirv(self.module(0xffffffff))['constant_oob'])

    def test_portal_successor(self):
        source='shared uint table[64]; int row = int(floor(coord)); int base = row * 4; int next = base + 4;'
        self.assertEqual(audit.radial_table_candidates(source)[0]['rows'],16)
        self.assertEqual(audit.radial_table_candidates(source.replace('base + 4','min(base + 4, 60)')),[])

    def test_invalid_instruction(self):
        with self.assertRaises(ValueError):audit.inspect_spirv(struct.pack('<6I',0x07230203,0,0,0,0,0))


if __name__=='__main__':unittest.main()
