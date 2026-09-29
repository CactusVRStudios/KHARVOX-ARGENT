import pathlib,sys,unittest
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
from match_vertex_profile import canonical

class ProfileMatchTests(unittest.TestCase):
    def test_token_boundaries(self):
        self.assertNotEqual(canonical('a + + b;'),canonical('a++b;'))
        self.assertNotEqual(canonical('float a;'),canonical('floata;'))
    def test_comments_and_version(self):
        self.assertEqual(canonical('#version 450\nfloat a = 1.0;'),canonical('#version 460\nfloat /*note*/ a=1.0;'))
        self.assertNotEqual(canonical('a=1.0;'),canonical('a=2.0;'))
    def test_only_known_injection(self):
        original='void main(){gl_Position=vec4(1.0);}'
        block='''#version 460
#extension GL_EXT_multiview : enable
#define VIEW gl_ViewIndex
struct Vk3DStereo { vec4 stereo; vec4 custom_params; };
layout(set=0,binding=10,std140) uniform Vk3DParams { Vk3DStereo vk3d_params[2]; };
void main(){gl_Position=vec4(1.0);
gl_Position.x += vk3d_params[VIEW].stereo.x * (gl_Position.w - vk3d_params[VIEW].stereo.y);
}'''
        self.assertEqual(canonical(original),canonical(block,True))
        self.assertIsNone(canonical(block.replace('gl_Position.x +=','gl_Position.y +='),True))
        self.assertNotEqual(canonical(original),canonical(block.replace('vec4(1.0)','vec4(2.0)'),True))

if __name__=='__main__':unittest.main()
