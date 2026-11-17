# D3D12 on Apple M1 via CrossOver — what we built and why

Structured companion to `NOTES.md` (which is the chronological lab notebook; section numbers below, e.g. "N§23",
point into it). This document groups every change by layer and says, for each one: the symptom that led to it, the
root cause, the fix, how it was verified, how to switch it off, and whether it ships.

---

## 1. Goal and stack

Run D3D12 RE Engine games (Resident Evil Requiem, PRAGMATA SKETCHBOOK) on an M1 Max. Apple's D3DMetal can't run them
on M1: they need 64-bit atomics and M1 (Apple7) has none in hardware. So we use the open-source path, made to work:

```
Game (D3D12 / D3D11 / DXGI)
  ├─ d3d12.dll + d3d12core.dll   vkd3d-proton  ── DXIL → SPIR-V via dxil-spirv
  ├─ dxgi.dll + d3d11.dll         DXVK          (must come from the same implementation)
  └─ Vulkan (winevulkan)
        └─ MoltenVK 1.4.3  ── SPIR-V → MSL via SPIRV-Cross
              └─ Metal (Apple M1 Max)
```

Every fix lives in one of five source trees: **SPIRV-Cross**, **MoltenVK**, **vkd3d-proton**, **dxil-spirv**, **DXVK**.
Nothing in the games themselves is modified.

### Status at a glance
| | Requiem | PRAGMATA SKETCHBOOK |
|---|---|---|
| Boots, menus, gameplay | ✅ | ✅ |
| Correct image (no black screen) | ✅ | ✅ |
| Correct render resolution | ✅ (was 882x550) | ✅ |
| Hair | ✅ fixed | expected fixed (same code path), not yet confirmed |
| Performance | playable | 15–20+ fps heavy scenes, residual compile stutter |

---

## 2. Integration (how the pieces are wired)

| Item | What | Why |
|---|---|---|
| `CrossOverDev.app` | APFS clone of CrossOver with our `libMoltenVK.dylib` (x86_64 + aarch64, ad-hoc signed) | CrossOver ships MoltenVK 1.2 (Vulkan 1.2); vkd3d-proton needs Vulkan 1.3. `win32u.so` dlopens MoltenVK via an rpath inside the app and DYLD_* is stripped, so the dylib has to live inside the app. N§Setup, Finding 1 |
| DXVK `dxgi.dll` with vkd3d-proton | Built from `src/dxvk` | Wine's builtin dxgi has an ABI mismatch with vkd3d's swapchain factory (crash in libobjc). N Finding 5 |
| DXVK `d3d11.dll` | Built from `src/dxvk`, installed in the bottle | **Rule: d3d11 and dxgi must come from the same implementation.** D3DMetal's d3d11 on DXVK's dxgi crashes in `D3D11CreateDevice`; RE Engine needs a D3D11 device for Media Foundation video → Requiem black screen after the intro, Pragmata crash 0xe0465043. N§29, N§30 |
| Per-app DLL overrides | `HKCU\Software\Wine\AppDefaults\<exe>\DllOverrides`: d3d11, d3d12, d3d12core, dxgi = native | So only the games use our DLLs; Steam's UI keeps Wine's builtins. |
| MoltenVK build flag | `MVK_USE_METAL_PRIVATE_API=1` (via `build/rebuild_mvk.sh`) | Real logic ops → feature level 11_1+. N§25 |
| Rebuild rule | Always rebuild MoltenVK with `build/rebuild_mvk.sh` after a SPIRV-Cross change | Plain `make macos` links a stale prebuilt SPIRV-Cross. N§34 |
| Install rule | Copy dylib to `.new`, then `mv` | `cp` over a mapped signed dylib breaks loading in new processes. N§17 |

---

## 3. Shipping fixes by layer

Legend — **Kind**: C = correctness (wrong output / crash), F = missing feature implemented, P = performance.
**Off switch**: environment variable that restores the old behaviour (for bisecting).

### 3.1 SPIRV-Cross (SPIR-V → MSL)

