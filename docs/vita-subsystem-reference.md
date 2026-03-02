# Vita Subsystem Reference

Comparison of **vitaQuake1**, **vitaQuake2**, and **KeeperFX** across four PS Vita
platform subsystems. Source paths used:

| Codebase    | Root |
|-------------|------|
| vitaQuake1  | `vitaQuake/source/` |
| vitaQuake2  | `vitaQuakeII/psp2/` and `vitaQuakeII/ref_gl/` |
| KeeperFX    | `src/` (this repo) |

---

## 1. Graphics

### 1.1 vitaGL Init Sequence

All three codebases call the vitaGL init helpers in a similar order, but with
different parameter values.

| Call | vitaQuake1 | vitaQuake2 | KeeperFX |
|------|-----------|-----------|---------|
| `vglSetVertexPoolSize` | `64 MB` | *(not called)* | `64 MB` |
| `vglSetVDMBufferSize`  | *(not called)* | `1 MB` | `1 MB` |
| `vglInitExtended` arg 1 (GXM cmd buf) | `0x1400000` (20 MB) | `0` | `0x1400000` (20 MB) |
| `vglInitExtended` arg 2/3 (w×h) | `scr_width, scr_height` | `scr_width, scr_height` | `960, 544` (fixed) |
| `vglInitExtended` arg 4 (RAM pool) | `0x1000000` (16 MB) | `0x1000000` (16 MB) | `0x1000000` (16 MB) |
| `vglInitExtended` arg 5 (MSAA) | `SCE_GXM_MULTISAMPLE_NONE/2X/4X` | `SCE_GXM_MULTISAMPLE_NONE/2X/4X` | `SCE_GXM_MULTISAMPLE_NONE` |
| `vglUseVram` | *(not called — uses RAM)* | `GL_TRUE` | `GL_TRUE` |

#### vitaQuake1 — `source/sys_psp2.c:495–511`
```c
vglSetVertexPoolSize(64 * 1024 * 1024);
// antialiasing case switch:
invalid_res = vglInitExtended(0x1400000, scr_width, scr_height, 0x1000000,
                               SCE_GXM_MULTISAMPLE_NONE /* or 2X / 4X */);
// No vglSetVDMBufferSize, no vglUseVram
```

#### vitaQuake2 — `psp2/glimp_psp2.c:118–130`
```c
vglSetVDMBufferSize(1024 * 1024);
// msaa case switch:
vglInitExtended(0, scr_width, scr_height, 0x1000000,
                SCE_GXM_MULTISAMPLE_NONE /* or 2X / 4X */);
vglUseVram(GL_TRUE);
// No vglSetVertexPoolSize
```

#### KeeperFX — `src/renderer/RendererVita.cpp:61–72`
```c
vglSetVertexPoolSize(64 * 1024 * 1024);   // same as vitaQuake1
vglSetVDMBufferSize(1024 * 1024);          // same as vitaQuake2
vglInitExtended(0x1400000, 960, 544, 0x1000000, SCE_GXM_MULTISAMPLE_NONE);
vglUseVram(GL_TRUE);                       // same as vitaQuake2
```

**KeeperFX notes:**
- Combines the best settings from both Quake ports.
- Resolution is fixed at 960×544; no in-game resolution selection is
  implemented — the game's native 640×480 framebuffer is scaled up by the
  palette-lookup shader.
- `vglInitExtended` is called from `vita_vitagl_preinit()` *before* SDL_Init,
  because vitaGL must own the GXM context before SDL2 initialises the display.

---

### 1.2 Shader Loading

All three codebases use **pre-compiled `.gxp` binaries** produced by `psp2cgc` at
build time and loaded at runtime via `glShaderBinary`. None use `glShaderSource`
or inline Cg text.

| Aspect | vitaQuake1 | vitaQuake2 | KeeperFX |
|--------|-----------|-----------|---------|
| API | `glShaderBinary(1, &sh, 0, buf, sz)` | `glShaderBinary(1, &sh, 0, buf, sz)` | `glShaderBinary(1, &sh, 0, buf, sz)` |
| Load path | `app0:shaders/*.gxp` | `app0:shaders/*.gxp` | `app0:shaders/vita_blit_{v,f}.gxp` |
| Shader count | 9 fragment + 4 vertex | 9 fragment + 4 vertex + optional postFX | 1 vertex + 1 fragment |
| Purpose | Quake world/UI rendering | Quake world/UI + postFX | Palette-indexed blit to 960×544 |
| Attribute binding | `vglBindAttribLocation()` | `vglBindAttribLocation()` | `glBindAttribLocation()` |

#### vitaQuake1 — `source/gl_vidpsp2.c:202–214`
```c
void* GL_LoadShader(const char* filename, GLuint idx, GLboolean fragment) {
    FILE* f = fopen(filename, "rb");
    // ... fread into res ...
    if (fragment)
        glShaderBinary(1, &fs[idx], 0, res, size);
    else
        glShaderBinary(1, &vs[idx], 0, res, size);
    free(res);
}
// Called as e.g.: GL_LoadShader("app0:shaders/replace_f.gxp", REPLACE, GL_TRUE);
```

#### vitaQuake2 — `psp2/glimp_psp2.c:151–162`
```c
void GL_LoadShader(const char* filename, GLuint idx, GLboolean fragment){
    FILE* f = fopen(filename, "rb");
    // ... fread into res ...
    if (fragment) glShaderBinary(1, &fs[idx], 0, res, size);
    else          glShaderBinary(1, &vs[idx], 0, res, size);
    free(res);
}
```

