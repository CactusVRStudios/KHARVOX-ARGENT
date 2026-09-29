import pathlib, sys, unittest
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
from audit_vk3d_vr_profile import identity, base_and_tail

class IdentityTests(unittest.TestCase):
    source='layout(set=0,binding=3) uniform U { vec4 _m0; } _12; void main() { vec4 _13=_12._m0; gl_Position=_13; gl_Position.y = -gl_Position.y; }'
    def test_anonymous_renaming_and_hints(self):
        other=self.source.replace('_12','_92').replace('_13','_93').replace('vec4 _93=', 'SPIRV_CROSS_BRANCH vec4 _93=')
        self.assertEqual(identity(self.source),identity(other))
    def test_binding_change_rejected(self):
        self.assertNotEqual(identity(self.source),identity(self.source.replace('binding=3','binding=4')))
    def test_scalar_inequality_spelling(self):
        self.assertEqual(identity('if (!(_12.x == 0.0)) {}'),identity('if (_92.x != 0.0) {}'))
        self.assertEqual(identity('if (!(_12 == (-1.0))) {}'),identity('if (_92 != (-1.0)) {}'))
        self.assertNotEqual(identity('if (!(_12 < 0.0)) {}'),identity('if (_92 >= 0.0) {}'))
        self.assertNotEqual(identity('if (!(_12 == 0.0)) {}'),identity('if (_92 != 1.0) {}'))
    def test_dependency_change_rejected(self):
        self.assertNotEqual(identity(self.source),identity(self.source.replace('gl_Position=_13','gl_Position=_12._m0')))
    def test_constants_preserved(self):
        self.assertNotEqual(identity(self.source+'const float k=.1;'),identity(self.source+'const float k=.2;'))
    def test_appended_injection_only(self):
        profile=self.source[:-1]+'gl_Position.x += vk3d_params[VIEW].stereo.x; }'
        key,tail=base_and_tail(profile)
        self.assertEqual(key,identity(self.source))
        self.assertIn('vk3d_params',tail)
        self.assertIsNone(base_and_tail(profile.replace('gl_Position=_13','gl_Position=_13+vk3d_params[VIEW].stereo'))[0])

if __name__=='__main__':unittest.main()