| # | Kind | Symptom | Root cause | Fix | Verified by | Off switch |
|---|---|---|---|---|---|---|
| S1 | C | **Blown-out hair** (Requiem; same path in Pragmata) | Compare-exchange emitted the comparator expression several times; when it was a forwarded load, each use re-read memory → CAS always "succeeded", success test reported failure → game's retry loop re-added each strand colour up to 128x | Snapshot the comparator in a temporary (`forced_temporaries`) before the CAS loop | GPU repro: 64 increments → 1695 (old) vs 64 (fixed); vkd3d atomics tests 316/316; user confirmed hair | — |
| S2 | F | Shaders using `uint64` atomics failed to translate ("MSL does not support 64-bit atomics") | M1 has no 64-bit atomics | Emulation with 32-bit atomics on the two halves (add/and/or/xor/load exact) | test_shader_sm66_64bit_atomics 79/85 (6 = concurrent exchange, inherent) | — |
| S3 | C | Torn depth/ID pairs possible in 64-bit max (visibility buffers) | Two 32-bit halves updated without mutual exclusion | Max/SMax take a short per-element lock on the low word (deadlock-safe single-iteration pattern, bounded, falls back) | 79/85 unchanged, all min/max cases pass | — |
| S4 | C | "Fatal D3D error (1, E_FAIL)" in RE Engine | Metal SIMD reductions reject 64-bit types (WaveActiveSum on uint64) | Exact 64-bit wave-op emulation from 32-bit SIMD ops | N§17 |  — |
| S5 | F | SM 6.5/6.6 shaders | Missing emulations | Subgroup scans (582/582), compute derivatives + implicit-LOD sampling (823/823), sparse-feedback ops as always-resident | N Finding 7, N§18 | — |
| S6 | C | Metal compile errors → failed PSOs | (a) OpCopyObject of an access-chain pointer lost the flag (`*arr[i] = x`); (b) atomics on a vector component through a BDA pointer; (c) aliased descriptor casts dropped `__restrict`; (d) mesh ClipDistance arrays; (e) derivatives in helper functions missing lane id | Individual codegen fixes | 0 Metal compile failures in Pragmata after (a); mesh tests 260/260 | — |
| S7 | C | Pipeline creation rejected | Fragment output / vertex input / VS→PS interface type or width mismatches that D3D allows | Bitcast/retype options driven by MoltenVK (`fragment_output_basetypes`, `fragment_input_basetypes`, …) | test_shader_io_mismatch 3 → 0 | — |
| S8 | C | Tessellation pipelines failing | Component-decorated varyings | Remap location to location + 256*component across stages | tess tests +4, 0 regressions | — |
| S9 | C | Silent memory corruption (writes ~4 GB away) | vkd3d relies on robustness to drop out-of-range indices; Metal has none | `robust_buffer_access`: guarded loads/stores/atomics using the per-descriptor size table; null-texture queries return 0 | OOB tests 68 → 0 failures (+ several others) N§23 | `MVK_ROBUST_BUFFER_ACCESS=0` |
| S10 | C | Mesh shaders hung the GPU (user logged out) | `gl_WorkGroupSize` emitted as 0 for LocalSizeId → loops stepping by 0 | Read LocalSizeId constants; clamp strides | Mesh test suites pass | — |

### 3.2 MoltenVK (Vulkan → Metal)