#### KeeperFX — `src/renderer/RendererVita.cpp:83–101`
```c
static GLuint load_shader_binary(GLenum type, const char* path)
{
    GLuint sh = glCreateShader(type);
    FILE* f = fopen(path, "rb");
    // ... fread into buf ...
    glShaderBinary(1, &sh, 0, buf, (GLsizei)sz);
    free(buf);
    return sh;
}
// Usage:
m_vert_shader = load_shader_binary(GL_VERTEX_SHADER,   "app0:shaders/vita_blit_v.gxp");
m_frag_shader = load_shader_binary(GL_FRAGMENT_SHADER, "app0:shaders/vita_blit_f.gxp");
```

---

### 1.3 Texture Upload Pattern

| Aspect | vitaQuake1 | vitaQuake2 | KeeperFX |
|--------|-----------|-----------|---------|
| Main texture format | `GL_RGBA / GL_UNSIGNED_BYTE` | `GL_RGBA / GL_UNSIGNED_BYTE` | `GL_LUMINANCE / GL_UNSIGNED_BYTE` (index) + `GL_RGBA / GL_UNSIGNED_BYTE` (palette) |
| Typical call site | `gl_draw.cpp`, `gl_rmisc.c`, `gl_warp.c` | `ref_gl/gl_image.c`, `gl_draw.c` | `src/renderer/RendererVita.cpp:137–148` |
| Per-frame update | `glTexImage2D` for SSAA FB | `glTexImage2D` for postFX FB | `glTexSubImage2D` every frame (both index + palette) |
| SSAA / postFX FB | `glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, scr_w, scr_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, buf)` | `glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, scr_w, scr_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL)` | N/A |

#### KeeperFX texture setup — `src/renderer/RendererVita.cpp:131–149`
```c
// 8-bit index texture (one byte per pixel — the palette index)
glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, k_gameW, k_gameH, 0,
             GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL);

// 256×1 RGBA palette texture (resolved in the fragment shader)
glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 1, 0,
             GL_RGBA, GL_UNSIGNED_BYTE, NULL);
```

Every frame, both are updated with `glTexSubImage2D` (no re-allocation).

---

### 1.4 Framebuffer / Swap Pattern

| Aspect | vitaQuake1 | vitaQuake2 | KeeperFX |
|--------|-----------|-----------|---------|
| Swap call | `vglSwapBuffers(isKeyboard \|\| netcheck_dialog_running)` | `vglSwapBuffers(isKeyboard)` | `vglSwapBuffers(GL_FALSE)` |
| Blocking? | Blocks when OSK/netcheck dialog is open | Blocks when OSK is open | Never blocks (GL_FALSE) |
| SSAA blit | Yes — renders FB texture quad to 960×544 after swap condition | No (postFX replaces SSAA) | N/A |
| PostFX blit | No | Yes — renders main FB texture with Cg postFX shader | N/A |

#### vitaQuake1 — `source/gl_vidpsp2.c:544`
```c
vglSwapBuffers(isKeyboard || netcheck_dialog_running);
```

#### vitaQuake2 — `psp2/glimp_psp2.c:460`
```c
vglSwapBuffers(isKeyboard);
```

#### KeeperFX — `src/renderer/RendererVita.cpp:272`
```c
vglSwapBuffers(GL_FALSE);   // non-blocking; KeeperFX has no OSK or netcheck overlay
```

---

### 1.5 VRAM vs RAM Allocation Strategy

| Codebase | `vglUseVram` | Vertex pool | Game textures |
|----------|-------------|-------------|---------------|
| vitaQuake1 | Not called (default = CDRAM off) | 64 MB vertex pool in RAM | RGBA in RAM |
| vitaQuake2 | `GL_TRUE` | No explicit vertex pool | RGBA in VRAM |
| KeeperFX | `GL_TRUE` | 64 MB vertex pool | 8-bit index + 256-entry palette in VRAM |

KeeperFX uses the GPU palette-lookup path, which means the main game texture is
`k_gameW × k_gameH` bytes (8 bits/pixel) rather than 4 bytes/pixel. This cuts
VRAM usage for the framebuffer to 25% of an RGBA approach.

---

## 2. Audio

### 2.1 sceAudioOut Port Initialisation

| Parameter | vitaQuake1 | vitaQuake2 | KeeperFX (SFX) | KeeperFX (Music) | KeeperFX (FMV) |
|-----------|-----------|-----------|----------------|------------------|----------------|
| Port type | `SCE_AUDIO_OUT_PORT_TYPE_MAIN` | `SCE_AUDIO_OUT_PORT_TYPE_MAIN` | `SCE_AUDIO_OUT_PORT_TYPE_MAIN` | `SCE_AUDIO_OUT_PORT_TYPE_MAIN` | `SCE_AUDIO_OUT_PORT_TYPE_MAIN` |
| Sample count | `AUDIOSIZE/2` = **8192** | `BUFFER_SIZE/4` = **4096** | `VITA_DMA_GRAIN` = **1024** | `MUSIC_GRAIN` = **2048** | `FMV_DMA_GRAIN` = **2048** |
| Sample rate | **48000** Hz | **48000** Hz | **48000** Hz | **44100** Hz | 44100 or 48000 (auto) |
| Channel mode | `SCE_AUDIO_OUT_MODE_MONO` | `SCE_AUDIO_OUT_MODE_STEREO` | `SCE_AUDIO_OUT_MODE_STEREO` | `SCE_AUDIO_OUT_MODE_STEREO` | Stereo or Mono (dynamic) |
| Volume | L=32767, R=32767 | L=32767, R=32767 | Not set (default max) | Not set (default max) | Not set |

