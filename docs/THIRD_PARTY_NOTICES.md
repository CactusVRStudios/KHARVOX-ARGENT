# ARGENT third-party notices



This local development package includes components copied from the read-only KHARVOX reference at commit 250cfbe40ff384e10bbded2c12ba988892c49794. Original KHARVOX bridge code and tests retain the MIT license in KHARVOX-LICENSE.txt. Bridge protocol enums are preserved without any engine hook implementation.



- Khronos OpenXR-SDK headers and official loader: revision 57af7fc61f9f2d492580cb28aab6d0ea59d8d417, release 1.1.62, Apache-2.0 option. Loader SHA-256 5A2B18579A8647EF4DD79AAD807AF143AFEE45A1D58DEA2186EE22A96DD84516. License in OpenXR-APACHE-2.0.txt (packaged) or third-party/openxr-sdk/LICENSE (source).

- Khronos Vulkan-Headers: revision b51f6b865c18fc5b33990d12f75e8dfd672cede6, Apache-2.0 option. Included notices retained in headers and Vulkan-Headers-LICENSE.md.

- PSVR2 Toolkit: CAPI/common headers at 339f2eb4dff01f05bae1a3ae5b8629a345507e2b, official CAPI loader from v1.0.0-experimental-1. MIT, Bnuuy Solutions. SHA-256 E366143796FBB90EDCB5CFB61AFB05C7BAA3E27EBAABE209BD7DF16A3ADCED16. License in PSVR2-Toolkit-LICENSE.txt (packaged) or third-party/psvr2-toolkit/LICENSE.

- bHaptics SDK2: proprietary DLL copied from the user's existing LOCALAPPDATA/KHARVOX/bhaptics installation for this local package, SHA-256 909124BAED4467AA0C85B1A25BC84065FFC65030F3199C13EA7A18403434CE52. Governed by bHaptics terms; no rights to public redistribution are granted by this project. This task did not publish a release. No credentials or device assets copied.



The VK3DVision Eternal profile is an external analytical reference attributed in its configuration to Helifax. Its shader source, artwork and driver are not distributed in ARGENT.



Reference specifications used for the new Vulkan/OpenXR integration:

- https://registry.khronos.org/OpenXR/specs/1.0-khr/html/xrspec.html

- Vulkan loader/layer ABI definitions from the copied Khronos vk_layer.h.

## Additional development tools and SFS foundations

SPIRV-Cross was imported from https://github.com/KhronosGroup/SPIRV-Cross for local offline analysis: tag `vulkan-sdk-1.4.304.0`, commit `ebe2aa0cd80f5eb5cd8a605da604cacf72205f3b`. See `third-party/spirv-cross/LICENSE` for its license; the standalone tool is not shipped in the Quad runtime package.

`src/sfs/ShaderIdentity.h`, `PipelineIdentity.h`, `StereoResources.h`, the SFS foundation tests, and `tools/compile_sfs_profile.py` originate from the local KHARVOX reference (commit `250cfbe40ff384e10bbded2c12ba988892c49794`). At the initial integration stage, hashes were evaluated only as unverified mapping candidates, and resource rules were not yet connected to the Eternal render path. No AER render path was imported.

`src/sfs/StereoCompiler.h` adapts the general `StereoCompiler` class from `src/sfs/ShaderCompiler.cpp` in the same MIT-licensed reference. The DOOM-specific assumption that comparison samplers always read layer 0 was removed. Engine-name heuristics, lighting corrections, and reserved DOOM 2016 descriptor slots were not imported. `tools/sfs_transcode.cpp` provides ARGENT's separate offline entry point.

Additional MIT-licensed KHARVOX SFS, OpenXR, and input files and their tests were imported from the same commit. The local development manifest `kharvox-reuse-manifest.json` records the complete file list and SHA-256 hashes. The glslang C backend in `src/sfs/ShaderCompiler.cpp` was extracted from KHARVOX. Local compiler tools link against the existing glslang/SPIRV-Tools SDK at `ARGENT_GLSLANG_ROOT`; the compiler libraries are also statically linked into the ARGENT layer, as described below.

The ARGENT layer statically links the adapted NativeSfs core and glslang/SPIRV-Tools/SPIRV-Cross. The retained license texts are available in the source tree under `third-party/sfs-compiler-licenses`. The local glslang SDK reports version 16.5.0; its exact glslang and SPIRV-Tools build revisions are not documented.

