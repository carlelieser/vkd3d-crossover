# vkd3d-proton on MoltenVK (Apple M1 Max) — baseline notes

Goal: open-source D3D12 path (vkd3d-proton → MoltenVK → Metal) on M1 that can expose
SM 6.6 + mesh shaders + int64 atomics, loaded into CrossOver.

## Setup
- `src/` — shallow clones: MoltenVK (1.4.3, Oct 13), vkd3d-proton (5b1089c, Nov 13), dxil-spirv.
- `build/CrossOverDev.app` — APFS clone of CrossOver Preview with our MoltenVK swapped in
  (`lib/{x86_64,aarch64}/libMoltenVK.dylib`, ad-hoc signed). Real CrossOver untouched.
  Needed because win32u.so dlopens MoltenVK via an rpath inside the app; DYLD_* is stripped (hardened runtime).
- Test bottle: `vkd3d-test` (win10_64, win64). Test binaries in its `C:\vkd3d`.
- `build/run_test.sh <exe> [timeout]` — copies d3d12/d3d12core and runs with logs.
  Env: `VKD3D_BUILD=build.dbg` for the trace build, `VKD3D_TEST_FILTER=<substr>` for d3d12.exe.
- `build/rebuild_mvk.sh` — rebuilds SPIRV-Cross + MoltenVK without fetchDependencies' force-checkout
  (which would wipe patches), keeps Xcode intermediates in the scratchpad (build.db hits disk I/O errors here).

## Findings
1. CrossOver ships MoltenVK 1.2.10 (Vulkan 1.2); vkd3d-proton needs Vulkan 1.3 → use MoltenVK 1.4.3.
2. Hard device requirements MoltenVK lacks (bypassed with `VKD3D_RELAX_CAPS=1`, our dev flag):
   - transform feedback (no Metal stream-out)
   - robustness2 robustBufferAccess2/robustImageAccess2 + nullDescriptor
   - single-texel texel-buffer offset alignment (Metal linear texture alignment)
3. SPIRV-Cross bug (fixed): atomics on a vector component through a `buffer_reference` pointer
   emitted `&vec[0]` (invalid MSL). `get_expression_effective_storage_class` returned the push-constant
   block's storage class instead of PhysicalStorageBuffer. Patch in `spirv_glsl.cpp` + `spirv_msl.cpp`.
4. Result: offscreen D3D12 works. Sampled d3d12.exe groups all pass (device, resources, clears,
   draws, compute, typed UAV, textures, wave ops, CBV heaps).
5. Swapchain: FIXED. Crash was Wine's builtin dxgi.dll paired with vkd3d-proton (ABI mismatch; it
   faulted in libobjc on a garbage object pointer before reaching vkd3d). vkd3d-proton must be paired
   with DXVK's dxgi.dll (swapchains go through IDXGIVkSwapChainFactory). CrossOver's bundled DXVK has
   no dxgi.dll, so we build it from `src/dxvk` (3.1.1, 71bf4ba). DXVK rejects MoltenVK for missing
   geometryShader (a D3D11 need), so added dev flag `DXVK_RELAX_CAPS=1` in `dxvk_device_info.cpp`.
   Result: triangle.exe presents ~60 fps (935 presents / ~15 s), no present errors; sampled tests still pass.
   Runner now copies dxgi.dll and sets `WINEDLLOVERRIDES=d3d12,d3d12core,dxgi=n`.
   Visual check not done by me (no screen-recording permission).
7. Shader model ladder — now SM 6.6 natively (no VKD3D_SHADER_MODEL override). FL still 11_0.
   - SM 6.2: `VKD3D_RELAX_CAPS` exposes it without FP32 denorm controls (Metal has none).
     test_shader_sm62_denorm: 35/36 (one denorm mode not honored — expected).
   - SM 6.5 wave intrinsics: SPIRV-Cross emulates bitwise/logical inclusive+exclusive subgroup scans
     (active-lane-rank broadcast loop; correct for partial waves / WaveMultiPrefix). 582/582 pass.
   - SM 6.6 subgroup size: MoltenVK reports min=max=32 on Apple7+ (real SIMD width).
   - SM 6.6 compute derivatives: MoltenVK advertises VK_KHR_compute_shader_derivatives (linear);
     SPIRV-Cross emulates ddx/ddy/fwidth (fine+coarse) with quad_shuffle_xor / quad_broadcast,
     implicit-LOD sampling via gradient2d/3d/cube from those derivatives (bias folded as exp2),
     and OpImageQueryLod computed manually. 823/823 pass.
   - SM 6.6 int64 atomics: MoltenVK advertises VK_KHR_shader_atomic_int64 (buffer + shared);
     SPIRV-Cross emulates with 32-bit atomics on the halves (spvAtomic64*).
     Exact: add/sub/inc/dec, and/or/xor, load. Best-effort under contention: min/max, exchange,
     compare-exchange, store. test_shader_sm66_64bit_atomics: 79/85; all 6 failures = torn concurrent
     exchange. Typed (R64 image) atomics not supported (skipped). Fix needs a lock table (phase 2).
   - SPIRV-Cross: aliased descriptor reinterpret_cast now keeps __restrict (else "casts away qualifiers").
   - The BDA storage-class fix (finding 3) is narrowed to the vector->scalar access-chain path only.
8. Pre-existing (NOT a regression, verified against a minimal-patch baseline): test_atomic_instructions
   DXBC pixel-shader atomics on root UAVs — 64/316 fail on MoltenVK. DXIL path passes.