#### vitaQuake1 — `source/snd_psp2.c:39–42`
```c
chn = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_MAIN,
                          AUDIOSIZE / 2,   /* 8192 samples */
                          SAMPLE_RATE,     /* 48000 */
                          SCE_AUDIO_OUT_MODE_MONO);
sceAudioOutSetConfig(chn, -1, -1, -1);
int vol[] = {32767, 32767};
sceAudioOutSetVolume(chn, SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH, vol);
```

#### vitaQuake2 — `psp2/snddma_psp2.c:37–40`
```c
int chn = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_MAIN,
                              BUFFER_SIZE / 4,   /* 4096 samples */
                              SAMPLE_RATE,       /* 48000 */
                              SCE_AUDIO_OUT_MODE_STEREO);
sceAudioOutSetConfig(chn, -1, -1, -1);
int vol[] = {32767, 32767};
sceAudioOutSetVolume(chn, SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH, vol);
```

#### KeeperFX — `src/audio/audio_vita.c:477–484`
```c
s_dma_port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_MAIN,
                                  VITA_DMA_GRAIN,       /* 1024 samples */
                                  VITA_OUT_RATE,        /* 48000 */
                                  SCE_AUDIO_OUT_MODE_STEREO);
// No explicit volume set — default is hardware maximum
```

---

### 2.2 Audio Thread Configuration

| Parameter | vitaQuake1 | vitaQuake2 | KeeperFX (DMA/SFX) | KeeperFX (Music) | KeeperFX (FMV) |
|-----------|-----------|-----------|-------------------|------------------|----------------|
| Thread name | `"Audio Thread"` | `"Sound Thread"` | `"vita_dma"` | `"vita_music"` | `"vita_fmv_thread"` |
| Priority | `0x10000100` | `0x10000100` | `0x40` (user RT) | `0x60` (lower) | `0x40` |
| Stack size | `0x10000` (64 KB) | `0x800000` (8 MB!) | `64 * 1024` (64 KB) | `64 * 1024` (64 KB) | 16 KB (default) |
| Affinity | `0` (any core) | `0` (any core) | `0` (any core) | `0` (any core) | `0` |

#### vitaQuake1 — `source/snd_psp2.c:74`
```c
SceUID audiothread = sceKernelCreateThread("Audio Thread",
    (void*)&audio_thread, 0x10000100, 0x10000, 0, 0, NULL);
```

#### vitaQuake2 — `psp2/snddma_psp2.c:73`
```c
SceUID audiothread = sceKernelCreateThread("Sound Thread",
    (void*)&audio_thread, 0x10000100, 0x800000, 0, 0, NULL);
```
> **Note:** vitaQuake2 allocates an 8 MB stack for the audio thread — this is
> probably a bug/oversight; 64 KB is sufficient for a simple DMA loop.

#### KeeperFX — `src/audio/audio_vita.c:488–491`
```c
s_dma_thread = sceKernelCreateThread("vita_dma", vita_dma_thread,
                                      0x40,       /* user real-time priority */
                                      64 * 1024,  /* 64 KB stack */
                                      0, 0, NULL);
```

---

### 2.3 Mixer Architecture

| Aspect | vitaQuake1 | vitaQuake2 | KeeperFX |
|--------|-----------|-----------|---------|
| Channels | DMA ring, single mono voice | DMA ring, 2-channel stereo | 16 concurrent software-mixer voices (`VITA_MAX_CHANNELS`) |
| Algorithm | No mixing — DMA thread just loops `sceAudioOutOutput(chn, audiobuffer)` | Same — raw DMA buffer output | Full software mixer: accumulate S32 per-voice → clamp → S16 stereo |
| Synchronisation | DMA position estimated from wall clock (RTC ticks) | DMA position estimated from wall clock | DMA blocks on `sceAudioOutOutput` (inherently synchronised) |
| Resampling | None — source rate must match 48 kHz | None | Per-voice fractional step: `fstep = src_rate / VITA_OUT_RATE * pitch_scale` |
| Pan | No | No | Yes — `vol_l` / `vol_r` per channel, `pan 0..128` |
| Pitch | No | No | Yes — fractional position advance |

#### KeeperFX mixer loop — `src/audio/audio_vita.c:343–376`
```c
static int vita_dma_thread(SceSize args, void *argp)
{
    while (s_dma_running) {
        memset(s_mix_buf, 0, sizeof(s_mix_buf));  // 32-bit accumulator
        for (int c = 0; c < VITA_MAX_CHANNELS; c++) {
            SwChannel *ch = &s_sw[c];
            if (!ch->active) continue;
            for (int s = 0; s < VITA_DMA_GRAIN; s++) {
                uint32_t idx = (uint32_t)ch->fpos;
                if (idx >= ch->pcm_len) { /* loop or stop */ }
                int32_t smp = ch->pcm[idx];
                s_mix_buf[s*2+0] += (int32_t)((float)smp * ch->vol_l);
                s_mix_buf[s*2+1] += (int32_t)((float)smp * ch->vol_r);
                ch->fpos += ch->fstep;
            }
        }
        // Clamp to S16 range, then output
        sceAudioOutOutput(s_dma_port, s_out_buf);
    }
}
```

