# Nintendo 3DS Debugging Guide for KeeperFX

Remote debugging of KeeperFX running on a 3DS via GDB over serial UART connection or crash dump analysis.

## Architecture

```
VS Code (cppdbg)  ──►  arm-none-eabi-gdb (Docker)  ──►  Serial Port / USB
                                                          │
                                                      3DS Debug Probe
                                                          │
                                                      keeperfx (target process)
```

GDB runs inside a Docker container via `pipeTransport` — no local ARM toolchain install needed.

## Prerequisites

1. **Docker Desktop** running
2. **3DS Debug Probe** connected (e.g., custom UART dongle or official DevKit cable)
3. **Serial connection** to 3DS debug port (typically via USB adapter)
4. **keeperfx.3dsx** (3ds-debug build) installed on the 3DS
5. PC and 3DS connected via serial interface

## Quick Start: Serial Debugging

### 1. Identify Serial Port

```bash
# Windows
Get-SerialPort  # PowerShell, or check Device Manager

# Linux/macOS
ls /dev/tty*
```

### 2. Connect via GDB

```bash
docker compose run 3ds-sdk bash -c 'arm-none-eabi-gdb out/build/3ds-debug/keeperfx'
```

Then in GDB prompt:

```gdb
target remote /dev/ttyUSB0
# or for serial over network:
target remote tcp:<3ds-ip>:3333
```

### 3. Set Breakpoints and Continue

```gdb
break main
break my_function
continue
```

## Debugging Approach

### Serial UART Debugging

Direct GDB debugging over serial connection to a 3DS with debug probe:

| Feature | Status |
|---------|--------|
| Source-level breakpoints | ✅ Set breakpoints in VS Code, they hit on 3DS |
| Call stack / backtrace | ✅ Full stack via DWARF CFI |
| Local variables | ✅ Hover or Watch panel (requires `-O0` build) |
| Register inspection | ✅ r0-r15, cpsr in Registers panel |
| Step over / step into | ✅ Single-step and continue |
| Thread inspection | ⚠️ Single-threaded; thread support limited |

### Crash Dump Analysis

When the 3DS generates a crash dump:

1. **Extract crash dump** from 3DS SD card (`sdmc:/crash-dumps/`)
2. **Use crash analyzer** to decode the dump
3. **Map addresses** back to `keeperfx.elf` using `arm-none-eabi-addr2line`
4. **Symbolicate stack trace** with debug symbols from `3ds-debug` or `3ds-reldebug` build

## Step-by-Step Debugging Setup

### Option A: Serial Debug Probe (Hardware)

1. **Obtain debug probe**:
   - Use official 3DS DevKit cable (if available)
   - Or build custom UART adapter (requires electrical knowledge)

2. **Connect probe**:
   - Attach to 3DS debug port (internal connector on motherboard)
   - Connect USB end to PC

3. **Enable debug mode in code**:
   ```c
   #ifdef PLATFORM_3DS
   #ifdef DEBUG
   // Serial debug output enabled
   printf("Debug message\n");
   #endif
   #endif
   ```

4. **Build with debug symbols**:
   ```bash
   cmake --preset 3ds-debug
   cmake --build --preset 3ds-debug
   ```

5. **Launch GDB session**:
   ```bash
   docker compose run 3ds-sdk bash -c \
     'arm-none-eabi-gdb -ex "target remote /dev/ttyUSB0" \
                        out/build/3ds-debug/keeperfx'
   ```

### Option B: Crash Dump Analysis (Post-Mortem)

1. **Extract crash dump**:
   - From 3DS SD card: `sdmc:/crash-dumps/report-*.dat`
   - Use specialized tools to parse (e.g., Luma3DS crash dump viewer)

2. **Decode crash dump**:
   ```bash
   # Use crash analyzer tool (if available)
   ./tools/crash_analyzer report-XXXXX.dat
   ```

