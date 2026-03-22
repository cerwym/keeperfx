# Renderer Abstraction Layer

Describes the design and data-flow of the renderer abstraction introduced for
the Vita port, covering the `IRenderer` / `IPostProcessPass` interfaces, the
`RendererManager` C shim, and the Vita-specific GPU lens-effect pipeline.

---

## 1. Overview

The game's original rendering path wrote 8-bit paletted pixels directly into a
software framebuffer and blitted it to the display.  The abstraction layer
wraps that framebuffer handshake behind a thin C++ interface, letting each
platform provide its own backend:

| Backend | `RendererType` | Platform |
|---------|---------------|----------|
| `RendererSoftware` | `RENDERER_SOFTWARE` | Windows, Linux — SDL2 blit |
| `RendererOpenGL`   | `RENDERER_OPENGL`   | Desktop — GL texture upload |
| `RendererVita`     | `RENDERER_VITA`     | PS Vita — vitaGL palette shader |
| `Renderer3DS`      | `RENDERER_3DS`      | Nintendo 3DS — citro3d |

The active backend is selected at startup (or switched at runtime on supported
platforms) via `RendererManager`.

---

## 2. Core Interfaces

### 2.1 `IRenderer` (`src/renderer/IRenderer.h`)

Pure abstract base class.  All backends derive from it.

```
IRenderer
 ├── Init()              — allocate GPU context, textures, shaders
 ├── Shutdown()          — free all GPU resources
 ├── BeginFrame()        — called each frame before rendering; returns false to skip
 ├── EndFrame()          — present the completed frame to the display
 ├── LockFramebuffer()   — return a writable 8-bit paletted CPU buffer
 ├── UnlockFramebuffer() — release the CPU buffer after writes
 ├── GetName()           — human-readable backend name string
 ├── SupportsRuntimeSwitch() — can this backend be hot-swapped?
 └── SupportsGPUPasses() — can this backend run IPostProcessPass lens effects?
                           (default: false; RendererVita returns true)
```

The framebuffer contract: the software rasteriser always writes into the 8-bit
buffer returned by `LockFramebuffer()`.  The backend is then free to upload
those bytes to GPU memory, blit them via SDL, or do anything else during
`EndFrame()`.

### 2.2 `IPostProcessPass` (`src/renderer/IPostProcessPass.h`)

Single GPU post-processing pass interface.  All four Vita lens effects
implement it.  Platform-neutral by design: handle types are `unsigned int`
(same wire size as `GLuint`) so no GL header is needed in this file.

```
IPostProcessPass
 ├── Init()   — allocate GPU resources (shaders, textures, uniforms)
 ├── Apply(src_tex, dst_fbo, src_w, src_h)
 │            — execute the pass: read src_tex, write to dst_fbo (0 = screen)
 └── Free()   — release GPU resources; safe to call before Init()
```

Life cycle is driven by the owning `LensEffect` subclass:
`Init()` is called inside `LensEffect::Setup()`, `Free()` inside
`LensEffect::Cleanup()`.

### 2.3 `RendererManager` (`src/renderer/RendererManager.h`)

Thin C-callable shim that manages the active `IRenderer*` instance.  The rest
of the codebase (including C translation units such as `bflib_video.c`)
interacts with the renderer exclusively through this module.

```c
/* C / C++ API */
int            RendererInit(RendererType type);
int            RendererSwitch(RendererType type);
void           RendererShutdown(void);
RendererType   RendererGetActiveType(void);

unsigned char* RendererLockFramebuffer(int* out_pitch);
void           RendererUnlockFramebuffer(void);
int            RendererBeginFrame(void);
void           RendererEndFrame(void);

/* C++ only */
IRenderer*     RendererGetActive();
```

---

## 3. Vita Backend (`RendererVita`)

### 3.1 Frame pipeline