---

### 2.4 Music

| Aspect | vitaQuake1 | vitaQuake2 | KeeperFX |
|--------|-----------|-----------|---------|
| Music support | **None** | **None** | OGG streaming via `libvorbisfile` |
| Thread | — | — | `"vita_music"`, priority `0x60`, 64 KB stack |
| Semaphore | — | — | `"vita_music"` semaphore (MUS_PLAY/STOP/PAUSE/RESUME commands) |
| Format | — | — | OGG Vorbis; decoded with `ov_read()` in 2048-sample stereo grains |
| Rate | — | — | 44100 Hz stereo (separate `SCE_AUDIO_OUT_PORT_TYPE_MAIN` port) |
| Track naming | — | — | `FGrp_Music/"keeper%02d.ogg"` |
| Build guard | — | — | `VITA_HAVE_VORBIS` CMake define; stubs when absent |

#### KeeperFX music thread — `src/audio/audio_vita.c:383–448`
```c
static int vita_music_thread(SceSize args, void *argp)
{
    while (s_music_running) {
        sceKernelWaitSema(s_music_sema, 1, NULL);  // block until MUS_PLAY
        OggVorbis_File vf;
        ov_fopen(s_music_path, &vf);
        while (keep_going && s_music_running) {
            // check s_music_cmd for STOP/PAUSE/RESUME
            long got = ov_read(&vf, dst, remaining, 0, 2, 1, &bitstream);
            if (got == 0) ov_pcm_seek(&vf, 0);  // EOF → loop
            sceAudioOutOutput(s_music_port, s_music_buf);
        }
        ov_clear(&vf);
    }
}
```

---

### 2.5 FMV Audio

| Aspect | vitaQuake1 | vitaQuake2 | KeeperFX |
|--------|-----------|-----------|---------|
| FMV support | **None** | **None** | Smacker video with dedicated audio port |
| Port | — | — | `SCE_AUDIO_OUT_PORT_TYPE_MAIN`, 2048-sample grain |
| Ring buffer | — | — | 4-grain ring (`FMV_RING_GRAINS=4`, ~170 ms buffer) |
| Semaphore pair | — | — | `s_fmv_sema_empty` (free slots) + `s_fmv_sema_full` (filled slots) |
| Audio clock | — | — | `vita_fmv_audio_pts_ns()`: sub-grain (~1 ms) accuracy via `sceKernelGetSystemTimeWide` |

#### KeeperFX FMV ring buffer — `src/audio/audio_vita.c:796–810`
```c
// Producer (decode thread):
//   WaitSema(empty) → write ring[wr] → advance wr → Signal(full)
// Consumer (audio thread):
//   WaitSema(full)  → DMA ring[rd]  → advance rd → Signal(empty)
static int16_t s_fmv_ring[FMV_RING_GRAINS][FMV_DMA_GRAIN * 2]; // [4][4096]
static SceUID  s_fmv_sema_empty;  // starts at FMV_RING_GRAINS (4)
static SceUID  s_fmv_sema_full;   // starts at 0
```

---

### 2.6 Key Differences Summary

| Dimension | vitaQuake1 | vitaQuake2 | KeeperFX |
|-----------|-----------|-----------|---------|
| Overall sophistication | Minimal — mono DMA loop | Basic — stereo DMA loop | Full — SW mixer, music, FMV |
| Channel count | 1 | 1 (stereo) | 16 concurrent voices |
| Mixing | None | None | S32 accumulate → clamp → S16 |
| Music | None | None | OGG via vorbisfile |
| FMV audio | None | None | Dedicated port + ring buffer + PTS clock |
| Source data | Raw PCM in DMA buffer | Raw PCM in DMA buffer | `.dat` sound banks (lazy WAV decode) |

---

## 3. Input

### 3.1 sceCtrl Initialisation and Polling

| Call | vitaQuake1 | vitaQuake2 | KeeperFX |
|------|-----------|-----------|---------|
| `sceCtrlSetSamplingMode` | `SCE_CTRL_MODE_ANALOG_WIDE` | `SCE_CTRL_MODE_ANALOG_WIDE` | `SCE_CTRL_MODE_ANALOG` |
| Polling call | `sceCtrlPeekBufferPositive(0, &pad, 1)` | `sceCtrlPeekBufferPositive` + `sceCtrlPeekBufferNegative` | `sceCtrlPeekBufferPositive(0, &s_padData, 1)` |
| Poll location | `source/sys_psp2.c:371` | `psp2/sys_psp2.c:231–232` | `src/input/input_vita.c:163` |

`SCE_CTRL_MODE_ANALOG_WIDE` (used by the Quake ports) provides 8-bit analog
range from 0–255, same as `SCE_CTRL_MODE_ANALOG`. The `_WIDE` suffix just
changes the dead-zone handling in the hardware; KeeperFX uses `ANALOG` and
applies its own software deadzone.

#### vitaQuake1 — `source/sys_psp2.c:371–388`
```c
void Sys_SendKeyEvents(void)
{
    sceCtrlPeekBufferPositive(0, &pad, 1);
    int kDown = pad.buttons;
    int kUp   = oldpad.buttons;
    if (kDown) PSP2_KeyDown(kDown);
    if (kUp != kDown) PSP2_KeyUp(kDown, kUp);
    // ...
    oldpad = pad;
}
```