3. **Symbolicate addresses**:
   ```bash
   # Get function names and line numbers from addresses
   docker compose run 3ds-sdk bash -c \
     'arm-none-eabi-addr2line -e out/build/3ds-debug/keeperfx <address>'
   ```

   Example:
   ```bash
   docker compose run 3ds-sdk bash -c \
     'arm-none-eabi-addr2line -e out/build/3ds-debug/keeperfx 0x080123AB'
   # Output: src/game.c:456
   ```

4. **Analyze stack trace**:
   ```
   Crash occurred at 0x08001234 (game_update+0x42)
   Called from 0x08005678 (main_loop+0x18)
   Called from 0x08000ABC (main+0x50)
   ```

## GDB Commands Reference

Common commands when debugging via serial:

```gdb
# Connection
target remote /dev/ttyUSB0          # Connect to serial debug port
disconnect                          # Close connection

# Execution
break function_name                 # Set breakpoint at function
break src/file.c:123                # Set breakpoint at line
continue                            # Resume execution
step                                # Step into function
next                                # Step over function
finish                              # Run until return

# Inspection
info registers                      # Show ARM register values
print variable_name                 # Print variable value
p *array_ptr@10                     # Print array of 10 elements
backtrace                           # Show call stack
info threads                        # List threads (if multithread support)

# Memory Inspection
x/16xb 0x08001000                  # Dump 16 bytes at address as hex
dump memory output.bin 0x08000000 0x08100000  # Dump memory region to file
```

## File-Based Logging (Alternative to Serial)

If debug probe is unavailable, use file-based logging:

```c
#ifdef PLATFORM_3DS
#ifdef DEBUG
FILE *log_file = fopen("sdmc:/keeperfx_debug.log", "a");
if (log_file) {
    fprintf(log_file, "Debug: %s\n", message);
    fflush(log_file);
    fclose(log_file);
}
#endif
#endif
```

1. Build with `3ds-debug` preset
2. Add logging statements at critical points
3. After crash, extract `sdmc:/keeperfx_debug.log` from SD card
4. Analyze logs to determine failure point

## Build Artifacts for Debugging

- `out/build/3ds-debug/keeperfx` — ELF executable with full debug symbols
- `out/build/3ds-debug/keeperfx.3dsx` — 3DSX executable (loadable on device)
- `out/build/3ds-reldebug/keeperfx` — ELF with optimized code but retained symbols

## Tools Available

- `arm-none-eabi-gdb` — GNU Debugger for ARM (in Docker container)
- `arm-none-eabi-addr2line` — Convert addresses to source file:line
- `arm-none-eabi-readelf` — Read ELF symbol table and debug info
- `arm-none-eabi-nm` — List symbols in ELF file

## Troubleshooting

### "Unable to establish connection"
- Is the debug probe connected and recognized by the OS?
- Try `ls /dev/ttyUSB*` (Linux) or Device Manager (Windows)
- Is the 3DS powered on?

### Breakpoints show as "unverified"
- Ensure you built with the **3ds-debug** preset (`CMAKE_BUILD_TYPE: Debug`)
- Verify DWARF info: `docker compose run 3ds-sdk arm-none-eabi-readelf --debug-dump=info out/build/3ds-debug/keeperfx | head -50`

### Variables show as "optimized out"
- You're using a Release or RelWithDebInfo build
- Switch to **3ds-debug** preset (`-O0 -g`) for full variable visibility

### Crash dump analysis shows wrong symbols
- Ensure you're using the correct ELF file corresponding to the build that crashed
- Symbol addresses must match the exact binary that ran on the 3DS

## Notes

- Debug probe access is limited; serial debugging may not always be available
- Post-mortem crash dump analysis is the most practical debugging approach for most developers
- Keep both `3ds-debug` and `3ds-reldebug` builds for different scenarios:
  - Use `3ds-debug` for interactive debugging when hardware is available
  - Use `3ds-reldebug` for production profiling and crash analysis