| # | Kind | Symptom | Root cause | Fix | Verified by | Off switch |
|---|---|---|---|---|---|---|
| M1 | F | vkd3d refused the device / missing SM 6.6 features | Features not advertised | Advertise int64 atomics (emulated), compute derivatives, subgroup size 32, robustness2 + nullDescriptor (now real, S9) | vkd3d device caps | — |
| M2 | F | No mesh shaders | Not implemented | VK_EXT_mesh_shader on Metal 3 object/mesh stages, incl. indirect + count | Mesh suites 260/260, 57/57, 136/136 | — |
| M3 | C | Device lost on mesh draws in empty passes | AGX faults on no-attachment passes with huge w*h*layers | Cap `renderTargetArrayLength` so w*h*layers ≤ 2^34 | culling tests pass N Finding 11 | — |
| M4 | F | "Fatal D3D error (1, E_FAIL)" sources | Pipeline-statistics query pools refused; planar image arrays refused | Emulated stats pool (zeros); allow planar arrays | test_create_query_heap 8/8; planar 1774/1774 | — |
| M5 | C | Process aborts | Typed buffer with no Metal format; >2^28 texel views; sub-block compressed copies | Return errors / clamp / round up | probes N§14 | — |
| M6 | C | GPU "Invalid Resource" device loss under suballocation | Blanket `useResource` of all buffers referenced freed memory | Skip with residency sets | stress tests 3372 → 0 failures | — |
| M7 | C | Black 3D world (Pragmata) | Blending disabled on RGB9E5 render targets | Allow on Apple GPUs when all RGB channels are written | world renders N§20 | — |
| M8 | C | Undefined draws when PSO formats ≠ bound targets | Metal requires pipeline formats = pass attachments; vkd3d relies on VK_EXT_dynamic_rendering_unused_attachments | Implemented the extension: per-(pipeline, bound-presence) variants | test_unused_attachments_mix_and_match 1978 → 0 N§27c | — |
| M9 | C | Logic ops reported but not applied | Only set for blendable formats | Set whenever enabled | new test_logic_op_rendering 84/84 | — |
| M10 | C | Wrong descriptor sizes past element 0 (upstream bug) | Size written at `i`, not `start+i` | Fixed index | OOB tests | — |
| M11 | C | One slow dispatch = permanently black game that still presents | Metal's 0.5 s watchdog → device marked lost forever | Resume after command-buffer errors; host-signal timeline semaphores whose carrier buffer failed | hang probe, fence tests N§27 | `MVK_CONFIG_RESUME_LOST_DEVICE=0` |
| M12 | C | NaN scrubbing in shaders could be folded away | MTLMathModeFast assumes no NaN/Inf | Never assume no-NaN/Inf (Relaxed + fast functions) | nanprobe N§27d | `MVK_ALLOW_NO_NAN_FAST_MATH=1` |
| M13 | P | Pragmata GPU-bound, G-buffer split into 73 render passes | Each indirect-count draw converted with a compute pass mid-render-pass | Hoist all conversions of a pass before it begins | fps +25%, passes/frame 187 → 66 N§30 | `MVK_HOIST_INDIRECT_COUNT=0` |
| M14 | P | ~9 min CPU per launch converting shaders | No persistent SPIR-V→MSL cache | Disk cache `~/Library/Caches/MoltenVK/msl` (keyed by SPIR-V + config + dylib) | 29,000 → 1,476 conversions N§20 | `MVK_CONFIG_DISK_SHADER_CACHE=0` |
| M15 | P | Freezes when the game needs a pipeline still queued | ~48k PSOs re-created every launch, ~16 ms each | Deferred render-pipeline compiles (4 in flight), urgent-first when a draw needs one; library compiles yield to urgent work | stutter shorter/rarer (user); psostress identical N§33 | `MVK_DEFERRED_PSO=0` |
| M16 | C | Null vertex buffers dereferenced | — | Bind a 4 MiB zero buffer | test_null_vbv | — |

### 3.3 vkd3d-proton (D3D12 → Vulkan)

| # | Kind | Symptom | Root cause | Fix | Verified by | Off switch |
|---|---|---|---|---|---|---|
| V1 | C | Pragmata null-pointer crash at first frame | No sparse binding → TiledResourcesTier 0 → FL 11 fallback path that RE Engine doesn't really support | Reserved resources use the committed fallback; report tier 2 | game passes loading N§18 | — |
| V2 | C | Max feature level 11_0 (games requesting 12_0 failed / took fallbacks) | FL 11_1 checks per-stage UAV slots (Metal: 31) though vkd3d uses bindless heaps | Check max of plain and update-after-bind limits → FL 12_0 | N§25 | — |
| V3 | C | Present kept returning S_OK after device removal | — | Present returns DXGI_ERROR_DEVICE_REMOVED like DXGI | N§27 | — |
| V4 | C | test_create_reserved_resource assert | Emulated reserved textures not validated like tier 2 | Validation + flag | 26/26 | — |

### 3.4 dxil-spirv (DXIL → SPIR-V)

| # | Kind | Symptom | Root cause | Fix | Verified by | Off switch |
|---|---|---|---|---|---|---|
| D1 | C | Inf in packed fp16 values (hair turned Inf before S1; any overflow) | D3D's `f32tof16` is round-toward-zero (overflow → 65504); emitted as packHalf2x16, which rounds to nearest on Metal (→ Inf) | Exact RTZ: pack, unpack, step back one ulp if rounded away from zero | fp16 rounding tests (16.7M cases), shader instructions 5347 — 0 failures | `DXIL_SPIRV_F16_RTE=1` |

### 3.5 DXVK (DXGI / D3D11)