#### vitaQuake2 — `psp2/sys_psp2.c:229–239`
```c
void Sys_SendKeyEvents(void)
{
    SceCtrlData kDown, kUp;
    sceCtrlPeekBufferPositive(0, &kDown, 1);
    sceCtrlPeekBufferNegative(0, &kUp,   1);   // separate up-event buffer
    if (kDown.buttons) Sys_SetKeys(kDown.buttons, true);
    if (kUp.buttons)   Sys_SetKeys(kUp.buttons, false);
    sys_frame_time = Sys_Milliseconds();
}
```

#### KeeperFX — `src/input/input_vita.c:160–167`
```c
static void input_vita_poll_events(void)
{
    sceCtrlPeekBufferPositive(0, &s_padData, 1);
    sceTouchPeek(SCE_TOUCH_PORT_FRONT, &s_touchDataFront, 1);
    sceTouchPeek(SCE_TOUCH_PORT_BACK,  &s_touchDataBack,  1);
    update_key_states();
    // ...
}
```

---

### 3.2 Button-to-Action Mapping

| Approach | vitaQuake1 | vitaQuake2 | KeeperFX |
|----------|-----------|-----------|---------|
| Strategy | Static `KeyTable[12]` → Quake key codes | `if`-chain in `Sys_SetKeys` → Quake key codes | Static `s_buttonMap[12]` → KeeperFX `KC_*` keycodes; also CROSS/CIRCLE → mouse left/right |
| Customisable? | Yes — Quake console `bind` commands | Yes — Quake console `bind` commands | No in-game remapping; hardcoded in `input_vita.c` |

#### vitaQuake1 — `source/sys_psp2.c:332–349`
```c
psvita_buttons KeyTable[12] = {
    { SCE_CTRL_SELECT,   K_SELECT },
    { SCE_CTRL_START,    K_START  },
    { SCE_CTRL_UP,       K_UPARROW },
    { SCE_CTRL_DOWN,     K_DOWNARROW },
    { SCE_CTRL_LEFT,     K_LEFTARROW },
    { SCE_CTRL_RIGHT,    K_RIGHTARROW },
    { SCE_CTRL_LTRIGGER, K_LEFTTRIGGER },
    { SCE_CTRL_RTRIGGER, K_RIGHTTRIGGER },
    { SCE_CTRL_SQUARE,   K_SQUARE },
    { SCE_CTRL_TRIANGLE, K_TRIANGLE },
    { SCE_CTRL_CROSS,    K_CROSS },
    { SCE_CTRL_CIRCLE,   K_CIRCLE }
};
```

#### KeeperFX — `src/input/input_vita.c:37–53`
```c
static const struct { uint32_t btn_vita; int keycode; } s_buttonMap[] = {
    { SCE_CTRL_CROSS,    KC_RETURN  },  // Cross = Enter
    { SCE_CTRL_CIRCLE,   KC_ESCAPE  },  // Circle = Escape
    { SCE_CTRL_SQUARE,   KC_SPACE   },  // Square = Space
    { SCE_CTRL_TRIANGLE, KC_TAB     },  // Triangle = Tab
    { SCE_CTRL_L1,       KC_LSHIFT  },  // L trigger = Left Shift
    { SCE_CTRL_R1,       KC_LCONTROL},  // R trigger = Left Control
    { SCE_CTRL_START,    KC_ESCAPE  },  // Start = Escape
    { SCE_CTRL_SELECT,   KC_M       },  // Select = Map
    { SCE_CTRL_UP,       KC_UP      },
    { SCE_CTRL_DOWN,     KC_DOWN    },
    { SCE_CTRL_LEFT,     KC_LEFT    },
    { SCE_CTRL_RIGHT,    KC_RIGHT   },
};
```

---

### 3.3 Analog Stick Handling

| Aspect | vitaQuake1 | vitaQuake2 | KeeperFX |
|--------|-----------|-----------|---------|
| Deadzone value | **30** (`abs(lx) < 30`) | **30** (`abs(left_x) < 30`) | **30** (`abs(lx) < deadzone`) |
| Deadzone style | Axial (per-axis) | Axial (per-axis) | Axial for cursor; radial for `IN_RescaleAnalog` logic mirrored |
| Left stick | Move (forward/back/strafe) | Move | Cursor movement when no touch active |
| Right stick | Camera look (with `IN_RescaleAnalog`) | Camera look (with `IN_RescaleAnalog`) | Not used for camera — no FPS gameplay |
| Scale | Centre at 127; `lx = analogs.lx - 127` | Centre at 127; `lx = pad.lx - 127` | Centre at 128; `lx = s_padData.lx - 128` |

#### vitaQuake1 — `source/in_psp2.c:114–132` (radial rescale)
```c
void IN_RescaleAnalog(int *x, int *y, int dead) {
    float magnitude = sqrt(analogX*analogX + analogY*analogY);
    if (magnitude >= deadZone) {
        float scalingFactor = maximum / magnitude * (magnitude - deadZone) / (maximum - deadZone);
        *x = (int)(analogX * scalingFactor);
        *y = (int)(analogY * scalingFactor);
    } else { *x = 0; *y = 0; }
}
```

