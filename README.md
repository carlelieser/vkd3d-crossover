# vkd3d-crossover

**Run D3D12 games that D3DMetal can't on M1 Macs.** An open-source D3D12 path for CrossOver —
vkd3d-proton → MoltenVK → Metal — with the emulations and fixes that make it work on Apple Silicon,
including 64-bit atomics that M1 lacks in hardware.

Verified with **Resident Evil Requiem** and **PRAGMATA SKETCHBOOK** (RE Engine) on an M1 Max.

> Status: working, pre-release. Scripts and prebuilt releases are in progress; see [Roadmap](#roadmap).

---

## Why this exists

### What CrossOver ships today

| Piece | Job | In CrossOver |
|---|---|---|
| **DXVK** | D3D9/10/11 → Vulkan, plus `dxgi.dll` | ✅ for D3D9–11, but **no `dxgi.dll`** and **no D3D12** |
| **MoltenVK** | Vulkan → Metal | ✅ version 1.2 (Vulkan 1.2) |
| **vkd3d-proton** | **D3D12 → Vulkan** | ❌ needs Vulkan 1.3 |
| **D3DMetal** (Apple, closed source) | D3D12/11 → Metal directly | ✅ CrossOver's D3D12 path |

So on CrossOver, **D3D12 means D3DMetal**. D3DMetal is good, but modern games built on Shader Model 6.6 use
**64-bit atomic operations**, which D3DMetal maps to hardware. **M1 (Apple GPU family 7) has none; M2 and later do.**
Those games simply don't run on M1 under D3DMetal — Resident Evil Requiem is one of them.

### What this project adds

A second, fully open-source D3D12 path that runs on M1:

```
Game (D3D12 / D3D11 / DXGI)
  ├─ d3d12.dll + d3d12core.dll   vkd3d-proton (D3D12 → Vulkan; shaders: DXIL → SPIR-V via dxil-spirv)
  ├─ dxgi.dll  + d3d11.dll       DXVK          (windows, swapchains, display modes; D3D11 for video)
  └─ Vulkan
        └─ MoltenVK 1.4 (Vulkan → Metal; shaders: SPIR-V → MSL via SPIRV-Cross)
              └─ Metal
```

The M1 gap is closed in software: 64-bit atomics are **emulated** with 32-bit atomics in the shader translator,
along with the other Shader Model 6.6 features and mesh shaders Metal exposes differently.

### Why not just use upstream vkd3d-proton?

You can try. On a Mac it fails at every layer, in this order:

1. **It won't start.** vkd3d-proton needs Vulkan 1.3; CrossOver's MoltenVK is 1.2. Replacing it means editing
   CrossOver's app bundle (Wine loads MoltenVK from inside the app; macOS blocks loading it from elsewhere).
2. **It rejects the device.** Even stock MoltenVK 1.4 lacks features vkd3d-proton treats as mandatory
   (robust buffer access, null descriptors, transform feedback, texel-buffer alignment).
3. **It can't present.** vkd3d-proton needs DXVK's `dxgi.dll`, which CrossOver doesn't ship. RE Engine also
   needs DXVK's `d3d11.dll` next to it, or Requiem hangs on a black screen after the intro.
4. **RE games die at startup.** Stock SPIRV-Cross refuses any 64-bit atomic (the M1 problem itself), 64-bit wave
   operations fail in Metal (`Fatal D3D error (1, E_FAIL)`), and without tiled resources the game takes a fallback
   path that crashes at the first frame.
5. **Then it renders wrong.** Each of these needed its own fix: a black 3D world, feature level capped at 11_0,
   882x550 render resolution, blown-out hair, 73 render passes per frame, multi-second compile freezes.

**Most of the work isn't in vkd3d-proton** — about four fifths of the ~7,700 changed lines are in MoltenVK and the
shader translators (SPIRV-Cross, dxil-spirv), plus the setup knowledge of which DLLs to pair and how.

### Who should use it

| You have | Recommendation |
|---|---|
| **M1 Mac**, D3D12 game that fails under D3DMetal | **This project.** |
| M2 or later | Try D3DMetal first; this is an alternative if a game misbehaves there. |
| D3D11 or older game | CrossOver's built-in DXVK / D3DMetal already cover it. |

---

## What's in it

Every change is documented with its symptom, root cause, fix, verification and off switch in
[OVERVIEW.md](OVERVIEW.md); the investigation history is in [NOTES.md](NOTES.md). Highlights by layer:

**Shader translation (SPIRV-Cross, dxil-spirv)**
- 64-bit atomics on M1: emulated on 32-bit halves; atomic max/min with a per-element lock so depth/ID pairs never tear.
- Shader Model 6.5/6.6: 64-bit wave ops, subgroup scans, compute-shader derivatives, sparse feedback.
- Robust buffer access (out-of-range reads return 0, writes are dropped) — D3D12 games rely on it; without it
  writes land in other resources.
- Correctness fixes found in RE Engine: compare-and-swap loops re-reading memory (blown-out hair),
  D3D's round-toward-zero `f32tof16`, groupshared access chains, interface type mismatches, tessellation varyings.

**MoltenVK**
- Vulkan 1.3 plus the features vkd3d-proton needs; mesh shaders on Metal 3 object/mesh stages.
- `VK_EXT_dynamic_rendering_unused_attachments`, real logic ops, pipeline-statistics queries, HDR blending fix.
- Recovery instead of permanent device loss when Metal's 0.5 s watchdog kills a command buffer.
- Performance: indirect-count draws no longer split render passes (+25% fps), persistent shader cache,
  deferred pipeline compilation to cut stutter.

**vkd3d-proton**
- Tiled resources emulated (feature level 12_0 instead of 11_0), Present reports device removal.

**DXVK**
- `dxgi.dll` and `d3d11.dll` for this path; a Windows-like display mode list (Wine's 1,248 synthetic modes made
  RE Engine pick 882x550).

---

## Status

| | Resident Evil Requiem | PRAGMATA SKETCHBOOK |
|---|---|---|
| Boots, menus, gameplay | ✅ | ✅ |
| Correct image | ✅ | ✅ |
| Correct render resolution | ✅ | ✅ |
| Hair | ✅ | expected fixed (same code path), not yet confirmed |
| Performance | playable | 15–20+ fps in heavy scenes, some compile stutter |

Other D3D12 games: **may work, untested.** Most fixes are generic, but only RE Engine has been verified end to end.

**Not possible on Metal** (reported honestly to games): transform feedback, geometry shaders, per-shader FP32
denormal modes, rasterizer-ordered views / conservative rasterization (feature level 12_1),
device-generated commands (some ExecuteIndirect state).

---

## How it installs

### Quick start
Prebuilt releases are coming; for now build first (see [Building from source](#building-from-source)), then:

```sh
scripts/install.sh                                         # clone CrossOver, install our MoltenVK
scripts/setup-bottle.sh Steam-D3D12 --create --install-steam  # new bottle + DLLs + per-game overrides + Steam
scripts/launch-steam.sh Steam-D3D12                        # start Steam (launch games from this window)
```

Then sign in to Steam and add your library folders (Steam → Settings → Storage; your home folder is drive `Y:`).
Start games **from the Steam started by `launch-steam.sh`**, not from the CrossOver app — CrossOver's own MoltenVK
is too old for vkd3d-proton. Enable another game with `scripts/setup-bottle.sh Steam-D3D12 --game Game.exe`.
Re-run `scripts/install.sh --force` after updating CrossOver.

### What the scripts change

Nothing in CrossOver or your games is modified in place:

- A local APFS clone of your CrossOver app (`CrossOver (D3D12).app`, near-zero disk space) gets our MoltenVK.
  Your regular CrossOver stays untouched, and CrossOver updates don't break it (re-run the installer after updating).
- The bottle gets our `d3d12`, `d3d12core`, `dxgi` and `d3d11` DLLs, enabled **per game** through Wine DLL
  overrides — Steam and everything else keep their usual DLLs.
- **Rule:** `d3d11` and `dxgi` always come from DXVK together. Mixing DXVK's `dxgi` with D3DMetal's `d3d11`
  crashes RE Engine.
- **PlayStation controllers** (DualShock 4, DualSense) are also presented as XInput gamepads (Wine `winebus`
  `DisableHidraw=1`). By default Wine exposes them only as raw HID devices, which Steam and native-PlayStation games
  use, but XInput-only games — most modern ones, including non-Steam games — then see no controller. Trade-off:
  games may show Xbox button icons. `--native-ps-controllers` keeps Wine's default.

### Requirements
- Apple Silicon Mac (built and tested on M1 Max), macOS 15+
- CrossOver (a Windows bottle with Steam)
- For building from source: Xcode, MinGW-w64, Meson/Ninja

### Building from source

```sh
tools/dev/fetch.sh           # upstream sources at the versions.lock commits (shallow), into src/
tools/dev/apply-patches.sh   # pinned commit + shipping patches   (--dev adds diagnostics, --force discards local edits)
tools/dev/build.sh           # MoltenVK + vkd3d-proton + DXVK  ->  out/release/ (or out/dev/), with BUILDINFO + SHA256SUMS
```

Output: `libMoltenVK.dylib`, `d3d12.dll`, `d3d12core.dll`, `dxgi.dll`, `d3d11.dll`. A release build fails if dev
diagnostics leak into MoltenVK.

---

## Repository layout

```
vkd3d-crossover/
├─ README.md          this file
├─ OVERVIEW.md        every change: symptom → cause → fix → verification → off switch
├─ NOTES.md           chronological investigation notes
├─ versions.lock      pinned upstream commits
├─ patches/
│  ├─ README.md     index: every patch, what it fixes, OVERVIEW ID
│  ├─ spirv-cross/  moltenvk/  vkd3d-proton/  dxil-spirv/  dxvk/   one patch per fix, numbered
│  └─ dev/                                                         diagnostics, never in release builds
├─ scripts/
│  ├─ install.sh         clone CrossOver, install MoltenVK                      ✅
│  ├─ setup-bottle.sh    bottle + DLLs + per-game overrides (--game), controllers, Steam  ✅
│  ├─ launch-steam.sh    start a bottle's Steam through the cloned app          ✅
│  ├─ fix-game-config.sh one-time reset of RE Engine's saved display mode       (planned)
│  ├─ doctor.sh          check the setup                                        (planned)
│  └─ uninstall.sh       remove the clone, restore the bottle                   (planned)
├─ tools/dev/         developer scripts: fetch.sh, apply-patches.sh, build.sh, test harness, probes
└─ .github/workflows/ build and publish releases
```

---

## Roadmap

1. ~~Split the work into topic patches (`patches/`), pin upstream versions.~~ Done: 58 shipping + 24 dev patches.
2. Scripts: ~~install, bottle setup, Steam launcher, build-from-source~~ (done, tested on a fresh bottle); doctor, uninstall, game-config fixup.
3. Prebuilt releases (GitHub Actions).
4. Remove the dev-only capability bypasses (report only what is real).
5. **Upstream the generic fixes** to SPIRV-Cross, MoltenVK, dxil-spirv, vkd3d-proton and DXVK. The more lands
   upstream, the more this project shrinks to an installer — which is the goal.

---

## Licenses

This repository contains patches and scripts. Built releases contain binaries of:
MoltenVK and SPIRV-Cross (Apache-2.0), vkd3d-proton (LGPL-2.1), dxil-spirv (MIT), DXVK (zlib/libpng).
Source for every binary = the pinned upstream commit + the patches here. CrossOver is not included or
redistributed; the installer works on your own copy.