The OpenXR stereo output in `src/StereoXr.inc` and `src/openxr/StereoProjection.h` also adapts the nativeFrame projection handoff from KHARVOX's `src/openxr/OpenXRBootstrap.cpp` (MIT, same reference commit).

- MinHook: Tsuda Kageyu and contributors, BSD-style license; copied from the read-only KHARVOX third-party source. Full license retained in third-party/minhook/LICENSE.txt and packaged as licenses/MinHook-LICENSE.txt.


## AMD FidelityFX Super Resolution 1.0.2

- Upstream: `GPUOpen-Effects/FidelityFX-FSR`
- Source revision / release: `a21ffb8f6c13233ba336352bdff293894c706575` / `v1.0.2`
- Included files: `third-party/fsr1/ffx_a.h`, `third-party/fsr1/ffx_fsr1.h`, and `third-party/fsr1/LICENSE`
- Repository-file SHA-256 (`ffx_a.h`): `F60E2722FCD13989523B9164D776AB382B3692791767F3BF8BB19967F763F3FB`
- Repository-file SHA-256 (`ffx_fsr1.h`): `93C3922362EA7FC99CBCC698CA30C98DE4F8C246D1FBB0B09E015DDEF38CE3A5`
- Generated EASU SPIR-V SHA-256: `3A47843DB68492BC48A066BB272CD803BE3BE8599ADAE500389C16E414355911`
- Generated RCAS SPIR-V SHA-256: `2E30697AC4F767D38FA6186FB903D7CB4A14B5370E7D163389492AAAB97FD666`
- License: MIT; Copyright (c) 2021 Advanced Micro Devices, Inc.

The upstream headers are retained with their notices; only line endings are normalized by the project workspace. `src/fsr/Fsr1Pass.comp` supplies KHARVOX's Vulkan resources, bounds checks, and sRGB handoff around the official EASU and RCAS functions. The upstream MIT text is retained as `third-party/fsr1/LICENSE`.

## NVIDIA Optical Flow API headers

Included files:

- `archive/vendor/nvidia-optical-flow/include/nvOpticalFlowCommon.h`
- `archive/vendor/nvidia-optical-flow/include/nvOpticalFlowVulkan.h`

Copyright (c) 2018-2023 and 2022-2023 NVIDIA Corporation, respectively. Both headers contain and retain the following permission notice:

> Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
>
> The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
>
> THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

These historical headers are archived with their notices. The current runtime does not use Optical Flow or load nvofapi64.dll.

## cgltf 1.15

- Upstream: `jkuhlmann/cgltf`
- Source revision: `85cd62382dfea638278962690cf515023f33ed00`
- Included files: `third-party/cgltf/cgltf.h` and `third-party/cgltf/LICENSE`
- `cgltf.h` SHA-256: `EFB169DEE911696B5D35FC8E3F7EA0C56D679DEBC529EBA9CA6AA6443BA9D5E9`
- License: MIT; Copyright (c) 2018-2021 Johannes Kuhlmann

KHARVOX uses cgltf only to decode the trusted static GLB hand assets shipped
with the project. It can also bake a selected rigged fallback scene into a
static vertex pose at load time. The loader does not accept game-downloaded or
network content.

## Optional bHaptics SDK2 runtime

KHARVOX contains an original interoperability bridge that can dynamically load the proprietary `bhaptics_library.dll` from the bridge executable's own directory. The DLL, bHaptics Player, SDK credentials, device data, and bHaptics assets are not included in this source repository.

The bridge ABI follows the official `bhaptics/tact-csharp2` wrapper and `bhaptics/mfc-sample` SDK2 header. Distribution of `bhaptics_library.dll` is governed by bHaptics' terms and requires separate authorization. KHARVOX packages must omit that DLL until the project owner has documented express distribution permission.

## PSVR2 Toolkit v1 CAPI loader and external source link

- Upstream: `BnuuySolutions/PSVR2Toolkit`
- CAPI contract pinned from `main`: `339f2eb4dff01f05bae1a3ae5b8629a345507e2b`
- Official binary release: `v1.0.0-experimental-1`
- Release commit: `9e24e6ef475660481e8b46366aaa3cb24d0b4fde`
- Official asset: `CAPIApps-Windows-v1.0.0-experimental-1.zip`
- Asset SHA-256: `9D9058C0A2F186561CED05D9A40284C0451329FCA6DF999AA6A1E76A42EEDA74`
- Included official file: `third-party/psvr2-toolkit/psvr2_toolkit_capi_loader.dll`
- Loader SHA-256: `E366143796FBB90EDCB5CFB61AFB05C7BAA3E27EBAABE209BD7DF16A3ADCED16`
- Upstream license: MIT; Copyright (c) 2026 Bnuuy Solutions