#### KeeperFX — `src/input/input_vita.c:122–143`
```c
// Left analog → cursor movement (axial deadzone, no rescale)
const int deadzone = 30;
if (abs(lx) < deadzone) lx = 0;
if (abs(ly) < deadzone) ly = 0;
if (lx != 0 || ly != 0) {
    s_mouseX += lx / 20;   // pixel-per-poll rate
    s_mouseY += ly / 20;
    // Clamped to 0..959 / 0..543
}
```

---

### 3.4 Touch Screen Handling

| Aspect | vitaQuake1 | vitaQuake2 | KeeperFX |
|--------|-----------|-----------|---------|
| Init call | `sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, 1)` + `BACK` | *(none explicit)* | `sceTouchSetSamplingState(FRONT, SCE_TOUCH_SAMPLING_STATE_START)` + `BACK` |
| Init location | `source/sys_psp2.c:466–467` | N/A | `src/input/input_vita.c:251–252` |
| Poll call | `sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1)` | *(touch commented out)* | `sceTouchPeek(FRONT/BACK, &touchData, 1)` |
| Coordinate space | Raw 1919×1087 → lerp to 960×544 | — | Raw 1920×1088 → divide by 2 → 960×544 |
| Usage | Front touch = `K_TOUCH` button event; back touch = camera if `retrotouch` | Touch code commented out | Front touch = absolute mouse cursor; back touch = right-click |

#### vitaQuake1 coordinate mapping — `source/in_psp2.c:175–184`
```c
sceTouchPeek(SCE_TOUCH_PORT_BACK, &touch, 1);
int raw_x = lerp(touch.report[0].x, 1919, 960);
int raw_y = lerp(touch.report[0].y, 1087, 544);
// where lerp(value, from_max, to_max) = (value * to_max) / from_max
```

#### KeeperFX coordinate mapping — `src/input/input_vita.c:103–104`
```c
s_mouseX = s_touchDataFront.report[0].x / 2;   // 1920 → 960
s_mouseY = s_touchDataFront.report[0].y / 2;   // 1088 → 544
```

---

### 3.5 Gyroscope / Motion

| Aspect | vitaQuake1 | vitaQuake2 | KeeperFX |
|--------|-----------|-----------|---------|
| Supported | **Yes** | **Yes** | **No** |
| Init | `sceMotionReset(); sceMotionStartSampling();` | `sceMotionReset(); sceMotionStartSampling();` | — |
| Read | `sceMotionGetState(&motionstate)` | `sceMotionGetState(&motionstate)` | — |
| Axes | `angularVelocity.y` → YAW, `.x` → PITCH | `angularVelocity.y` → YAW, `.x` → PITCH | — |
| Guard | `motioncam.value` cvar | `use_gyro->value` cvar | — |

#### vitaQuake1 — `source/in_psp2.c:209–222`
```c
if (motioncam.value) {
    sceMotionGetState(&motionstate);
    float x_gyro_cam = motionstate.angularVelocity.y * motion_horizontal_sensitivity.value;
    float y_gyro_cam = motionstate.angularVelocity.x * motion_vertical_sensitivity.value;
    cl.viewangles[YAW]   += x_gyro_cam;
    cl.viewangles[PITCH] -= y_gyro_cam;
}
```

**KeeperFX note:** Gyro is omitted; DK1 has no concept of free-look — the
camera is isometric / fixed-angle. Adding gyro would require a separate feature.

---

### 3.6 Rumble Support

| Aspect | vitaQuake1 | vitaQuake2 | KeeperFX |
|--------|-----------|-----------|---------|
| Supported | **Yes** (PSTV only) | **Yes** (PSTV only) | **No** |
| Start | `sceCtrlSetActuator(1, {small=100, large=100})` | `sceCtrlSetActuator(1, {small=100, large=100})` | — |
| Stop | `sceCtrlSetActuator(1, {small=0, large=0})` | `sceCtrlSetActuator(1, {small=0, large=0})` | — |
| Duration | Auto-stop after 500 ms (`sceKernelGetProcessTimeLow` delta) | Auto-stop after 500 ms | — |
| Guard | `pstv_rumble` cvar | `pstv_rumble` cvar | — |

#### vitaQuake1 — `source/in_psp2.c:95–112`
```c
void IN_StartRumble(void) {
    if (!pstv_rumble.value) return;
    SceCtrlActuator handle = { .small = 100, .large = 100 };
    sceCtrlSetActuator(1, &handle);
    rumble_tick = sceKernelGetProcessTimeWide();
}
void IN_StopRumble(void) {
    SceCtrlActuator handle = { .small = 0, .large = 0 };
    sceCtrlSetActuator(1, &handle);
    rumble_tick = 0;
}
```

---

## 4. Platform / System

### 4.1 Memory Configuration

| Symbol | vitaQuake1 | vitaQuake2 | KeeperFX |
|--------|-----------|-----------|---------|
| `_newlib_heap_size_user` | `192 MB` | `192 MB` | `192 MB` |
| `sceUserMainThreadStackSize` | *(not set — uses Vita default 512 KB)* | `8 MB` | `4 MB` |
| Runtime main thread | Spawns worker at `sceKernelCreateThread("Quake", ..., 0x800000)` = 8 MB | Runs in main thread | Runs in main thread (SDL) |

#### vitaQuake1 — `source/sys_psp2.c:391` + `808–814`
```c
int _newlib_heap_size_user = 192 * 1024 * 1024;
// ...
int main(int argc, char **argv) {
    SceUID main_thread = sceKernelCreateThread("Quake", quake_main,
        0x40, 0x800000, 0, 0, NULL);  // 8 MB worker stack
    sceKernelStartThread(main_thread, 0, NULL);
    return sceKernelExitDeleteThread(0);
}
```

