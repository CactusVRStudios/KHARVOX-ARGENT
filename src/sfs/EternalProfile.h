#pragma once
#include "NativeSfs.h"
namespace argent::sfs {
inline Configuration eternalProfile(){Configuration c;c.imageComputeStereo=true;c.eternalVolumes=true;
// Final world-color/distortion resolve. The storage buffer at set 1/binding 9
// is declared writable but only read, so the conservative automatic policy
// needs this exact opt-in. AMD differs only by max3 instead of nested max.
c.stereoComputeShaders={0x497d9dd0489477bfull,0x9c2423225ead23c9ull};
c.broadcastComputeShaders={0x46ec3b5a9f6d32c2ull,0x866fd44ba9bfea56ull};c.projectionShaders={
#include "EternalWorldVariants.inc"
#include "EternalAmdWorldVariants.inc"
// Vk3D's configured generic vertex injection applies at the final clip-Y flip.
// These two captured variants transform mesh positions by the common world
// camera and were submitted to the scene depth target (1114x626), not the
// shadow atlas. Their old unprojected depth disagreed with stereo color passes.
0xf12270d373c45f2cull,
0x28c61f3860d5aa54ull,
// Previously unmatched Vk3D particle/menu variants, now exactly audited.
0x4afcaaf23d513976ull,
0x9ad72f1e67c8d14bull,
// Additional generic world replacements, exact source matches in the VR audit.
// Captured quarter-resolution distortion geometry (frame 7200, 278x156).
// Its fragment depth-tests against scene depth, so its positions must use
// the same eye projection as the scene. This is not a fullscreen resolve.
0x4c6606c9a9561709ull,
0x30bccd4de2f192efull,
0xe08d89d28ca2bf84ull,
0xea80bd1a27a55cf1ull,
0x11dcdb094bb80448ull,
0xaa2fb259120cd785ull,
0x9a236017972a2cbaull,
0xc32b6204771c705bull,
0x6cafa7ce13a6102cull,
0x7d5db60627ee833full,
0xf879b496a0a50c99ull,
0x3fdf3bd7e597623full,
0x409f81620ab8d847ull,
0x3c6730e2ddaeb55ull,
0xace9e4cd9dc6d910ull,
0x82e452fc9d37eb31ull,
0xb4dd923b5853eebeull,
0x8a120f80b3321699ull,
0x370f8431b75df10bull,
0xd4218d1822e77ce7ull,
0xb079a91ed49a125eull,
0x85806186a1df6e3bull,
0x1f0de3a1f4246cceull
};
// Exact alpha-equivalent source associations with the OpenVRDirect VR profile,
// including all original descriptor bindings/constants. See source audit.
c.screenUiShaders={0xdc2d10822f8eda88ull,0xf37a280cc1dc1a83ull,0x0749c071d7d1fdf5ull,0x4f50b1f4caf20882ull,0x475b91f7adce5776ull,0xc8d657a2321cc9eeull,0x32fb81bae310c07dull};
// Center-grid consumers and Vk3D's conservative producers are enabled together
// in world mode. Menu/quad keeps the original culling math.
c.eternalLightGrids=true;c.eternalVk3d=true;return c;}
}