The upstream MIT text is retained unchanged as `third-party/psvr2-toolkit/LICENSE`. The official loader source is unchanged between the published release commit and the pinned `main` revision. KHARVOX includes no `psvr2_toolkit_capi.dll`, Toolkit driver, `libcrossipc.dll`, legacy IPC server, SDL runtime, CAPI test executable, or copied trigger ABI header.

The PSVR2 bridge is compiled against the official Toolkit CAPI headers through the `tools/PSVR2Toolkit` Git submodule. KHARVOX stores only the external repository URL and pinned commit; the linked Toolkit source remains governed by its upstream notices and terms.

## Experimental native stereo adapter (r136)

The native stereo render implementation derives from the supplied doomvr stereo
renderer, copyright (c) 2026 dbkni, under the MIT License. Its notice is preserved
in `src/native/upstream/LICENSE`. Ordered hook fragments, matrix mathematics,
resource tracking and diagnostics are retained to preserve their dependencies.
The KHARVOX adapter replaces presentation/session ownership and the final-pass
entry assumption; it does not load doomvr.dll or its injector/OpenXR context.
The included RenderDoc API header retains its own MIT copyright notice.

MinHook v1.3.4, revision `c3fcafdc10146beb5919319d0683e44e3c30d537`, is statically
linked for the experimental engine hooks. Its license and source notices are
preserved under `third-party/minhook`. No MinHook DLL is needed at runtime.

The imported RenderDoc application API header retains the MIT license and
copyright (c) 2015-2026 Baldur Karlsson. Its full notice is included in
`src/native/upstream/RENDERDOC-LICENSE.txt` and in the runtime package.


## Optional SFS shader compiler (0.96 prototype)

The optional `KHARVOX_BUILD_SFS_COMPILER` build statically links unmodified
SPIRV-Cross core/GLSL libraries (KhronosGroup/SPIRV-Cross,
`vulkan-sdk-1.4.304.0`, commit `ebe2aa0cd80f5eb5cd8a605da604cacf72205f3b`),
glslang and SPIRV-Tools. KHARVOX's typed transformation is in
`src/sfs/ShaderCompiler.cpp`; it is not a modification of those libraries.
Full license collections are retained in `third-party/sfs-compiler-licenses`
and must accompany binary builds with this option enabled.

The local experimental build uses glslang headers reporting 16.5.0 from the
user's installed `D:/DoomVR/build/shader-tools/glslang-main` SDK. Its exact
upstream build revision and the bundled SPIRV-Tools revision are not recorded
in that installation; do not represent this as a reproducibly pinned toolchain.
License texts for those libraries were obtained from their official upstream
repositories on 2026-09-17. A distributable release should rebuild them from
pinned sources and retain the matching notices.

The local DOOM shader capture and the user-provided shader replacement profile
are test inputs, not part of this source distribution. The native SFS layer
does not load the Vk3DVision provider DLL.


ARGENT adapts the KHARVOX Fsr1Upscaler wrapper and hardware test for downstream Vulkan dispatch and array-layer eye sources. Shaders are copied unchanged. License packaged as licenses/FSR1-MIT.txt. Physical punch thresholds match KHARVOX OpenXRBootstrap (2.8 m/s, 45% rearm, 350 ms cooldown, 200 ms pulse).
## ARGENT hand renderer

The hand renderer, HandPbr shaders, four DOOM hand GLB assets, visibility and
calibration policies were imported from the user's local KHARVOX project.
ARGENT adapts Vulkan dispatch, stereo array layers, configuration and Eternal
profile names. The source KHARVOX project is unchanged.
Initial left/right calibration comes from
`out/v1.0/calibration-input/hand_models_calibration_saved.cfg`, saved 2026-09-19,
SHA-256 `c0a46a388480c6fc6ba79ecf1180a5e58176569550840bf8d5bf51fe604f314c`.
The bundled cgltf parser retains its MIT notice in `third-party/cgltf/LICENSE`
and `licenses/cgltf-MIT.txt` in runtime packages. Imported game-derived assets
retain their original ownership; no new ownership or licensing is asserted.