#### vitaQuake2 — `psp2/sys_psp2.c:35–36`
```c
int _newlib_heap_size_user   = 192 * 1024 * 1024;
int sceUserMainThreadStackSize = 8 * 1024 * 1024;   // 8 MB main thread
```

#### KeeperFX — `src/platform/PlatformVita.cpp:40–41`
```c
int _newlib_heap_size_user     = 192 * 1024 * 1024; // 192 MB heap
int sceUserMainThreadStackSize =   4 * 1024 * 1024; // 4 MB main thread stack
```

**KeeperFX note:** 4 MB is sufficient; SDL2's main loop is shallow. The
comment block above the declarations (lines 28–39) documents the full 256 MB
process budget breakdown.

---

### 4.2 Clock Speeds

All three codebases set identical clocks.

| Clock | Value | vitaQuake1 | vitaQuake2 | KeeperFX |
|-------|-------|-----------|-----------|---------|
| ARM CPU | 444 MHz | `sys_psp2.c:459` | `sys_psp2.c:447` | `PlatformVita.cpp:369` |
| Bus | 222 MHz | `sys_psp2.c:460` | `sys_psp2.c:448` | `PlatformVita.cpp:370` |
| GPU | 222 MHz | `sys_psp2.c:461` | `sys_psp2.c:449` | `PlatformVita.cpp:371` |
| GPU XBar | 166 MHz | `sys_psp2.c:462` | `sys_psp2.c:450` | `PlatformVita.cpp:372` |

```c
// Identical in all three:
scePowerSetArmClockFrequency(444);
scePowerSetBusClockFrequency(222);
scePowerSetGpuClockFrequency(222);
scePowerSetGpuXbarClockFrequency(166);
```

---

### 4.3 Logging Approach

| Aspect | vitaQuake1 | vitaQuake2 | KeeperFX |
|--------|-----------|-----------|---------|
| Normal logging | `fopen("ux0:/data/Quake/log.txt", "a+")` + `fwrite` | `fopen("ux0:/data/quake2/quake.log", "a+")` + `fwrite` | `sceClibPrintf("KeeperFX: %s", message)` |
| Conditional | `#ifdef DEBUG` only | `#ifndef RELEASE` only | Always enabled at WARNING/ERROR level |
| Location | `source/sys_psp2.c:100–118` | `psp2/sys_psp2.c:58–74` | `src/platform/PlatformVita.cpp:344–346` |

#### vitaQuake1 — `source/sys_psp2.c:100–118`
```c
void Log(const char *format, ...) {
#ifdef DEBUG
    // vsprintf into msg
    FILE* f = fopen("ux0:/data/Quake/log.txt", "a+");
    if (log != NULL) { fwrite(msg, 1, strlen(msg), log); fclose(log); }
#endif
}
```

#### vitaQuake2 — `psp2/sys_psp2.c:58–74`
```c
void LOG_FILE(const char *format, ...) {
#ifndef RELEASE
    // vsprintf into msg
    FILE* log = fopen("ux0:/data/quake2/quake.log", "a+");
    if (log != NULL) { fwrite(msg, 1, strlen(msg), log); fclose(log); }
#endif
}
```

#### KeeperFX — `src/platform/PlatformVita.cpp:344–346`
```c
void PlatformVita::LogWrite(const char* message)
{
    sceClibPrintf("KeeperFX: %s", message);
}
```

**KeeperFX note:** Using `sceClibPrintf` sends output to the PlayStation TV /
development kit debug UART. This is always active (no `#ifdef`). The game's
higher-level logging (JUSTLOG/WARNLOG/ERRORLOG) calls `PlatformVita::LogWrite`
via the platform abstraction, so all log levels reach the same sink.

---

### 4.4 Fatal Error Handling

| Aspect | vitaQuake1 | vitaQuake2 | KeeperFX |
|--------|-----------|-----------|---------|
| On `Sys_Error` | Write to `log.txt` → `Sys_Quit()` → `sceKernelExitProcess(0)` | Write to `quake.log` → spin loop (wait for START) | Signal handlers (SIGSEGV/SIGABRT/etc.) → write `crash.log` → `sceClibPrintf` → `sceKernelExitProcess(1)` |
| Crash file | `log.txt` (appended) | `quake.log` (appended) | `ux0:data/keeperfx/crash.log` (appended) |
| Recoverable? | No — process exits | Partial — waits for user to press START | No — exits with code 1 |
| Signal handlers | None | None | SIGSEGV, SIGABRT, SIGFPE, SIGILL |

#### vitaQuake1 — `source/sys_psp2.c:261–277`
```c
void Sys_Error(const char *error, ...) {
    // vsnprintf into buf
    FILE* f = fopen("ux0:/data/Quake/log.txt", "a+");
    fwrite(buf, 1, strlen(buf), f);
    fclose(f);
    Sys_Quit();   // → sceKernelExitProcess(0)
}
```

#### vitaQuake2 — `psp2/sys_psp2.c:76–94`
```c
void Sys_Error(char *error, ...) {
    LOG_FILE(str);
    while(1) {   // spin forever — user presses START to exit
        SceCtrlData pad;
        sceCtrlPeekBufferPositive(0, &pad, 1);
        if (pad.buttons & SCE_CTRL_START) break;
    }
}
```

