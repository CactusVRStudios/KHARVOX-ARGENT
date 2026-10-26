# ARGENT third-party notices

ARGENT incorporates components adapted from KHARVOX and KHARVOX CIRCLE.
Source notices remain with their respective components. Release packages contain
runtime files and assets only; development documentation and audit manifests are
maintained separately. These notices do not relicense third-party components.

## KHARVOX components

The input, OpenXR, SFS compiler, hand renderer, FSR wrapper and integration bridges
include MIT-licensed KHARVOX code. See `docs/KHARVOX-LICENSE.txt` and retained
source headers. The original reference revision was
`250cfbe40ff384e10bbded2c12ba988892c49794`.

The historical native stereo renderer attribution is copyright (c) 2026 dbkni
under the MIT License. ARGENT owns its Vulkan/OpenXR presentation and game hooks.
RenderDoc API declarations retain copyright (c) 2015-2026 Baldur Karlsson (MIT).
Historical component notices are retained in `docs/REFERENCE-THIRD-PARTY-NOTICES.txt`.

## Graphics and asset dependencies

- OpenXR-SDK headers and loader: Khronos, Apache-2.0 option; see
  `third-party/openxr-sdk/LICENSE` (revision
  `57af7fc61f9f2d492580cb28aab6d0ea59d8d417`, SDK 1.1.62).
- Vulkan-Headers: Khronos, Apache-2.0 option; retained notices in
  `third-party/vulkan-headers` (revision
  `b51f6b865c18fc5b33990d12f75e8dfd672cede6`).
- MinHook: Tsuda Kageyu and contributors, BSD-style license;
  `third-party/minhook/LICENSE.txt`.
- SPIRV-Cross: KhronosGroup, tag `vulkan-sdk-1.4.304.0`, revision
  `ebe2aa0cd80f5eb5cd8a605da604cacf72205f3b`; see the submodule's license.
  glslang and SPIRV-Tools notices are in `third-party/sfs-compiler-licenses`.
  These compiler libraries are linked into the SFS renderer.
- FidelityFX FSR 1.0.2: AMD, MIT; `third-party/fsr1/LICENSE`.
  ARGENT supplies the Vulkan integration around the EASU/RCAS algorithms.
- cgltf 1.15: Johannes Kuhlmann, MIT; `third-party/cgltf/LICENSE`.
  Used to load bundled hand assets. Game-derived artwork and models retain
  their original ownership.
- pocketmod and intro assets retain their component notices and original ownership.

## Peripheral integrations

PSVR2 Toolkit CAPI headers and loader: Bnuuy Solutions, MIT;
`third-party/psvr2-toolkit/LICENSE`. The loader is from
`v1.0.0-experimental-1`; CAPI headers were pinned at
`339f2eb4dff01f05bae1a3ae5b8629a345507e2b`.

The bHaptics SDK2 runtime is proprietary and governed by bHaptics terms.
It is not included in this source repository. ARGENT's bridge does not grant
redistribution rights to that SDK, Player, credentials or device assets.
Approved runtime hashes for both integrations are checked by
`tools/verify_package_integrations.ps1`.

## DLSS-G 2X

The D3D12 NGX wrapper and generator are adapted through KHARVOX CIRCLE.
The wrapper's MIT copyright notice (2026 dariulone) is retained in
`third-party/framegen/ngx-wrapper-LICENSE.txt`.
The NVIDIA DLSS SDK and runtime retain NVIDIA's licensing.
The Community DLSS 0.4.0 runtime is supplied separately in the development
workspace, with its original notices. `tools/package_framegen.ps1` verifies
approved runtime hashes and selects the RTX 20/30, RTX 40 and RTX 50 payloads.