```
CPU framebuffer (8-bit indexed, 640×480, lbDrawSurface)
        │  LockFramebuffer / UnlockFramebuffer
        ▼
  m_index_tex (GL_LUMINANCE 640×480)   ◄─ glTexSubImage2D each frame
  m_palette_tex (GL_RGBA 256×1)        ◄─ lbPalette expanded to RGBA8 each frame

        │  Stage 1 — VitaBlitShader
        │  palette-decode: index + palette → RGBA scene
        ▼
  m_scene_fbo / m_scene_tex (GL_RGBA 640×480)
        │
        │  Stage 2 — GPU lens passes (ping-pong, zero or more)
        │  each IPostProcessPass reads src_tex → writes dst_fbo
        ▼
  m_pass_fbo_a/b / m_pass_tex_a/b (GL_RGBA 640×480, alternating)
        │
        │  Stage 3 — VitaPassthroughPass
        │  upscale / stretch 640×480 → 960×544 screen
        ▼
  default framebuffer (GXM display, 960×544)
        │
        └─ vglSwapBuffers()
```

If no GPU lens passes are active, Stage 1 renders directly to the default
framebuffer and Stages 2–3 are skipped entirely.

### 3.2 GPU resources

| Member | Type | Purpose |
|--------|------|---------|
| `m_index_tex`   | `GL_LUMINANCE 640×480` | 8-bit palette indices from CPU |
| `m_palette_tex` | `GL_RGBA 256×1`        | Expanded palette (lbPalette × 4) |
| `m_scene_fbo/tex` | `GL_RGBA 640×480`    | RGBA-decoded scene (ping-pong source) |
| `m_pass_fbo_a/b`  | `GL_RGBA 640×480`  | Ping-pong render targets for GPU passes |

### 3.3 GLSL compilation

vitaGL is built with `HAVE_GLSL_SUPPORT=1`, which routes
`glShaderSource / glCompileShader` through `SceShaccCg` on-device.  No
offline shader compilation with `psp2cgc.exe` is needed.  All pass shaders are
embedded as `const char*` strings compiled in `Init()`.

The shared helpers live in `src/renderer/vita/VitaPassCommon.h`:

| Helper | Purpose |
|--------|---------|
| `k_pp_pos[4][2]` | Clip-space quad positions (TL TR BL BR) |
| `k_pp_uv[4][2]`  | UV coords matching positions |
| `k_pp_vert_glsl` | Shared vertex shader: `aPos` → `gl_Position`, `aUV` → `vUV` |
| `vita_compile_shader(type, src)` | Compile one GLSL stage; log errors |
| `vita_link_program(vert, frag)` | Bind `aPos`=0 / `aUV`=1, link, delete shaders |
| `vita_build_pass_program(frag_glsl)` | Compile shared vert + custom frag in one call |
| `vita_draw_fullscreen_quad()` | `vglVertexAttribPointer` + `vglDrawObjects(TRIANGLE_STRIP, 4)` |

---

## 4. GPU Lens Effects

### 4.1 How a lens effect exposes a GPU pass

Each `LensEffect` subclass has an optional `GetGPUPass()` override.  On
non-Vita builds it inherits the default (returns `nullptr`).  On Vita each
subclass owns a concrete `VitaXxxPass` member, guarded by `#ifdef PLATFORM_VITA`:

```cpp
// In MistEffect.h (pattern repeated for all four effects)
#ifdef PLATFORM_VITA
    virtual IPostProcessPass* GetGPUPass() override {
        return m_gpu_pass.IsInitialized() ? &m_gpu_pass : nullptr;
    }
private:
    VitaMistPass m_gpu_pass;
#endif
```

`RendererVita::EndFrame()` queries `LensManager::GetEffects()` and collects
every non-null `GetGPUPass()` pointer into `gpu_passes` before executing the
ping-pong loop.  If a pass's `Init()` failed (e.g. shader compile error) it
returns `nullptr` and is silently skipped; the CPU `Draw()` path in
`LensManager` remains available as a fallback.

### 4.2 Implemented passes