#### KeeperFX — `src/platform/PlatformVita.cpp:82–101`
```c
static void vita_crash_handler(int sig) {
    FILE* f = fopen("ux0:data/keeperfx/crash.log", "a");
    if (f) { fprintf(f, "KeeperFX crashed: signal %d\n", sig); fclose(f); }
    sceClibPrintf("KeeperFX CRASH: signal %d\n", sig);
    sceKernelExitProcess(1);
}
void PlatformVita::ErrorParachuteInstall() {
    signal(SIGSEGV, vita_crash_handler);
    signal(SIGABRT, vita_crash_handler);
    signal(SIGFPE,  vita_crash_handler);
    signal(SIGILL,  vita_crash_handler);
}
```

---

### 4.5 Module Loading

| Module | vitaQuake1 | vitaQuake2 | KeeperFX |
|--------|-----------|-----------|---------|
| `SCE_SYSMODULE_NET` | Yes (`sys_psp2.c:463`) | Yes (`sys_psp2.c:452`) | **No** |
| `SCE_SYSMODULE_PSPNET_ADHOC` | Yes (`sys_psp2.c:464`) | **No** | **No** |
| Any others | None | None | None |

Both Quake ports need networking for multiplayer. KeeperFX has no peer-to-peer
networking on Vita (its LAN multiplayer is not implemented for the platform),
so no `sceSysmoduleLoadModule` calls are needed.

#### vitaQuake1 — `source/sys_psp2.c:463–464`
```c
sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
sceSysmoduleLoadModule(SCE_SYSMODULE_PSPNET_ADHOC);
```

#### vitaQuake2 — `psp2/sys_psp2.c:452`
```c
sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
```

---

### 4.6 File I/O

| Aspect | vitaQuake1 | vitaQuake2 | KeeperFX |
|--------|-----------|-----------|---------|
| Primary API | `FILE*` (POSIX `fopen/fread/fwrite/fclose`) | `FILE*` (POSIX) | `sceIo*` via platform abstraction (`sceIoOpen`, `sceIoRead`, `sceIoWrite`, `sceIoLseek`, `sceIoClose`) |
| Directory ops | `sceIoMkdir`, `sceIoDopen`/`sceIoDread`/`sceIoDclose` | `sceIoMkdir`, `sceIoDopen`/`sceIoDread`/`sceIoDclose` | `sceIoMkdir`, `sceIoDopen`/`sceIoDread`/`sceIoDclose` |
| Abstraction | None — direct calls throughout | None — direct calls throughout | `IPlatform` methods (`FileOpen`, `FileRead`, `FileWrite`, `FileSeek`, `FileClose`, …) in `PlatformVita.cpp` |
| Path resolution | Manual `ux0:` / `uma0:` prefixing | Manual `ux0:` / `uma0:` prefixing | `vita_modify_load_filename()` — prepends `keeper_runtime_directory` to relative paths |

#### vitaQuake1 — `source/sys_psp2.c:133–150` (POSIX)
```c
int Sys_FileOpenRead(char *path, int *hndl) {
    FILE *f = fopen(path, "rb");
    // ...
}
```

#### KeeperFX file open — `src/platform/PlatformVita.cpp:243–262` (sceIo)
```c
TbFileHandle PlatformVita::FileOpen(const char* fname, unsigned char accmode)
{
    int flags;
    switch (accmode) {
        case Lb_FILE_MODE_NEW:       flags = SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC;  break;
        case Lb_FILE_MODE_READ_ONLY: flags = SCE_O_RDONLY;                               break;
        // ...
    }
    SceUID fd = sceIoOpen(vita_modify_load_filename(fname), flags, 0777);
    // ... wrap in TbFileInfo* opaque handle
}
```

#### KeeperFX path resolver — `src/platform/PlatformVita.cpp:354–364`
```c
extern "C" const char *vita_modify_load_filename(const char *input)
{
    if (input[0] == '*' || input[0] == '!') return input;  // special markers
    if (strchr(input, ':') || input[0] == '/') return input; // already absolute
    static char resolved[2048];
    snprintf(resolved, sizeof(resolved), "%s/%s", keeper_runtime_directory, input);
    return resolved;
}
```

---

## Quick-Reference: Key Numbers

| Parameter | vitaQuake1 | vitaQuake2 | KeeperFX |
|-----------|-----------|-----------|---------|
| Heap (`_newlib_heap_size_user`) | 192 MB | 192 MB | 192 MB |
| Main thread stack | 8 MB (spawned worker) | 8 MB | 4 MB |
| ARM clock | 444 MHz | 444 MHz | 444 MHz |
| GPU clock | 222 MHz | 222 MHz | 222 MHz |
| Audio rate | 48 kHz mono | 48 kHz stereo | 48 kHz stereo (SFX) / 44.1 kHz (music) |
| Audio DMA grain | 8192 samples | 4096 samples | 1024 samples (SFX) / 2048 (music/FMV) |
| Audio channels | 1 | 1 (stereo) | 16 SW-mixer voices |
| Analog deadzone | 30 | 30 | 30 |
| Vertex pool | 64 MB | not set | 64 MB |
| VDM buffer | not set | 1 MB | 1 MB |
| vglInitExtended cmd buf | 20 MB | 0 | 20 MB |
| vglInitExtended RAM pool | 16 MB | 16 MB | 16 MB |
| vglUseVram | off | on | on |
