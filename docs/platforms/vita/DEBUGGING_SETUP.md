# PS Vita Debugging Guide for KeeperFX

Breakpoint debugging of KeeperFX running on a PS Vita, driven from VS Code.

## Architecture

```
VS Code (cppdbg)  ──►  arm-vita-eabi-gdb (Docker)  ──►  TCP:31337
                                                            │
                                                        vdbtcp.suprx (Vita user plugin, TCP↔pipe bridge)
                                                            │
                                                        kvdb.skprx (Vita kernel plugin, GDB stub)
                                                            │
                                                        keeperfx (target process)
```

GDB runs inside the `ghcr.io/cerwym/build-vitasdk:latest` Docker container via
`pipeTransport` — no local ARM toolchain install needed.

## Prerequisites

1. **Docker Desktop** running
2. **kvdb.skprx** + **vdbtcp.suprx** deployed to `ur0:tai/` on the Vita
3. **taiHEN `config.txt`** configured (see below)
4. **keeperfx.vpk** (vita-debug build) installed on the Vita
5. Vita and PC on the same network

## taiHEN Configuration

Edit `ur0:tai/config.txt` on the Vita:

```
*KERNEL
ur0:tai/kvdb.skprx

*main
ur0:tai/vdbtcp.suprx
```

`*KERNEL` loads the GDB stub into kernel space. `*main` injects the TCP bridge
into the **foreground application's process** — so when keeperfx is running,
vdbtcp shares its address space. This is required for Razor GPU capture to work.

For a more targeted setup (only inject into keeperfx, not every app), replace
`*main` with keeperfx's title ID:

```
*KERNEL
ur0:tai/kvdb.skprx

*KPFX00001
ur0:tai/vdbtcp.suprx
```

Replace `KPFX00001` with the title ID from your `vita_create_vpk()` call.

## Quick Start

1. Run task **"Fetch Vita Debugger Plugins"** (one-time) to pull kvdb + vdbtcp
2. Run task **"Deploy Vita Debugger"** to FTP plugins to the Vita
3. Reboot the Vita (loads taiHEN plugins)
4. Launch keeperfx on the Vita — it will halt waiting for GDB
5. Press **F5** on the **"Vita Remote Debug (GDB)"** configuration
6. Enter the Vita's IP address when prompted
7. GDB connects → breakpoints, call stack, locals, watch expressions all work

## What Works

| Feature | Status |
|---------|--------|
| Source-level breakpoints | ✅ Set breakpoints in VS Code, they hit on Vita |
| Call stack / backtrace | ✅ Full stack via DWARF CFI |
| Local variables | ✅ Hover or Watch panel (requires `-O0` build) |
| Watch expressions | ✅ Any C expression GDB can evaluate |
| Thread list with names | ✅ kvdb serves thread names via qThreadExtraInfo |
| Monitor commands | ✅ Type `monitor help` in Debug Console |
| Register inspection | ✅ r0-r15, cpsr in Registers panel |
| Step over / step into | ✅ Single-step and continue |

## Monitor Commands

In the **Debug Console**, type:

```
monitor help        — list available commands
monitor threads     — thread list with name, priority, state
monitor modules     — loaded modules with base address
monitor display     — framebuffer info (resolution, format, VRAM addr, vblank count)
```

## Inspecting GPU/Audio State at a Breakpoint

When halted at a breakpoint, GDB can inspect **any in-memory struct** via DWARF
debug info. No special kvdb support needed — just use the Watch panel or Debug
Console.

### vitaGL GPU State

```gdb
# In Debug Console — inspect vitaGL internals
p vgl_context
p vgl_context->vram_usage
p vgl_context->framebuffer
p *gxm_render_target
```

### Audio State

```gdb
# Inspect the audio mixer state
p audio_status
p audio_port
```

### GXM Display Buffers

```gdb
# Inspect raw GXM display buffer info
p display_buffer_data
```

> **Tip:** Add expressions to the **Watch** panel for persistent monitoring
> across step operations. Any global or local variable visible at the current
> scope works.

## Razor GPU Capture

Capture a GPU frame for offline analysis in Sony's Razor profiler.

### Trigger a capture

From a TCP client (e.g. `nc` or a script), connect to the Vita's command port:

```bash
echo "razor" | nc <vita-ip> 1338
# or with a custom filename:
echo "razor ux0:data/my_capture.sgx" | nc <vita-ip> 1338
```

Default output: `ux0:data/razor_capture.sgx`

### Retrieve and view

1. FTP the `.sgx` file from the Vita (`ux0:data/`)
2. Open in **PSP2Razor.exe** (`C:\Program Files (x86)\SCE\PSP2\Tools\Razor\bin\PSP2Razor.exe`)

> **Note:** Razor capture requires vdbtcp to be loaded in keeperfx's process
> (via `*main` or the title ID in `config.txt`). See the taiHEN Configuration
> section above.

## Troubleshooting

### "Unable to establish connection"
- Is Docker Desktop running?
- Is the Vita on and connected to WiFi?
- Is vdbtcp.suprx loaded? (check `ur0:/tai/config.txt`)
- Try `ping <vita-ip>` and `telnet <vita-ip> 31337`

### Breakpoints show as "unverified"
- Ensure you built with the **vita-debug** preset (`CMAKE_BUILD_TYPE: Debug`)
- Check `sourceFileMap` maps `/src` → your workspace folder
- Verify DWARF info: `docker compose run vitasdk arm-vita-eabi-readelf --debug-dump=info out/build/vita-debug/keeperfx | head -50`

### Variables show as "optimized out"
- You're using a Release or RelWithDebInfo build
- Switch to **vita-debug** preset (`-O0 -g`) for full variable visibility

### GDB timeout on connect
- WiFi latency can be high; `set remotetimeout 10` is already configured
- If still failing, try `set remotetimeout 30` in setupCommands

### Symbols not loading
- The `program` path in launch.json must match where the ELF lives inside Docker
- Currently: `/src/out/build/vita-debug/keeperfx`
- This maps to `${workspaceFolder}/out/build/vita-debug/keeperfx` on the host