9. Broad regression sample (18 groups, ~23k checks): all pass except item 8.
10. Patches saved in `patches/` (spirv-cross, moltenvk, vkd3d-proton, dxvk) — NOTE: predates mesh work; regenerate.
11. Mesh shaders (VK_EXT_mesh_shader in MoltenVK):
    - Advertised on Metal3-family GPUs (macOS 13+): taskShader/meshShader; limits per Metal mesh pipeline.
    - New Metal stages Object/Mesh (binders setObject*/setMesh*); Vulkan task -> TessCtl slot, mesh -> TessEval slot
      (stage-flag mapping includes TASK/MESH bits), bindings aliased to TaskEXT/MeshEXT execution models.
    - Pipelines via MTLMeshRenderPipelineDescriptor; fragment stage built on a scratch render descriptor and copied over;
      empty fragment function supplied when Vulkan has none (Metal requires one).
    - vkCmdDrawMeshTasks{,Indirect,IndirectCount}EXT -> drawMeshThreadgroups{,WithIndirectBuffer}; count variant reuses the
      indirect-count conversion kernel (reads 4 bytes past the last 12-byte command).
    - Fixed null pVertexInputState deref for mesh pipelines.
    - !! GPU HANG INCIDENT: SPIRV-Cross emitted gl_WorkGroupSize = uint3(0,0,0) for mesh shaders using LocalSizeId
      (SPIR-V 1.6 / glslang), so mesh output loops stepped by 0 forever -> WindowServer watchdog -> user logged out.
      Fixed: read LocalSizeId constants; loop strides clamped to >= 1. Safety protocol now: create-only MSL review first
      (`MESHTEST_CREATE_ONLY=1 MVK_CONFIG_DEBUG=1`), Metal API+shader validation on, ask the user before each GPU run.
    - Native harness `build/meshtest` (links MoltenVK directly, no Wine): mesh-only + per-primitive -> 255 0 255 255 (OK);
      task + payload + mesh + frag -> 51 102 153 255 (OK). Both with Metal validation clean.
    - vkd3d-proton reports MeshShaderTier 1.
    - vkd3d mesh tests (Wine):
      * test_mesh_shader_create_pipeline: 11 pass, 1 upstream todo.
      * test_mesh_shader_rendering: "simple quad" and "interface matching" subtests PASS. Then:
        - "shared memory / dynamic primitive counts / multiple dispatches" (ms_culling + ps_culling, no render
          targets, root SRV/UAV via BDA, DispatchMesh(4,1,1)) -> MTLCommandBuffer Internal Error 0x10d, device lost.
          Reproduces without any validation layer (real GPU fault, recovered cleanly). Not yet diagnosed.
        - ms_clip_distance fails to compile: SPIRV-Cross emits `float gl_ClipDistance [[clip_distance]] [4][1]`
          in spvPerVertex (per-vertex array not peeled) -> invalid mesh vertex type. SPIRV-Cross bug, not fixed yet.
        - Metal GPU shader validation (MTL_SHADER_VALIDATION=1) itself causes Internal Error with these mesh
          pipelines; API validation (MTL_DEBUG_LAYER=1) aborts on a 16MB shared buffer freed while in use
          (only in this test; plain draw tests are clean under API validation). Lifetime issue not yet traced.
      * amplification / ExecuteIndirect mesh tests: not run yet.
    - GPU fault investigation (culling subtest):
      * Native repro `build/meshtest/culltest` with the EXACT vkd3d SPIR-V (ms_culling 43ea05d8, ps_culling ec9c7946),
        BDA buffers via push constants, 4 groups, no RT, with/without descriptor sets (push constants at buffer(16)):
        PASSES (uav = deadbeef 12345678 00000000 ffffffff). So shader translation + mesh draw path are correct.
      * Not state carry-over: test patched with MESH_ONLY_CULL (jumps to culling subtest) still faults.
      * With MVK_CONFIG_SYNCHRONOUS_QUEUE_SUBMITS=1 + vkd3d trace: the faulting submit is a plain CopyBufferRegion
        (+ an initial image layout transition), the first submit AFTER creating the culling PSO.
      * MESH_CULL_NO_PSO (skip PSO creation, same uploads): no device loss. => Creating vkd3d's culling mesh PSO
        poisons the next command buffer. Native harness creating a pipeline from the same SPIR-V does not.
        Next: diff MoltenVK pipeline creation inputs between vkd3d and the harness (dynamic states, pipeline cache,
        rendering formats, rasterization/multisample state, descriptor layouts), then replicate in the harness.
      * CORRECTION: the "PSO creation poisons" conclusion was wrong — MESH_ONLY_CULL skipped earlier subtests and left
        resources in wrong states. Full run with sync submits: the faulting submit is the one with DispatchMesh.
      * ROOT CAUSE (FIXED): vkd3d's no-attachment pass is 16384x16384 x 2048 layers (render area 16384^2). Mesh draws in
        a Metal render pass with no attachments and huge width*height*layers fault on AGX (Internal Error 0x10d).
        Reproduced natively (culltest CULL_EXT/CULL_LAYERS). Not a clean formula; everything <= 2^34 pixels passed.
        Fix in MVKCommandBuffer.mm: for no-attachment passes cap renderTargetArrayLength so w*h*layers <= 2^34
        (16384^2 -> 64 layers). Extent left alone (would clip viewports / drop UAV writes).
        Result: test_mesh_shader_rendering culling + cull_primitive subtests now PASS.
      * Also fixed along the way: nil MTLBuffer guard in encodeGPUAddressableBuffers.
      * SPIRV-Cross: standalone (non-block) arrayed ClipDistance/CullDistance mesh outputs (dxil-spirv style) declared
        the [[clip_distance]] member as float[verts][N] and copied whole arrays -> fixed (strip per-vertex array; copy
        per element). Block-member form (glslang) verified unchanged.
      * SPIRV-Cross: emulated derivatives used inside helper functions referenced gl_SubgroupInvocationID without it
        being passed in -> derivative/implicit-LOD/QueryLod ops now add the lane id to the function's args.
      * RESULT: test_mesh_shader_rendering 260/260 pass. test_mesh_shader_create_pipeline 11 pass (+1 upstream todo).
      * test_amplification_shader: 57/57 pass. test_mesh_shader_execute_indirect: 136/136 pass.
      * *_execute_indirect_state tests fail (4 + 2 checks): ExecuteIndirect with per-command root constants/descriptors
        needs VK_EXT_device_generated_commands; without it vkd3d FIXMEs and drops the state (not mesh-specific).
        MoltenVK has no DGC — large separate project.
      * Dev logging removed; patches regenerated (patches/*.patch, no dev code). Regression sweep all green
        (except known 64-bit atomic torn-exchange 6/85).
12. E_OUTOFMEMORY hunt (RE Engine "Fatal D3D error (6, E_OUTOFMEMORY)"):
    - vkd3d-proton maps a failed vkCreate*Pipelines to E_OUTOFMEMORY, and MoltenVK reports device loss as
      VK_ERROR_OUT_OF_DEVICE_MEMORY. Allocation probe (build/probe/d3d12probe.c): 1M-descriptor heaps, 2048 sampler
      heaps, 4 GiB heaps/buffers, 16K textures all OK.
    - Full per-test sweep of all 597 vkd3d-proton tests (build/sweep.sh -> sweep_results.tsv; re-sweep sweep2_results.tsv,
      build/sweep_diff.py): fixed every pipeline-creation / device-loss class found that applies to real engines:
      * MoltenVK: blanket useResource of all GPU-addressable buffers per command buffer (when a residency set exists)
        made in-flight work reference app-freed memory -> GPU "Invalid Resource" device loss. Skipped with residency
        sets (all such buffers are already resident). test_stress_suballocation(+_multithread) 3372/48478 -> 0 failures.
      * Fragment output type != render target type (float4 -> R32_UINT): Metal rejected pipeline. SPIRV-Cross option
        fragment_output_basetypes + bitcast; MoltenVK fills from color attachment formats.
      * Vertex attribute format class != shader input type (UInt4 -> float4): MoltenVK remaps MTLVertexFormat class.
      * VS->PS interface: type mismatch (bitcast via fragment_input_basetypes), width mismatch (exact-width member),
        PS reading a location the VS doesn't write (zero local). Builtins with synthesized locations ignored;
        component-packed locations not retyped. test_shader_io_mismatch 3 -> 0 failures.
      * Standalone CullDistance output wrote out.gl_CullDistance (no such member) -> local array (Metal has no
        cull distance; primitives aren't culled). Compile errors gone; 59 -> 12 failures.
    - Re-sweep: 0 regressions, 6 improvements.
    - Still open: tessellation shaders with Component decorations (SPIRV-Cross limitation), BaryCoord+NoPersp
      together, pipeline-statistics queries (E_FAIL), R32G32B32 typed buffers, geometry shaders (not in Metal),
      ExecuteIndirect state (no DGC), exhaustive-resolve command-buffer OOM (64K resolves in one submission).
    - ~/Applications/CrossOverDev.app updated with this MoltenVK; Steam-vkd3d bottle DLLs current.
13. Next game error "Fatal D3D error (1, E_FAIL)". Known E_FAIL sources on this stack, both fixed in MoltenVK:
    - Pipeline-statistics query pools (D3D12 PIPELINE_STATISTICS query heaps): MoltenVK refused them. New
      MVKEmulatedPipelineStatisticsQueryPool: one value per enabled statistic, all zero, normal availability; base
      host readback made multi-element. test_create_query_heap now 8/8 (was crash); probe heap OK.
      test_query_pipeline_statistics 6 fails (expects real counts — expected).
    - Chroma-subsampled (NV12/P010...) image arrays: MoltenVK refused arrayLayers > 1 although plane textures are
      created with the image's array type. Restriction removed. test_planar_video_formats 1774/1774 (was 1 fail).
    - Regression subset clean. ~/Applications MoltenVK updated, patches regenerated.
      * Dev test edits (MESH_ONLY_CULL / MESH_CULL_NO_PSO) were stashed out of vkd3d-proton (`git stash` in src/vkd3d-proton).
      * DEV-ONLY test edits in vkd3d-proton tests/d3d12_mesh_shader.c (MESH_ONLY_CULL, MESH_CULL_NO_PSO) — revert before upstreaming.

## Original ladder blockers (before fixes)
6. Reported caps: SM 6.0, FL 11_0. Ladder blockers:
   - SM 6.2: MoltenVK reports no denorm float controls (FTZ/preserve = false, independence NONE).
   - SM 6.6: needs computeDerivativeGroupLinear (no KHR_compute_shader_derivatives),
     shaderBufferInt64Atomics (hard-coded false; M1 has no HW 64-bit atomics), required subgroup size.
   - Mesh tier 1: no VK_EXT_mesh_shader in MoltenVK (Metal 3 mesh shaders exist on M1).
   - SPIRV-Cross MSL throws on any 64-bit atomic ("MSL currently does not support 64-bit atomics").

## 14. Startup-path probes (probe2–4) and three Metal aborts fixed
- probe2 (queues, query heaps, swapchains, 95 formats): all OK except HDR10 color space (SetColorSpace1 G2084/P2020 -> E_INVALIDARG; surface lacks HDR10).
- probe3: max feature level is 11_1 (TiledResourcesTier 0, no sparse in MoltenVK); D3D12CreateDevice(FL12_0) -> E_INVALIDARG. VKD3D_FEATURE_LEVEL=12_x overrides exist upstream. Shared fence handles -> DXGI_ERROR_INVALID_CALL.
- probe4 (DXGI fullscreen/mode switch/HDR metadata/latency/legacy CreateSwapChain): all OK.
- Fixed (MoltenVK) process aborts:
  1. Typed buffer view with no Metal format (R32G32B32_*) passed MTLPixelFormatInvalid to minimumLinearTextureAlignmentForPixelFormat -> now VK_ERROR_FORMAT_NOT_SUPPORTED.
  2. Texel buffer views > 2^28 texels -> Metal descriptor assertion; width clamped, maxTexelBufferElements = 2^28 on Apple7+.
  3. Compressed->uncompressed image copy smaller than one block produced a 0x0 buffer->texture copy (Metal abort); round-up division + zero-size skip.
- Sweep3: 597 tests, 0 regressions (suballocate_va_alignment flake rechecked), 7 improvements.
- E_FAIL in vkd3d comes only from dxil-spirv translation failure (VKD3D_ERROR/INVALID_SHADER) or an unhandled VkResult outside pipeline creation; pipeline compile failures surface as E_OUTOFMEMORY.

## 15. E_FAIL hunt, round 2 (all clean)
- Shader corpus: 785 dxil-spirv test shaders (SM6.0-6.9, raster/compute/mesh/tess) compiled with DXC 1.9.2609 (tools/dxc, under Wine) and run through dxil-spirv: 0 real failures (3 intentionally invalid). In vkd3d, translation failure -> E_INVALIDARG; only DXIL container parse failure -> E_FAIL.
- probe5: 1858 texture configs (1D/1D-array/2D/cube/arrays/3D, mips, MSAA, MSAA arrays, depth arrays/cubes, BC incl. 3D, RT/UAV/simultaneous), committed + placed: 0 failures.
- probe6: swapchain recreation on same window, concurrent swapchains, resize/minimize/hide/borderless, hidden/1x1/minimized/child/destroyed windows, 4K FP16 buffers: 0 failures.
- vkd3d Present is asynchronous and returns S_OK/OCCLUDED only, so present-thread failures cannot surface as E_FAIL (unlike vkd3d-proton issue #913, an older synchronous path).

## 16. PRAGMATA SKETCHBOOK demo (free, legit Steam install) as the RE Engine test target
- Per-app overrides for PRAGMATA_SKETCHBOOK.exe (d3d12/d3d12core/dxgi native). build/run_demo.sh launches it directly into the running Steam (SteamAppId=4003800) with logs in build/demo_runs/.
- Found + fixed (SPIRV-Cross): OpCopyObject of a pointer to an access chain lost the access_chain flag, so stores through the copy emitted "*arr[i] = x" -> Metal compile error -> failed compute PSOs. dxil-spirv emits this pattern for groupshared stores. After the fix: 0 Metal compile failures, ~2x more shaders reach compilation.
- Current state: game finishes its PSO burst, window stays black, then a native crash ~30-100 s in; Capcom's CrashHandler launches CrashReport.exe (WPF, crashes under Wine: the 0xe0434352). CrashReport gets only an in-process pointer, no dump.
- Not causes: Streamline startup dumps (null NVAPI call + std::system_error EBUSY from Wine's builtin msvcp140; first-chance, game continues); threading/async-compute config toggles; ID3D10/11 QIs.
- Diagnostics blocked: any CX_DEBUGMSG makes runs die at 16 s (fault inside vkGetPhysicalDeviceProperties2 only under tracing; MoltenVK struct writes verified clean with a guard/sentinel test, build/meshtest/chaintest.c); the game exits under a debugger (anti-tamper; not bypassed).

## 17. "Fatal D3D error (1, E_FAIL)" root cause found and fixed (reproduced on the PRAGMATA demo)
- Method: temporary instrumentation of every E_FAIL return in vkd3d (now removed). Culprit: vkCreateComputePipelines ->
  VK_ERROR_INITIALIZATION_FAILED (-3) -> hresult_from_vk_result default -> E_FAIL -> CreateComputePipelineState fails ->
  RE Engine "Fatal D3D error (1, E_FAIL)". (Compute PSO failures map to E_FAIL, not E_OUTOFMEMORY.)
- Cause: 64-bit wave ops (WaveActiveSum/WavePrefixSum on uint64): Metal simd_sum/simd_prefix_* reject 64-bit types.
  Fix (SPIRV-Cross, SPVFuncImplSubgroup64): exact emulation from 32-bit SIMD ops (add via lo16/hi16/hi32 partial sums;
  and/or/xor per half incl. scans via the existing scan helpers; u/s min/max reduce via high-half-then-low-half).
- Further pipeline failures fixed after that:
  - Tessellation with Component-decorated varyings (SPIRV-Cross): buffer-linked tess interfaces (VS-for-tess outputs,
    TCS in/out, TES in) remapped to location' = location + 256*component in decorations and adjacent-stage tables;
    TES outputs use user() names. Tess tests: 4 improved, 0 regressions (a 4L+C variant broke indexed IO ranges).
  - VK_FORMAT_A2B10G10R10_UINT/SINT_PACK32 vertex attributes (MoltenVK + SPIRV-Cross): fetched as MTLVertexFormatUInt,
    unpacked in the vertex shader (vertex_input_1010102_{uint,sint}_mask options).
  - "Vertex shader requires per-vertex output buffer, but there is no free slot" (MoltenVK): translated-vertex-binding
    reservation counted attributes even with dynamic/zero strides, where no translation happens.
- MoltenVK install note: replace libMoltenVK.dylib atomically (copy to .new then mv) — cp over a mapped signed dylib makes
  new processes fail to load it ("Failed to load libMoltenVK.dylib").
- Demo status: 0 pipeline failures, 0 MoltenVK errors; still a deterministic null read (movq 0x18(%r10)) in a game worker
  thread ~30 s in during loading, with no D3D12/DXGI error logged. Game code is packed (Enigma), not analyzed.
  build/run_demo_bt.sh captures Wine's backtrace (temporarily disables CrashReport.exe and redirects AeDebug; restores both).
- Any CX_DEBUGMSG value makes winevulkan report "Exception 0xc0000005 in Unix call" in vkGetPhysicalDeviceProperties2
  (debug-mode-only; not reproducible without it), so Wine tracing is unusable here.

## 18. Demo gets past loading: emulated tiled resources (the null-pointer crash)
- Crash thread = render thread (vkd3d trace + Wine backtrace thread IDs); first frame, no D3D errors.
- Ruled out: NVAPI (CrossOver's nvapi64 disabled via AppDefaults: same crash).
- Cause: MoltenVK has no sparse binding -> TiledResourcesTier 0 -> max FL 11_1 -> RE Engine's FL 11 fallback path
  dereferences a resource it never created. With VKD3D_FEATURE_LEVEL=12_1 the game immediately creates a 32 MiB
  reserved UAV buffer (tight alignment), confirming it uses tiled resources on the 12_x path.
- Fix (vkd3d-proton): no sparseBinding -> every reserved resource uses the existing committed fallback
  (d3d12_resource_create_reserved_fallback); UpdateTileMappings already ignores fallbacks, CopyTileMappings now too;
  TiledResourcesTier 2 reported under VKD3D_RELAX_CAPS (-> FL 12_0). Exact unless an app aliases heap tiles across
  resources or relies on unmapped tiles reading zero (test_large_heap / tiling tests show those expected diffs).
- Fix (SPIRV-Cross): sparse feedback (OpImageSparse*, OpImageSparseRead, OpImageSparseTexelsResident) emulated as
  always resident: plain op into a texel temporary, result { 0, texel }; residency check -> (code == 0).
- Result: demo no longer crashes; it runs its shader-compilation screen (first run is slow: ~12,800 MoltenVK shader
  dump files after 17 min, no errors). Sweep5 (mixed with the tiled change mid-run): 5 improvements; flagged tests were
  collisions with concurrent runs, or newly-run tiled tests that previously skipped.

## 19. PRAGMATA SKETCHBOOK demo runs in game
- After first-run shader compilation (~40 min under Rosetta with shader dumps on), the demo reached gameplay.
- Live logs during play: no MoltenVK errors, no device loss, no vkd3d errors (only long-standing fixmes).

## 20. Rendering + performance round
- Black 3D world: MoltenVK dropped blending on RGB9E5Float on Mac GPUs (format caps); RE Engine accumulates HDR
  lighting into R9G9B9E5 with blending. Fix: allow blending on Apple GPUs when all RGB channels are written. World renders.
- Hair blown out (still): visibility buffer = 64-bit atomic max of (depth<<32 | MESHLET_ID) (1 FS + 3 CS). M1 has no
  64-bit atomics; emulation now restores the displaced low word when its high word is overtaken (fixes the 2-writer
  race). Hair unchanged -> likely another cause; open.
- Stutter: Metal pipeline compiles (up to ~800 ms) during play; Metal's own cache persists
  (~/…/com.codeweavers.CrossOver.wineloader/com.apple.metal, 885 MB) but MoltenVK rebuilds invalidate it.
- New: persistent SPIR-V -> MSL disk cache in MoltenVK (~/Library/Caches/MoltenVK/msl, keyed by SPIR-V + full config +
  dylib identity, verified on load; MVK_CONFIG_DISK_SHADER_CACHE=0 disables). Warm launch: 1,476 conversions vs
  ~29,000 before (saves ~9 min CPU/launch); also dedups identical shaders across vkd3d shader modules.
- Menu benchmark (build/bench_menu.sh): ~32-39 FPS, dominated by the game's continuous background PSO creation.
  MVK_CONFIG_SHOULD_MAXIMIZE_CONCURRENT_COMPILATION=1 speeds compiles (61 -> 28-39 ms avg) but FPS within noise;
  PREFILL_METAL_COMMAND_BUFFERS=1 had no effect on encode cost (vkd3d) and measured slower: not adopted.
- xctrace (Metal System Trace) fails from this app (DTServiceHub) even with DevToolsSecurity enabled; works from Terminal.app.

## 21. Hardening plan (generic; ordered by impact)

Symptom that motivated it: a game presents at ~90 FPS (Metal HUD live) but the window is black, no UI;
only the Win32 cursor shows. So frames are presented but the final image is black, or the game is stuck
in a state that draws nothing (e.g. waiting on loading).

Rule: make each relaxed feature work for real; only where Metal makes that impossible, stop reporting it.

1. Failure log (always on, one line per event, own file): PSO/MSL/Metal pipeline compile failures,
   draws/dispatches skipped for a missing pipeline, Metal command-buffer errors, device loss,
   unsupported SPIR-V ops. Plus a cheap black-swapchain detector (sampled at present).
2. D3D12 guarantees currently skipped via RELAX_CAPS: robustBufferAccess2/robustImageAccess2
   (out-of-bounds reads return 0) and nullDescriptor (null SRV/UAV/CBV read 0). Garbage or NaN here can
   poison temporal history (TAA/upscalers) permanently. Implement in SPIRV-Cross/MoltenVK.
3. Final output path: HDR10 (R10G10B10A2 + ST2084) and scRGB (FP16) swapchains through DXVK dxgi ->
   MoltenVK -> CAMetalLayer/EDR. Extend probe6; fix, or stop reporting HDR outputs.
4. DirectStorage (both RE Engine titles ship dstorage.dll): file IO path under Wine and GPU GDeflate
   decompression. Test with Microsoft's open-source DirectStorage samples.
5. Upscaler libraries shipped by games (FSR 3 dx12, XeSS, Streamline/DLSS): FSR/XeSS must run;
   DLSS/Reflex must fail cleanly.
6. No silent hangs: Metal command-buffer error/device loss -> signal pending fences, return DEVICE_REMOVED.
7. Turn emulations into the real thing: int64 atomic loops with guaranteed forward progress; tiled
   resources on Metal sparse textures where possible (unmapped tiles read 0), emulation for the rest;
   single-texel-alignment texel buffers; transform feedback; R9G9B9E5 render-target blending.
   Not possible on Metal: per-shader FP32 denorm mode (document, keep).
8. Honest caps for anything left from 7; full regression sweep (vkd3d tests + demo) after every step.

## 22. Step 1 done: always-on failure log (C:\gfx-failures.log)

One file per bottle: `drive_c/gfx-failures.log`, appended by both vkd3d-proton (Windows side) and MoltenVK
($WINEPREFIX/drive_c; else ~/Library/Logs). One line per event: time, component[exe or pid], category.
Each distinct message (numbers/hex collapsed) is written in full 3 times; after that, "repeat: +N more" summaries
every 10 s. Disable: VKD3D_FAILURE_LOG=0 / MVK_FAILURE_LOG=0 (or set either to another path).

Categories:
- vkd3d `error` / `fixme`: every vkd3d ERR and FIXME (unimplemented path hit), regardless of VKD3D_DEBUG.
- vkd3d `create-failed`: any device Create* (resources with dim/format/size/flags, heaps, PSOs, root signatures,
  descriptor/query heaps, command signatures) returning a failure HRESULT to the game.
- vkd3d `info`: cached PSO blob from another build rejected; the following create-failed is expected.
- vkd3d `draw-skipped`: no pipeline variant could be created, or render targets could not be set up.
- vkd3d `device-removed`: device lost, with reason.
- mvk `error` / `warning`: every MoltenVK error and warning (shader conversion, MSL compile, compile timeout,
  pipeline creation...). Filtered: "does not support disabling primitive restart" (benign, ~400/s).
- mvk `draw-skipped` / `dispatch-skipped`: draw or dispatch dropped because its pipeline failed to compile.
- mvk `gpu-error`: Metal command buffer failure with code and faulted/affected encoders; `device-lost`.
- mvk `swapchain`: format + color space (sRGB, HDR10-PQ, scRGB...) + size + present mode for each swapchain.
- mvk `present`: black-screen detector. Every 30 presents a 4x4 grid of the swapchain image is read back; logs
  "only black frames presented for N s" after 5 s (then each minute), "first visible frame", and recovery.

Verified on the demo: lines from both layers, swapchain RGB10A2 sRGB, first visible frame, black logo fades
detected and recovered. The demo's create-failed lines are all stale cached PSO blobs.
Seen at startup on every run (known gaps, steps 2/7): no transform feedback, no single-texel alignment,
no robustness2 (robust buffer/image access, null descriptors).

Open: Steam's steamwebhelper crashed 3 times (page fault reading 0x17 at 0x6FFFFBC3532C, after
"Terminating renderer for bad IPC message, reason 213") while the new MoltenVK was installed. That helper does
load libMoltenVK through winevulkan. The previous build ran 2 min without a crash, but so did the new one
afterwards, and the helper never wrote to the failure log. Not reproduced; no backtrace yet.

## 23. Step 2: robustBufferAccess2 + nullDescriptor (real, no longer relaxed)

Finding: the gap was worse than "OOB reads aren't zero". vkd3d/dxil-spirv (and vkd3d-shader for DXBC) range-check
every raw/structured view against the offset buffer and, when out of range, substitute a huge index
(e.g. `buf[cond ? idx : 1073741820u]`), relying on the driver's robustness to drop it. MoltenVK has none, so those
writes landed ~4 GB away or (32-bit offset wrap) 16 bytes before the view: silent corruption of other resources.

Fix (SPIRV-Cross MSL, option `robust_buffer_access`; MoltenVK enables it when the app enables robustBufferAccess or
robustBufferAccess2; `MVK_ROBUST_BUFFER_ACCESS=0` turns it off for debugging):
- Access chains into StorageBuffer/Uniform blocks (descriptor arrays included) record a guard
  `uint(idx) < (size - member_offset) / array_stride`, using the per-descriptor size table MoltenVK already keeps for
  OpArrayLength (size 0 for null descriptors).
- Loads become `guard ? load : T{}`; stores are wrapped in `if (guard)`; atomics (32-bit and our 64-bit emulation)
  return 0 / are skipped. Shares the helper-invocation discard-check machinery.
- Size/level/sample queries on null textures return 0 (`is_null_texture`); Metal reports 1.
- MoltenVK: null vertex buffers bind a 4 MiB zero buffer (was a null-pointer deref).
- MoltenVK bug (upstream): descriptor writes stored buffer sizes at slot `i` instead of `start + i`, so any
  descriptor written past array element 0 had the wrong size (also broke OpArrayLength on descriptor arrays).
- Features now reported: robustBufferAccess2 = true, nullDescriptor = true (Apple GPUs). vkd3d's
  "Robustness2 not supported" / "Null descriptor not supported" errors are gone; RELAX_CAPS no longer needed for them.
Not covered: buffers reached through raw pointers (BDA / root descriptors) — D3D12 has no bounds for those either.

Results: test_buffers_oob_behavior_dxbc/dxil 68/68 failures -> 0; vectorized_byte_address 192 -> 0;
test_uav_robustness_oob_structure_element 5 -> 0; test_null_descriptor_resinfo 4 -> 0; test_null_vbv 1 -> 0;
null SRV/UAV/CBV still 0.

Full sweep (sweep6 vs sweep5, 597 tests): 23 improvements, 0 regressions attributable to step 2. Besides the targets:
test_shader_fp16 and test_denorm_behavior_dxil no longer crash; test_undefined_structured_raw_alias 1543 -> 414,
test_undefined_typed_read_structured_raw 464 -> 48, tex1d/tex2d array reinterpretation 3968 -> 1920,
rwtex1d/rwtex2d array reinterpretation 2168 -> 120.
"Worse" entries, all explained:
- Tiled suites that sweep5 skipped (baseline partly predates the tier-2 tiled emulation) now run and show the
  emulation's known limits: get_resource_tiling, update_tile_mappings(_remap_*), buffer/texture feedback,
  suballocate_va_alignment (extra reserved-resource checks, same 64 KiB VA-alignment failure rate).
- test_create_reserved_resource crashed (assert in the emulated tiling path for a non-2D texture). Fixed in vkd3d:
  emulated reserved resources are now validated like tier 2 (buffers ROW_MAJOR + no clear value; textures
  64KB_UNDEFINED_SWIZZLE, 2D, single plane, no packed mips in arrays below tier 4) and flagged
  VKD3D_RESOURCE_EMULATED_RESERVED so GetHeapProperties fails as for real reserved resources. Now 26/26.
- test_unused_attachments_mix_and_match 1977 -> 1978: same with MVK_ROBUST_BUFFER_ACCESS=0, so not the guards.
Demo: no draw-skipped / gpu-error / device-lost; robustness2/nullDescriptor startup errors gone. First run after the
change shows longer black phases (all MSL changed -> every pipeline recompiles); by the third run the timeline matches
step 1 (23 s vs 24 s loading black). Frame interval A/B (MVK_CONFIG_PERFORMANCE_TRACKING): menu ~9.2 ms with guards vs
~9.0 ms without (noise); the later heavy phase is ~65-70 ms (~15 FPS) either way — the open performance item.

## 24. Step 3: output path (SDR / scRGB / HDR10) — verified correct, nothing to fix

probe7 (build/probe/d3d12probe7.c; optional arg = case index) clears each swapchain to a known colour and presents;
MoltenVK's present probe with `MVK_PRESENT_PROBE_LOG=1` logs the presented pixel (new `present-sample` lines), and
`swapchain` lines now include the CAMetalLayer colour space, EDR flag and pixel format.
Results (presented values bit-exact in every case):
- RGBA8 sRGB -> BGRA8 layer sRGB, EDR off: 191 64 128 255 (= 0.5/0.25/0.75, BGRA order).
- RGB10A2 sRGB -> layer sRGB: r=512 g=256 b=767.
- RGBA16F scRGB (2.0, 0.5, 0.25) -> layer ExtendedLinearSRGB, EDR on: 4000 3800 3400 3c00 (values > 1 preserved).
- RGB10A2 HDR10 -> layer ITUR_2100_PQ, EDR on: r=512 g=256 b=767 (only with DXVK_HDR=1).
- RGBA16F + sRGB is rejected (E_INVALIDARG) and stays scRGB, matching DXGI.
Default (DXVK_HDR unset): output reports sRGB, HDR10 not offered (like Windows with HDR off); FP16 is scRGB.
DXVK_HDR=1: output reports HDR10 (max 1499 nits), HDR10 swapchains accepted and presented on a PQ layer.
Not verifiable by readback: how macOS tone-maps EDR/PQ content on the display itself.
Conclusion: a black window is not caused by format/colour-space selection or the present blit.

## 25. Step 4: DirectStorage works; found and fixed the feature-level cap (11_0 -> 12_0)

DirectStorage probe (build/probe/dstorageprobe.c; declarations from Microsoft's MIT dstorage.h, SDK 1.x) loads a
game's own dstorage.dll + dstoragecore.dll (copied from the demo) and checks each request byte-for-byte with timeouts:
file->memory, file->GPU buffer (64 KiB offset), 512 scattered file->buffer requests, GDeflate file->buffer (GPU
decompression via vkd3d's meta command; GetCompressionSupport = 0x19), GDeflate file->memory (CPU), GDeflate
memory->buffer. All pass, 3-20 ms per 8 MiB. Wine has no IoRing (QueryIoRingCapabilities stub), dstorage falls
back to Win32 file IO correctly. DirectStorage is not a hang source.

Found on the way: D3D12CreateDevice(FL 12_0) returned E_INVALIDARG; max feature level was 11_0.
- FL 11_1 needs OutputMergerLogicOp, which MoltenVK only reports with private Metal API. MoltenVK is now built with
  MVK_USE_METAL_PRIVATE_API=1 (both rebuild scripts). That turns on real Metal logic ops, primitive restart disable
  (D3D strip-cut "disabled" now honoured; the ~400/s warning is gone), hardware sample mask, wide lines,
  provoking-vertex-last, non-seamless cube maps.
- FL 11_1 also checked maxPerStageDescriptorStorageBuffers/Images >= 64; MoltenVK reports Metal's 31 discrete buffer
  slots. vkd3d never binds UAVs through per-stage slots (descriptor tables go through bindless update-after-bind
  heaps, ~1M on argument buffers; this is why it already reports binding tier 3), so the check now takes the max of
  the plain and update-after-bind limits (vkd3d device.c).
- Result: max feature level 12_0 (12_1 needs ROVs + conservative rasterization, not available on Metal).
  Before this every game saw 11_0: a 12_0 device request failed, or the game took its FL11 fallback.
Demo at FL 12_0: same timeline as before (first frame immediately, 10 s logo fade, 25 s loading black), no errors.
Rollback copies: system32/d3d12*.dll.step2, CrossOverDev lib/*/libMoltenVK.dylib.step3.
Corrections after the full sweep (sweep7) and a new rendering test:
- Metal's private "OpenGL mode" (what unlocks disabling primitive restart) switches clipping to GL's z range [-w, w]:
  test_depth_clip 0 -> 2 failures (geometry with z < 0 drawn instead of clipped). Now off unless
  MVK_METAL_OPENGL_MODE=1 (new mvkUseMetalOpenGLMode()); restart-disable and VK_EXT_primitive_topology_list_restart
  follow it. So primitive restart stays always-on as before (test_primitive_restart 1, test_dynamic_index_strip_cut 6
  failures, unchanged from step 2).
- Private hardware sampleMask is applied after the fragment shader, so SV_Coverage input lacked the mask
  (test_coverage 0 -> 5). Sample masks stay emulated in the shader.
- Logic ops were reported but never applied: MoltenVK set them only for blendable formats, and D3D12 logic ops are
  for integer formats, which are not blendable. Fixed (set whenever logicOpEnable and the attachment is used).
  New vkd3d test test_logic_op_rendering (XOR/AND/OR/COPY/NOOP on R8G8B8A8_UINT, exact values): 0 failures
  (was 4/5 failing before the fix).
Final sweep (sweep8 vs sweep6, 597 shared tests + new test_logic_op_rendering): 0 regressions, 1 improvement
(test_create_reserved_resource crash -> 26/26); test_logic_op_rendering 84/84. Demo at FL 12_0: normal timeline, no errors.

## 26. Step 5: upscaler / frame-generation libraries — all work or fail cleanly (no fixes needed)

Probes drive the demo's own DLLs on vkd3d (build/probe/{fsrprobe,xessprobe,xefgprobe}.c; declarations from AMD's
MIT ffx_api headers and Intel's public XeSS / XeSS-FG / XeLL headers). Flat-colour inputs, zero motion, 4 frames,
every output pixel checked against the input colour and for NaN/Inf/black:
- FSR (amd_fidelityfx_dx12.dll, provider 3.1.4): 960x540 -> 1920x1080, SDR (max err 0.0002) and HDR >1.0 with
  inverted depth (max err 0.002): all 2,073,600 pixels correct.
- XeSS 2.0.2 (libxess.dll, Balanced -> 960x540 input): HDR and LDR+inverted depth: all pixels correct (max err 0.0005).
- XeLL 1.2.0 (libxell.dll): context + sleep mode accepted (calls are no-ops without Intel's driver).
- XeSS-FG 1.2.0 (libxess_fg.dll): context + init from the app swapchain succeed (note: FG takes ownership of the app
  swapchain and destroys it, per Intel's guide); presenting through the FG proxy shows the exact cleared colour.
  xefgSwapChainSetEnabled(1) returns -15 (latency reduction requirements not met: needs Intel driver XeLL), so FG
  stays a pass-through; frames still presented correctly. Clean refusal.
  Side note: FG creates many tiny timestamp query heaps; Metal caps hardware counter sample buffers, so MoltenVK logs
  "Could not create MTLCounterSampleBuffer ... Reverting to emulated behavior" per heap (harmless, emulated timestamps).
- DLSS / Streamline / Reflex: in the running demo only sl.interposer.dll is mapped; no sl.common/sl.dlss/nvngx
  (Streamline found no NVIDIA GPU and loaded no plugins); game continues. Clean.
Loaded by the demo at runtime (lsof): amd_ags_x64, amd_fidelityfx_dx12, dstorage(+core), libxess, libxess_fg, libxell,
nvapi64, sl.interposer.

## 27. Step 6: no silent hangs — Metal's 0.5 s watchdog no longer kills the device; Present reports removal

Probe build/probe/hangprobe.c (shaders build/probe/hang/*.hlsl, compiled with DXC in the test bottle, C:\dxc):
- fault: write through a root UAV at an unmapped VA (+1 TiB): Apple GPU drops it silently (soft fault). No error,
  no hang, fence completes. Harmless.
- hang: compute loop far longer than any frame. Metal kills the command buffer after ~0.5 s with
  kIOGPUCommandBufferCallbackErrorImpactingInteractivity (Windows TDR is 2 s). The fence wait returns (no hang).
Before: MoltenVK marked the device LOST on any command-buffer error; vkd3d noticed at the next submit
(DXGI_ERROR_DEVICE_REMOVED, fences -> UINT64_MAX), but Present kept returning S_OK while nothing rendered:
one slow dispatch = a permanently black game that still "presents" (cursor visible, frame counter running).
Fixes:
- MoltenVK: MVK_CONFIG_RESUME_LOST_DEVICE defaults to 1 here. A failed command buffer's work is dropped, the device
  stays usable; GPU removal / not-permitted still mark it lost. Failure log: "gpu-error ... device resumed".
  Verified: after the kill, follow-up work completes, Present works, a 1 MiB GPU copy is bit-exact.
  MVK_CONFIG_RESUME_LOST_DEVICE=0 restores the old behaviour.
- vkd3d: dxgi_vk_swap_chain_Present returns the removed reason (DXGI_ERROR_DEVICE_REMOVED) once the device is
  removed, like DXGI, instead of S_OK. Verified with resume off: Present -> 0x887a0005.
Demo: unchanged timeline, no gpu-error lines.
Rollback copies: system32/d3d12*.dll.step5, CrossOverDev lib/*/libMoltenVK.dylib.step5.

### 27b. Requiem's own failure log (read, per the user: our stack's diagnostics)
5 runs of re9.exe: no gpu-error / device-lost / draw-skipped / dispatch-skipped; create-failed lines are only stale
cached PSO blobs. Swapchains: 1920x1080 -> 882x550 -> 2074x1296; ~20 s in the game recreates at 882x550, shows the
credits there (visible frames), then only black frames for the rest of the run (real presents, black content).
vkd3d warn log on every launch: "VK_EXT_dynamic_rendering_unused_attachments not supported. The functionality in this
EXT is required for correct operation." -> fixed below.

### 27c. VK_EXT_dynamic_rendering_unused_attachments implemented in MoltenVK
D3D12 allows PSO RTV/DSV formats to differ from what is bound (pipeline writes a slot with nothing bound, or a bound
target the pipeline doesn't declare). vkd3d has no fallback and relies on this extension; Metal needs pipeline formats
to equal the render pass attachments, so every such draw was undefined (test_unused_attachments_mix_and_match:
1978/5227 failures). MVKGraphicsPipeline now keeps its final MTLRenderPipelineDescriptor; at bind time
(prepareDraw) the bound subpass formats are compared and, when only presence differs, a variant is compiled and cached:
bound-but-undeclared slots get the bound format with writeMask None, declared-but-unbound slots get no format,
depth/stencil follow the bound attachments. Plain pipelines (what vkd3d uses); tessellation/mesh/multiview keep the
old behaviour and a failed variant logs "draw-mismatch". Extension + feature advertised.
Result: test_unused_attachments_mix_and_match 1978 -> 0 failures; test_null_rtv, test_unbound_rtv_rendering still pass.

### 27d. NaN/Inf semantics (precaution, no failure found)
Nearly every D3D-translated shader was compiled with MTLMathModeFast (SPIRV-Cross reports all fast-math flags unless
a shader asks for IEEE), which assumes no NaN/Inf. Probe build/probe/nanprobe.c (+ hang/nan.hlsl): isnan/isinf/isfinite,
min/max/saturate/clamp with NaN, x/0 all correct in every mode; the three "wrong" results (NaN==NaN, NaN!=NaN,
NaN*0) are folded to constants by DXC itself (verified in the DXIL), so Windows behaves the same. Still, MoltenVK now
never assumes no-NaN/Inf (Relaxed mode with fast functions; MVK_ALLOW_NO_NAN_FAST_MATH=1 restores Fast), matching
how D3D drivers honour NaN. First launch after this recompiles all Metal shaders (longer black phases once).

### 27e. Sweep of all step-6 changes (sweep11 vs sweep8) and two follow-up fixes
3 entries marked worse, all resolved:
- test_renderpass_rendering: 3 MoltenVK errors "depthAttachmentPixelFormat is not valid and shader writes to depth":
  an attachment variant dropped depth for a pipeline whose shader writes depth. Variants are now compiled directly
  (silently) and, if that fails, retried keeping the pipeline's own depth/stencil formats. Now 0 errors.
- test_unused_attachments_mix_and_match: 0 failures but 157 s (512 PSOs x 512 RT layouts -> many variants), past the
  sweep's 90 s timeout. build/sweep.sh per-test timeout raised to 300 s. Variant compiles happen once per
  (pipeline, bound-attachment-presence) pair, synchronously at first draw: a possible one-time hitch per new pair.
- test_resolve_image_exhaustive_descriptors: hung with resume on. Its command buffers fail with Metal
  OutOfMemory (pre-existing 1024 failures); semaphore signals are encoded into the submission's last
  MTLCommandBuffer, so when it failed they never ran and D3D12 fence waits (timeline semaphores) blocked forever.
  MoltenVK now delivers those timeline signals from the host when the carrying command buffer fails
  (signalOnHostAfterGPUError). Test back to 16 s with its old 1024 failures. Open: why those resolves run out of
  GPU memory (1025 failed command buffers).
Rechecked: fence-wait robustness tests, device_removed_reason, hang probe (GPU usable after the kill, copy bit-exact).

## 28. Black-screen diagnostics: 64-bit atomic usage and a per-render-pass probe
- Failure log "atomic64" lines (MVKShaderModule, both disk-cache and fresh conversions): stage, emulated ops and
  counts, buffer/threadgroup. Demo: 1 fragment + 3 compute shaders, UMax only (visibility buffer). Requiem: identical
  (same 4 shaders, UMax only) -> torn 64-bit atomics are unlikely to cause Requiem's black screen.
- Per-render-pass probe (MVKCommandBuffer.mm, on by default, MVK_PASS_PROBE=0 disables): at 10/30/60/120 s after the
  first present one whole frame is captured; after each Metal render pass a 3x3 grid of every color target is copied
  (fence after the fragment stage) and summarized into $WINEPREFIX/drive_c/gfx-passes.log: pass index, compute
  dispatches since the previous pass, per target format/size, zero samples, NaN/Inf, max value. MSAA, memoryless,
  framebuffer-only and 3D targets are listed but not sampled; compute outputs are not sampled (dispatch counts only).
  Demo gameplay frame: 117 render passes, ~230 dispatches, HDR RG11B10F scene targets, visible RGB10A2 final output.

## 29. Requiem black screen: FIXED — the game was given D3DMetal's d3d11 next to DXVK's dxgi

Root cause (setup, not a translation bug): only d3d12/d3d12core/dxgi were per-app native for re9.exe, so d3d11
fell back to CrossOver's default, which on Apple Silicon is D3DMetal (lib/apple_gptk/wine/x86_64-windows/d3d11.dll).
After the intro logos RE Engine starts Media Foundation and creates a DXGI device manager for video, which needs a
D3D11 device. D3DMetal's d3d11 on top of DXVK's dxgi crashes in D3D11CreateDevice (call through a null pointer,
reproduced by build/probe/d3d11mfprobe.c); the game swallows it and waits forever behind a black fade.
Fix: build DXVK's d3d11 (src/dxvk: ninja -C build.64 src/d3d11/d3d11.dll), copy into the bottle's system32
(old one saved as d3d11.dll.pre-dxvk), add HKCU\Software\Wine\AppDefaults\re9.exe\DllOverrides d3d11=native.
Probe with DXVK d3d11 + dxgi: FL 11_1 with VIDEO_SUPPORT, ID3D10Multithread, ID3D11VideoDevice, ResetDevice OK.
Confirmed by the user: Requiem now gets past the intro credits.
Rule for packaging: d3d11 and dxgi must come from the same implementation (DXVK with DXVK); never mix with D3DMetal.

How it was found (all in our stack's logs, nothing attached to the game):
- pso-retry (state.c): rejected cached PSO blobs are all recreated -> not the cause.
- fence-stall (command.c, reported from Present): no CPU/queue waits >= 5 s, no stuck polling -> not GPU-bound.
- backbuffer-draw logger: final StretchBlt state correct; one-time shader + root-signature dump (C:\gfx-bbdraw-*);
  render-target ring and per-list event recorder (barriers/clears/discards/copies/dispatches, snapshotted at record).
- build/probe/blitprobe.c ran the game's own StretchBlt PS through our stack correctly; uavrwprobe.c: typed UAV
  RW works for RGB10A2/RGBA8/RGBA16F.
- one-time GPU readback (vkd3d_diag_readback) + MoltenVK texture identity (pass probe "tex/storage", copy-probe):
  same texture; the scene itself was black (HDR max ~0.001) under an opaque black UI fade -> game stuck in a transition.
- queue-activity (per-queue submits/copies/dispatches/signals + creating DLL): streaming stopped, no DirectStorage queues.
- Wine MF tracing via the Steam launcher (--debugmsg "-all,err+all,+mfplat,..."; note: enabling Wine's fixme
  channel makes winevulkan's get_physical_device_properties2 crash — separate Wine bug) showed re9 only calls
  MFStartup + MFCreateDXGIDeviceManager; loaded modules showed D3DMetal's d3d11.
Experiments left off by default: VKD3D_DISPATCH_BARRIER=1 (barrier after every dispatch; did not help).
Diagnostics still compiled in and on by default (to gate off before release): backbuffer-draw logger, event ring,
GPU readback, fence-stall, queue-activity, pso-retry, pass probe texture identity, copy-probe.
Also seen: MoltenVK warns "Metal does not support disabling primitive restart" (strip draws with cut disabled) — a
correctness gap only for meshes that use the max index value; added to the hardening list.

## 30. Pragmata performance: GPU-bound; indirect-count draws were splitting the G-buffer into 73 render passes

Diagnostics are now off by default (VKD3D_DIAGNOSTICS=1 for the vkd3d ones; MVK_PASS_PROBE=1 for the pass/copy probes).
Profiling tools: MVK_FRAME_STATS=1 (C:\gfx-frames.log: fps, worst frame, GPU busy/frame, compile counts+time per 2 s),
MVK_CONFIG_GPU_PROFILE=1 (per-encoder GPU time ranking in the Steam log; render passes now tagged "fs <hash>"),
MVK_PROFILE_SHADER_DIR=dir (MSL of every compute/fragment shader named by those hashes; keeps the disk cache on).
Setup fix: Pragmata also needs d3d11=native (DXVK d3d11) — it crashed once (0xe0465043) with D3DMetal's d3d11 + DXVK dxgi.
Profile (one ~5 min session): 11-16 fps in heavy scenes, GPU busy 60-80 ms/frame = 88-95% of frame time; busiest CPU
thread median 20% -> GPU-bound. >70% of encoder GPU time in G-buffer passes (RGB10A2, RGBA8 sRGB, RGB10A2, RG16?
+ D32S8 at 2074x1166). Frame structure: 187 render passes, 73 of them G-buffer passes with exactly one
DrawIndexedIndirectCount each. Cause: MoltenVK converts each indirect-count draw with a compute dispatch at the draw,
ending and restarting the Metal render pass (store + reload of 4 colour targets + depth/stencil at 2K) every time.
Fix: MVKCommandEncoder::hoistIndirectCountConversions — when a render pass begins, look ahead to its end and encode all
its indirect-count conversions first; the draws use the pre-converted args (MVK_HOIST_INDIRECT_COUNT=0 disables).
All 8 execute_indirect tests give identical results with and without the hoist (pre-existing failures unchanged).
Also seen: steady render/compute pipeline compiling during gameplay (4-6 s of compile time per 2 s window) and 1.5-15 s
hitches during compile bursts — next target after the pass-split fix.

## 31. Compile hitches: persistent Metal pipeline cache (MTLBinaryArchive)

Result of the hoist fix (section 30), same profiling: median fps 12.4 -> 15.5, p10 9.6 -> 12.8, GPU 72 -> 58 ms/frame,
2 s windows with a >500 ms hitch 12 -> 4; render passes per frame 187 -> 66, G-buffer passes 73 -> 4.
Hitch cause: Metal's implicit shader cache (DARWIN_USER_CACHE_DIR/com.apple.metal) is never written by the Wine
processes, so every launch recompiled every pipeline (4-6 s of compile time per 2 s window throughout play), and a
pipeline needed now waited behind the queue. (Also: MoltenVK's own MSL disk cache is keyed on the dylib's size+mtime,
so each rebuild today invalidated it — irrelevant for users of a fixed build.) vkd3d also rejects ~600 of the game's
cached PSO blobs per launch (desc hash mismatch), so the game re-creates them every time.
Fix: Utility/MVKBinaryArchive.h — one MTLBinaryArchive per program in ~/Library/Caches/MoltenVK/archives/
(<exe>-<registryID>.metalarchive). Render/compute/mesh pipeline creation and the attachment-format variants first try
a fail-on-miss lookup; on a miss the functions are compiled into the archive, then created normally. Saved in the
background at most once a minute while dirty, and at exit. Stale serializer temp files (<UUID>.metallib >10 min) are
deleted at startup. MVK_BINARY_ARCHIVE=0 disables. Probe: second run compute PSOs 2167 ms -> 24 ms; archive survives
a MoltenVK rebuild. Multithread/pipeline/indirect tests unchanged (test_create_pipeline_state's 1 failure pre-existing).
Caveat: concurrent additions to one archive are not documented as thread-safe by Apple; held up in tests so far.

## 32. Correction: Metal's cache works under Wine; binary archive switched off

The section 31 premise was wrong: Metal's implicit cache for Wine processes lives under
DARWIN_USER_CACHE_DIR/com.codeweavers.CrossOver.wineloader/com.apple.metal (2.5 GB, updated today) plus
com.apple.gpuarchiver (1.1 GB); I had only looked at the shared com.apple.metal folder. A native x86_64 (Rosetta) test
program also hits Metal's cache on its second run (48 ms -> 0.8 ms).
The binary archive was also unsafe: a periodic exclusive save deadlocked Pragmata mid-cutscene; exit-only saving never
ran for Pragmata; the segment rewrite worked under the 5227-pipeline stress test, but Metal failed to serialize a large
segment (MDB_MAP_FULL) and hung indefinitely loading a 21 MB segment it had written, even in a native program.
MVKBinaryArchive.h is now OFF by default (MVK_BINARY_ARCHIVE=1 to experiment); saved archives deleted.
Profiling of the last session (68605): the frame drops coincide with render-pipeline compiles taking ~200-500 ms each
(GPU busy falls to ~48% in those windows vs ~90% when smooth) — the game waiting on PSO creation. Every session today
followed a MoltenVK rebuild, which invalidates MoltenVK's own MSL cache; whether a warm launch (no rebuild in between)
still compiles this much is not yet measured. Open question: why render PSO creation stays slow with Metal's cache warm
(candidates: Metal cache eviction at its size limit, descriptors that differ per launch, compiler-service queueing).

## 33. Deferred Metal render pipeline compilation (MVKDeferredPSO.h)

Warm profile (no rebuild before it): the game re-creates its whole pipeline set every launch — 48,213 render PSOs and
92,842 shader conversions in 262 s. Per pipeline, warm: ~1.7 ms MSL (disk cache), ~3.6 ms Metal libraries, ~11 ms Metal
render pipeline -> ~16 ms vs ~0.05 ms for a Windows driver-cache hit, so the warm-up runs for minutes during play and
the game stalls (GPU busy 34% in freeze windows) when it needs a pipeline still queued.
Fix: vkCreateGraphicsPipelines (plain non-multiview, non-tess, non-mesh path) builds the descriptor and hands the Metal
render pipeline compile to MVKDeferredPSOScheduler, which keeps 4 compiles in flight (MVK_DEFERRED_PSO_INFLIGHT); the
first getMainPipelineState()/getPipelineStateForAttachments() for a still-queued pipeline submits it ahead of the queue
and waits for it alone. Compile failures surface at first use (logged "deferred-pso"; draws with no state are skipped as
before). Frame stats gained "draws waited on PSO n (ms)". MVK_DEFERRED_PSO=0 disables.
Tests: 20 rendering/pipeline/multithread tests identical with deferral on and off (pre-existing failures unchanged);
build/probe/psostress.c (8 threads x 300 PSOs, half destroyed unused, rest drawn and checked) identical on and off.
Previous MoltenVK saved as libMoltenVK.dylib.pre-deferred.

## 34. Blown-out hair: FIXED — SPIRV-Cross compare-exchange re-read a forwarded load
- Root cause (Requiem; same engine path in Pragmata): the hair k-buffer compositor (cs 62433f4466fc709e) blends overflow
  strands with `old = gs[i]; new = old + cov^2*col; InterlockedCompareExchange(gs[i], old, new, orig); ok = orig == old`
  (retry up to 128x). SPIRV-Cross forwarded the load of `old`, so the CAS loop and the success test re-read memory:
  the CAS always succeeded, the test reported failure, and the strand colour was re-added up to 128x -> saturated
  (Inf before the RTZ fix). GPU repro: 64 increments -> 1695 (buggy) vs 64 (fixed).
- Fix (spirv_msl.cpp, OpAtomicCompareExchange): force the comparator to a temporary (forced_temporaries + recompile).
- Also landed on the way (kept, correct on their own):
  - dxil-spirv LegacyF32ToF16 exact round-toward-zero (D3D spec; Metal packHalf rounds to nearest -> Inf on overflow).
    DXIL_SPIRV_F16_RTE=1 restores the old behaviour.
  - 64-bit atomic max emulation: per-element lock on the low word (no torn depth/payload pairs).
  - DXVK dxgi: Windows-like mode list when Wine reports >64 sizes (Requiem 882x550 render fix). DXVK_DXGI_MODE_FILTER=0.
- Debug tooling added: MVK_MSL_OVERRIDE_DIR (per-hash MSL replacement), MVK_SAFE_MATH_SHADERS, GPU "printf" buffer at
  Metal buffer index 20 (MVKDebugBuffer.h -> C:\gfx-dbg.bin, decoder build/dbgcap/decode.py), pass probe now records
  per-pass fs/cs hashes, 24 full rows per target, hot-value histograms. Local MSL compile check: scratchpad mslcheck.
- MoltenVK must be rebuilt with build/rebuild_mvk.sh when SPIRV-Cross changes (plain `make macos` links a stale lib).

## 35. Clean install with the repo scripts; PlayStation controllers in XInput games

- Fresh test: scripts/install.sh (APFS clone of CrossOver Preview + our MoltenVK), setup-bottle.sh Steam-D3D12
  --create --install-steam, launch-steam.sh. Both games run from the new bottle. The Pragmata Steam-launch stall of
  the old dev bottle (section 34 follow-up) does not occur: it was that bottle's state, not Denuvo or our stack.
- Non-Steam games had no controller, with or without Steam Input. tools/dev/probe/inputprobe.c (XInput 1.4/9.1.0/1.3
  slots, DInput8 game controllers, raw HID): DualShock 4 (054c:09cc, Bluetooth) visible as raw HID and DInput, all
  XInput slots empty. winebus trace: both backends see it — SDL adds a mapped gamepad (is_gamepad 1), IOHID a raw
  device (is_hidraw 1) — and Wine prefers hidraw for DS4/DualSense ("ignoring non-hidraw device 054c:09cc"), so the
  XInput-capable SDL device is dropped. "Enable SDL"/"Map Controllers" alone change nothing.
- Fix: HKLM\System\CurrentControlSet\Services\winebus DisableHidraw=1 (bottle restart needed). XInput slot 0
  connected, device path gets IG_00. User-confirmed: controller works in non-Steam games and still in Steam games.
  setup-bottle.sh sets it by default; --native-ps-controllers keeps Wine's default. Trade-off: Xbox button icons in
  games with native PlayStation support; DS4 extras (touchpad/gyro) via Steam Input may be lost.