| Class | File | Effect |
|-------|------|--------|
| `VitaMistPass`        | `src/renderer/vita/VitaMistPass.cpp`        | Two-layer animated fog |
| `VitaDisplacePass`    | `src/renderer/vita/VitaDisplacePass.cpp`    | Warp (linear / sinusoidal / radial) |
| `VitaFlyeyePass`      | `src/renderer/vita/VitaFlyeyePass.cpp`      | Hex-grid compound-eye |
| `VitaOverlayPass`     | `src/renderer/vita/VitaOverlayPass.cpp`     | Palette-indexed overlay sprite |
| `VitaPassthroughPass` | `src/renderer/vita/VitaPassthroughPass.cpp` | Stage-3 final blit to 960×544 (owned by RendererVita, not a LensEffect) |

### 4.3 VitaMistPass

`Configure(data, pos_x_step, pos_y_step, sec_x_step, sec_y_step)` is called
from `MistEffect::Setup()` while `eye_lens_memory` (the 256×256 mist texture)
is valid.  The pass uploads it as a `GL_LUMINANCE` texture in `Init()` and
maintains its own per-frame animation state (byte-wrap counters), advancing
them in each `Apply()` call regardless of whether the CPU path also runs.

Fragment shader UV formula (mirrors the CPU CMistFade implementation):
```glsl
vec2 uv1 = fract(uPos + vec2(vUV.x * 2.5, vUV.y * 1.875));
vec2 uv2 = fract(vec2(uSec.x - vUV.y * 1.875, uSec.y - vUV.x * 2.5));
float fog = clamp((k + i) * 255.0 / 256.0, 0.0, 1.0);
gl_FragColor = vec4(scene.rgb * (1.0 - fog), scene.a);
```
Scale factors: `2.5 = 640/256`, `1.875 = 480/256`.

### 4.4 VitaDisplacePass

Three separate fragment shaders (one per `DisplacementAlgorithm` value) are
compiled — the one matching `Configure(algo, magnitude, period)` is selected
at `Init()` time, keeping the fragment shader branch-free.

The `DisplacementAlgorithm` enum value is passed as `int` to avoid a
circular header dependency (`DisplacementEffect.h` → `VitaDisplacePass.h` →
`DisplacementEffect.h`).

### 4.5 VitaFlyeyePass

No `Configure()` needed — all constants are derived from the hardcoded 640×480
reference resolution and the original `ldpar1 = 640 × 0.0175 = 11.2` offset.

### 4.6 VitaOverlayPass

The overlay image is uploaded once as `GL_LUMINANCE` (8-bit palette indices).
A 256×1 `GL_RGBA` palette texture (`m_palette_tex`) is rebuilt from
`lbPalette` on every `Apply()` call because the palette changes each frame
(lightning, spell flashes, fades).  Palette index 255 is the transparent key
colour — the fragment shader passes the scene pixel through unmodified.

```glsl
float idx = texture2D(uOverlay, vUV).r;
if (idx > (254.5 / 255.0)) { gl_FragColor = scene; return; }
vec4 col = texture2D(uPalette, vec2(idx + 0.5 / 256.0, 0.5));
gl_FragColor = mix(scene, col, uAlpha);
```

---

## 5. Adding a New Backend

1. Derive a class from `IRenderer` in `src/renderer/`.
2. Add a `RENDERER_XXX` value to the `RendererType` enum in `IRenderer.h` and
   the corresponding C `#define` in `RendererManager.h`.
3. Register the backend in `RendererManager.cpp` (the factory switch).
4. Add the new `.cpp` to `CMakeLists.txt` under the appropriate platform guard
   (or rely on the existing `GLOB_RECURSE src/*.cpp` if it covers the directory).

## 6. Adding a New GPU Lens Pass

1. Create `src/renderer/vita/VitaXxxPass.h/.cpp` implementing `IPostProcessPass`.
2. Add `#ifdef PLATFORM_VITA VitaXxxPass m_gpu_pass; GetGPUPass() override #endif`
   to the relevant `LensEffect` subclass header.
3. Call `m_gpu_pass.Configure(...); m_gpu_pass.Init();` in `LensEffect::Setup()`.
4. Call `m_gpu_pass.Free();` in `LensEffect::Cleanup()`.

The ping-pong loop in `RendererVita::EndFrame()` picks up the pass automatically
through `GetGPUPass()` — no changes to `RendererVita` are needed.