| # | Kind | Symptom | Root cause | Fix | Verified by | Off switch |
|---|---|---|---|---|---|---|
| X1 | C | **Requiem rendered at 882x550** in borderless | Wine's macOS driver reports 1248 modes (312 odd sizes, no 1920x1080); RE Engine stores a mode *index* and its default lands on 882x550 | When >64 distinct sizes are reported, expose a Windows-like list: standard sizes that fit + native + desktop, at the panel's refresh rates | Requiem renders at desktop size (user confirmed) | `DXVK_DXGI_MODE_FILTER=0` |
| X2 | — | DXVK rejects MoltenVK (no geometry shaders) | D3D11 requirement, irrelevant for DXGI-only use | Dev flag `DXVK_RELAX_CAPS=1` | — | (see §6) |

Note for users: games that saved a bad mode index under the old list keep it; clear `FullScreenDisplayMode` and the
`DisplayMode*` entries from the game's `config.ini` once.

---

## 4. Diagnostic tooling (kept, off by default)

Tools that found the bugs above. They cost nothing unless enabled; worth keeping in a dev build, not in the package.

| Tool | Layer | Enable | Output | Found |
|---|---|---|---|---|
| Failure log (always-on, low volume) | vkd3d + MoltenVK | default; `VKD3D_FAILURE_LOG=0` / `MVK_FAILURE_LOG=0` | `C:\gfx-failures.log` | E_FAIL/E_OUTOFMEMORY sources, black swapchain detector, device loss |
| Frame stats | MoltenVK | `MVK_FRAME_STATS=1` | `C:\gfx-frames.log` | fps, GPU busy, compile cost, draws waiting on PSOs |
| GPU profiler | MoltenVK | `MVK_CONFIG_GPU_PROFILE=1` | Steam log `[mvk-gpuprof]` | indirect-count pass splitting (M13) |
| Shader dumps by hash | MoltenVK | `MVK_PROFILE_SHADER_DIR=dir` | `shader-{cs,fs}-<hash>.metal` | every shader investigation |
| Pass probe | MoltenVK | `MVK_PASS_PROBE=1` | `C:\gfx-passes.log` — per pass: fs/cs hashes, 24 rows/target, NaN/Inf counts and region, hot-value histogram | located the hair pass and the k-buffer compositor |
| MSL override | MoltenVK | `MVK_MSL_OVERRIDE_DIR=dir` | replaces one shader's MSL by hash | instrumented game shaders without rebuilds |
| GPU "printf" buffer | MoltenVK | auto with override dir, or `MVK_DEBUG_BUFFER=1` | `C:\gfx-dbg.bin` + `build/dbgcap/decode.py` | captured ~60 values/pixel in one run → S1 |
| Per-shader safe math | MoltenVK | `MVK_SAFE_MATH_SHADERS=hash,...` | — | ruled out precision as the hair cause |
| vkd3d diagnostics bundle | vkd3d | `VKD3D_DIAGNOSTICS=1` | failure log: fence stalls, queue activity, backbuffer-draw logger, event ring, GPU readback | Requiem black-screen investigation (N§29) |
| PSO retry tracking | vkd3d | default | failure log `pso-retry` | showed rejected PSO blobs are re-created (not a bug) |
| DXGI trace | DXVK | `DXVK_DXGI_TRACE=1` or file `C:\gfx-dxgi.trace` | `C:\gfx-dxgi.log` | found the 882x550 mode selection (X1) |
| Local MSL compile check | tool | scratchpad `mslcheck` | — | validates instrumented shaders before a game run |
| Probes | tools | `build/probe/*` | — | allocation, formats, swapchains, DirectStorage, upscalers, hangs, NaN, PSO stress, display |

---

## 5. Experiments that did not ship (and why)

