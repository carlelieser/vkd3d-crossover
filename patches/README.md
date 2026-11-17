# Patch series

Each directory is a `git format-patch` series for one upstream project, applied with `git am` on top of the commit
pinned in [`../versions.lock`](../versions.lock). One fix per patch; every message has
**Problem / Root cause / Fix / Verified by / Off switch**. IDs (S1, M13, …) refer to [`../OVERVIEW.md`](../OVERVIEW.md) §3.

- `patches/<component>/` — **shipping**: what releases are built from.
- `patches/dev/<component>/` — **diagnostics and experiments**: applied after the shipping series, developer builds only.

Verified for every component: the shipping series alone applies and builds at each commit, and shipping + dev
reproduces the original working tree exactly.

Apply order between components doesn't matter except at build time: MoltenVK must be built against SPIRV-Cross with
`spirv-cross/` applied (MoltenVK passes the new SPIRV-Cross options), and vkd3d-proton is built with `dxil-spirv/`
applied to its `subprojects/dxil-spirv`. `tools/dev/apply-patches.sh` does all of this.

## spirv-cross (20)
| # | Patch | ID |
|---|---|---|
| 0001 | Emulate 64-bit buffer and threadgroup atomics | S2 |
| 0002 | Lock the low word in emulated 64-bit atomic max/min | S3 |
| 0003 | Snapshot the compare-exchange comparator (blown-out hair) | S1 |
| 0004 | Emulate bitwise subgroup scans | S5 |
| 0005 | Emulate 64-bit subgroup arithmetic (needs 0004) | S4 |
| 0006 | Emulate compute-shader derivatives | S5 |
| 0007 | Emulate implicit-LOD sampling in compute shaders | S5 |
| 0008 | Emulate OpImageQueryLod in compute shaders | S5 |
| 0009 | Emulate sparse feedback as always resident | S5 |
| 0010 | Pass the lane id to helper functions using emulated derivatives | S6 |
| 0011 | Keep the access-chain flag on copied pointers | S6 |
| 0012 | PhysicalStorageBuffer storage for BDA vector-component atomics | S6 |
| 0013 | Keep `__restrict` on aliased argument-buffer descriptors | S6 |
| 0014 | Never emit CullDistance as a stage-out member | (sweep fix, NOTES Finding 12) |
| 0015 | Strip the per-vertex array from mesh ClipDistance outputs | S6 |
| 0016 | Options to retype fragment inputs/outputs (interface mismatches) | S7 |
| 0017 | Options to unpack 10:10:10:2 integer vertex attributes | — |
| 0018 | Remap component-decorated tessellation varyings | S8 |
| 0019 | `robust_buffer_access` option | S9 |
| 0020 | Honour LocalSizeId for `gl_WorkGroupSize` (mesh GPU hang) | S10 |

## moltenvk (28 shipping + 10 dev)
| # | Patch | ID |
|---|---|---|
| 0001 | Advertise emulated 64-bit atomics | M1 |
| 0002 | Advertise VK_KHR_compute_shader_derivatives (linear) | M1 |
| 0003 | Report a fixed subgroup size of 32 on Apple7+ | M1 |
| 0004 | Store buffer descriptor sizes at `start + i` | M10 |
| 0005 | Bind a zero buffer for null vertex buffers | M16 |
| 0006 | Implement robustBufferAccess2 and nullDescriptor | M1 / S9 |
| 0007 | Implement VK_EXT_mesh_shader on Metal mesh pipelines | M2 |
| 0008 | Cap layers of no-attachment render passes | M3 |
| 0009 | Emulate pipeline-statistics query pools | M4 |
| 0010 | Allow arrays of multi-planar images | M4 |
| 0011 | Fail buffer views without a Metal texel format | M5 |
| 0012 | Clamp texel buffer views to 2^28 texels | M5 |
| 0013 | Round up sub-block compressed copies, skip empty ones | M5 |
| 0014 | Skip per-command-buffer useResource with residency sets | M6 |
| 0015 | Skip unbound buffers in encodeGPUAddressableBuffers | M6 |
| 0016 | Allow blending on RGB9E5 render targets on Apple GPUs | M7 |
| 0017 | Gate the private OpenGL mode behind MVK_METAL_OPENGL_MODE | §5 |
| 0018 | Emulate sample masks in the shader with the private API | §5 |
| 0019 | Apply logic ops independently of format blendability | M9 |
| 0020 | Implement VK_EXT_dynamic_rendering_unused_attachments | M8 |
| 0021 | Resume after command-buffer errors; host-signal timeline semaphores | M11 |
| 0022 | Never assume no NaN/Inf in the Metal math mode | M12 |
| 0023 | A2B10G10R10 UINT/SINT vertex attributes | — |
| 0024 | Count translated vertex bindings only for static strides | — |
| 0025 | Pass shader interface types to SPIRV-Cross; vertex format class remap | S7 |
| 0026 | Hoist indirect-count conversions in front of render passes | M13 |
| 0027 | Persist SPIR-V → MSL conversions on disk | M14 |
| 0028 | Defer Metal render-pipeline compilation | M15 |

dev: 0001 failure log · 0002 64-bit atomic usage log · 0003 frame stats · 0004 GPU profiler · 0005 MSL dump by
hash · 0006 pass + copy probes · 0007 MSL override · 0008 safe-math tagging · 0009 GPU debug buffer ·
0010 MTLBinaryArchive experiment (off by default).

Note: shipping-only builds don't log failed deferred pipelines or attachment variants (that logging is in dev 0001);
those draws are still skipped as before.

## vkd3d-proton (7 shipping + 13 dev)
| # | Patch | ID |
|---|---|---|
| 0001 | Temporary `VKD3D_RELAX_CAPS` flag (first: 0003 reports tier 2 through it) | §6 |
| 0002 | Hide non-blendable R9G9B9E5 render targets under RELAX_CAPS | review: likely obsolete since moltenvk 0016 |
| 0003 | Emulate reserved resources without sparse binding | V1 |
| 0004 | Validate emulated reserved resources like tier 2 | V4 |
| 0005 | Use update-after-bind UAV limits for feature level 11_1 (→ FL 12_0) | V2 |
| 0006 | Return device removal from Present | V3 |
| 0007 | Add test_logic_op_rendering | test |

dev: 0001 failure log · 0002 VKD3D_DIAGNOSTICS switch · 0003 thread sampler · 0004 PSO retry tracking ·
0005 PSO mismatch log (off by default; `VKD3D_DIAGNOSTICS=1`) · 0006 fence stalls · 0007 queue activity ·
0008 SRV source fields · 0009 RT ring + event recorder · 0010 backbuffer-draw logger · 0011 one-time GPU readback ·
0012 VKD3D_DISPATCH_BARRIER experiment · 0013 VKD3D_REPORT_NON_UMA experiment.

## dxil-spirv (1)
| # | Patch | ID |
|---|---|---|
| 0001 | LegacyF32ToF16: exact round-toward-zero | D1 |

## dxvk (2 shipping + 1 dev)
| # | Patch | ID |
|---|---|---|
| 0001 | Expose a Windows-like display mode list (882x550 fix) | X1 |
| 0002 | Temporary `DXVK_RELAX_CAPS` flag | X2 |

dev: 0001 DXGI call trace.

## Notes on intermediate states
The original work existed only as final code, so a few intermediate patch states were reconstructed: spirv-cross
0001 (64-bit atomics before the 0002 lock), and a few single lines in moltenvk and dxvk that a later patch completes.
All of them compile; only the final states were tested on the GPU and in games.