| Experiment | Result | Status |
|---|---|---|
| MTLBinaryArchive persistent pipeline cache (N§31) | Premise wrong (Metal's cache works under Wine); deadlocked Pragmata, MDB_MAP_FULL, load hang | OFF (`MVK_BINARY_ARCHIVE=1`); candidate for removal |
| Metal private "OpenGL mode" for primitive-restart disable | Breaks depth clipping (z range) | OFF (`MVK_METAL_OPENGL_MODE=1`) |
| Private hardware sample mask | SV_Coverage lacked the mask | not used |
| `VKD3D_DISPATCH_BARRIER=1` | Did not help the black screen | OFF |
| `MVK_CONFIG_SHOULD_MAXIMIZE_CONCURRENT_COMPILATION`, `PREFILL_METAL_COMMAND_BUFFERS` | No FPS gain / slower | not adopted |
| GPU vendor spoof (NVIDIA / AMD) for Requiem | Crash / no change | removed |
| Non-UMA reporting (`VKD3D_REPORT_NON_UMA=1`) | Game never queried it | OFF; candidate for removal |
| Precise math for hair shaders | No change (cause was S1) | tool only |

---

## 6. Dev-only shortcuts to resolve before packaging

| Item | Why it exists | Action |
|---|---|---|
| `VKD3D_RELAX_CAPS=1` | Hides remaining gaps: transform feedback, single-texel buffer alignment; also gates TiledResourcesTier 2 (V1) | Make the V1/FL behaviour unconditional; keep only honest caps (N§21 rule) |
| `DXVK_RELAX_CAPS=1` | DXVK refuses MoltenVK without geometry shaders | Accept for dxgi-only use (vkd3d path) without an env var |
| PSO mismatch log `C:\gfx-pso-mismatch.log` | Investigation | Done: dev-only patch, off by default (`VKD3D_DIAGNOSTICS=1`) |
| DXGI trace trigger file `C:\gfx-dxgi.trace` | Investigation | Done: dev bottle deleted; the trace is a dev-only patch, opt-in |
| Debug buffer auto-enabled by `MVK_MSL_OVERRIDE_DIR` | Convenience | Fine for dev; no effect in a package without overrides |
| Launcher profiling vars (GPU profile, shader dumps, pass probe, override dir, frame stats) | Profiling | Done: dev launcher deleted; `launch-steam.sh` will set none of them |
| Dev test edits in vkd3d tests (MESH_ONLY_CULL, MESH_CULL_NO_PSO; test_logic_op_rendering added) | Testing | Revert dev edits; keep the new test for upstreaming |
| `patches/*.patch` | Last regenerated Nov 16 00:16 | Regenerate as a topic-split series (§9) |

---

## 7. Verification baseline

- vkd3d-proton test suite: full sweeps (597 tests) after each hardening step, 0 regressions attributable to our changes;
  latest spot checks after S1/S3/D1: atomic_instructions 316/316, interlocked 3/3, shader_instructions 5347 (0 fail),
  fp16 rounding 16.7M (0 fail), 64-bit atomics 79/85 (known 6).
- Known pre-existing failures: test_dispatch_huge_groups (4), execute-indirect state (no DGC), pipeline-statistics
  counts (emulated zeros), tiling limits of the reserved-resource fallback.
- Games: Requiem — boots, correct image, correct resolution, hair correct (user-confirmed). Pragmata — gameplay
  reached and profiled; hair after S1 not yet seen.

---

## 8. Open issues

| Issue | What we know | Next step |
|---|---|---|
| ~~Pragmata won't start from Steam~~ | Resolved: runs from Steam in a fresh bottle set up by the scripts. The old dev bottle stalled in Steam client/network init before any graphics DLL loaded; cause was that bottle's state (not Denuvo, not our stack) | — |
| Residual stutter | Game re-creates ~48k PSOs per launch; deferred compiles help | Measure "draws waited on PSO"; move remaining library compiles off game threads |
| 64-bit exchange under contention | 6/85 test cases; no game impact seen | Lock table if a game needs it |
| Unimplementable on Metal | Transform feedback, geometry shaders, per-shader FP32 denorm mode, ROVs/conservative raster (FL 12_1), DGC (ExecuteIndirect state) | Report honestly; document |

---

## 9. Patch series

The changes are split into one patch per fix: 58 shipping patches (spirv-cross 20, moltenvk 28, vkd3d-proton 7,
dxil-spirv 1, dxvk 2) and 24 dev patches for diagnostics and experiments (moltenvk 10, vkd3d-proton 13, dxvk 1).
The full index, with each patch mapped to the IDs in §3, is [`patches/README.md`](patches/README.md).

Verified: on fresh checkouts of the `versions.lock` commits, the shipping series applies and builds at every commit,
and shipping + dev reproduces the original working tree exactly.

Ordering differs from a strict §3 order where dependencies require it: spirv-cross scans (0004) before 64-bit wave ops
(0005); vkd3d-proton's temporary `VKD3D_RELAX_CAPS` first, since the tiled-resource fallback reports tier 2 through it.

Shipping = numbered patches only; `patches/dev/` stays in developer builds. Bottle setup (DXVK d3d11+dxgi, per-app
overrides, launcher without profiling vars) is done by the scripts.
