# Nintendo Wii U Debugging Guide for KeeperFX

Remote debugging of KeeperFX running on Wii U via GDB over serial connection or file-based crash dump analysis.

## Architecture

```
VS Code (cppdbg)  ──►  powerpc-eabi-gdb (Docker)  ──►  Serial Port / Network
                                                        │
                                                    Wii U GDB Stub
                                                        │
                                                    keeperfx (target process)
```

GDB runs inside a Docker container via `pipeTransport` — no local PowerPC toolchain install needed.

## Prerequisites

1. **Docker Desktop** running
2. **Wii U GDB Stub** running on Wii U (via loaded homebrew plugin or kernel module)
3. **Serial connection** or **network connection** to Wii U debug interface
4. **keeperfx.rpx** (wii-u-debug build) installed on the Wii U
5. PC and Wii U connected via serial/network interface

## Quick Start: GDB Debugging

### 1. Identify Connection

For serial connection:
```bash
# Windows
Get-SerialPort  # PowerShell, or check Device Manager

# Linux/macOS
ls /dev/tty*
```

For network GDB stub (if available):
- Determine Wii U's IP address (check router or Wii U settings)

### 2. Connect via GDB

```bash
docker compose run wii-u-sdk bash -c 'powerpc-eabi-gdb out/build/wii-u-debug/keeperfx'
```

Then in GDB prompt:

```gdb
# Serial connection
target remote /dev/ttyUSB0

# Network connection (if GDB stub runs on network)
target remote <wii-u-ip>:4242
```

### 3. Set Breakpoints and Continue

```gdb
break main
break my_function
continue
```

## Debugging Approach

### GDB over Serial/Network

Direct GDB debugging via connection to Wii U:

| Feature | Status |
|---------|--------|
| Source-level breakpoints | ✅ Set breakpoints in VS Code, they hit on Wii U |
| Call stack / backtrace | ✅ Full stack via DWARF CFI |
| Local variables | ✅ Hover or Watch panel (requires `-O0` build) |
| Register inspection | ✅ PowerPC registers in Registers panel |
| Step over / step into | ✅ Single-step and continue |

### File-Based Logging

When GDB debugging is unavailable, use file-based logging to analyze behavior:

1. Inject logging statements into code
2. Write logs to Wii U storage (SD card or internal memory)
3. Extract logs after execution or crash
4. Analyze logs to determine failure point

### Crash Dump Analysis

When Wii U generates a crash:

1. **Extract crash dump** from Wii U (if available)
2. **Use crash analyzer** to decode the dump
3. **Map addresses** back to `keeperfx.elf` using `powerpc-eabi-addr2line`
4. **Symbolicate stack trace** with debug symbols from `wii-u-debug` or `wii-u-reldebug` build

## Step-by-Step Debugging Setup

### Option A: GDB Stub (Hardware/Homebrew)

1. **Load GDB stub**:
   - Use existing Wii U homebrew GDB stub (if available)
   - Or develop custom kernel module that exposes GDB protocol

2. **Connect GDB stub to network/serial**:
   - Configure stub to listen on port 4242 (network) or serial device
   - Ensure stub is running before launching keeperfx

3. **Enable debug mode in code**:
   ```c
   #ifdef PLATFORM_WIIU
   #ifdef DEBUG
   // Debug logging enabled
   os_console_print("Debug message\n");
   #endif
   #endif
   ```

4. **Build with debug symbols**:
   ```bash
   cmake --preset wii-u-debug
   cmake --build --preset wii-u-debug
   ```

5. **Launch GDB session**:
   ```bash
   docker compose run wii-u-sdk bash -c \
     'powerpc-eabi-gdb -ex "target remote <wii-u-ip>:4242" \
                       out/build/wii-u-debug/keeperfx'
   ```

### Option B: File-Based Logging

1. **Add logging to code**:
   ```c
   #ifdef PLATFORM_WIIU
   #ifdef DEBUG
   FILE *log_file = fopen("sd:/keeperfx_debug.log", "a");
   if (log_file) {
       fprintf(log_file, "Debug: %s\n", message);
       fflush(log_file);
       fclose(log_file);
   }
   #endif
   #endif
   ```

2. **Build with debug symbols**:
   ```bash
   cmake --preset wii-u-debug
   cmake --build --preset wii-u-debug
   ```

3. **Extract log file**:
   - Access Wii U SD card: `sd:/keeperfx_debug.log`
   - Use Wii U homebrew tools to extract

4. **Analyze logs**:
   - Review log output to trace execution flow
   - Identify point of failure or unexpected behavior

### Option C: Crash Dump Analysis

1. **Capture crash dump**:
   - Wii U may generate crash dump if error handler is installed
   - Extract from Wii U storage location (varies by implementation)

2. **Symbolicate with powerpc-eabi-addr2line**:
   ```bash
   docker compose run wii-u-sdk bash -c \
     'powerpc-eabi-addr2line -e out/build/wii-u-debug/keeperfx <address>'
   ```

   Example:
   ```bash
   docker compose run wii-u-sdk bash -c \
     'powerpc-eabi-addr2line -e out/build/wii-u-debug/keeperfx 0x10001234'
   # Output: src/game.c:456
   ```

## GDB Commands Reference

Common commands when debugging:

```gdb
# Connection
target remote /dev/ttyUSB0                  # Connect to serial debug port
target remote <wii-u-ip>:4242               # Connect to network GDB stub
disconnect                                  # Close connection

# Execution
break function_name                         # Set breakpoint at function
break src/file.c:123                        # Set breakpoint at line
continue                                    # Resume execution
step                                        # Step into function
next                                        # Step over function
finish                                      # Run until return

# Inspection
info registers                              # Show PowerPC register values
print variable_name                         # Print variable value
p *array_ptr@10                             # Print array of 10 elements
backtrace                                   # Show call stack

# Memory Inspection
x/16xb 0x10001000                          # Dump 16 bytes at address as hex
dump memory output.bin 0x10000000 0x11000000  # Dump memory region to file
```

## Build Artifacts for Debugging

- `out/build/wii-u-debug/keeperfx.elf` — ELF executable with full debug symbols
- `out/build/wii-u-debug/keeperfx.rpx` — RPX executable (loadable on Wii U)
- `out/build/wii-u-reldebug/keeperfx.elf` — ELF with optimized code but retained symbols

## Tools Available

- `powerpc-eabi-gdb` — GNU Debugger for PowerPC (in Docker container)
- `powerpc-eabi-addr2line` — Convert addresses to source file:line
- `powerpc-eabi-readelf` — Read ELF symbol table and debug info
- `powerpc-eabi-nm` — List symbols in ELF file

## Troubleshooting

### "Unable to establish connection"
- Is the GDB stub running on Wii U?
- Is the network connection established?
- Try pinging the Wii U: `ping <wii-u-ip>`
- Check if port 4242 is open: `telnet <wii-u-ip> 4242` (on Linux/WSL)

### Breakpoints show as "unverified"
- Ensure you built with the **wii-u-debug** preset (`CMAKE_BUILD_TYPE: Debug`)
- Verify DWARF info: `docker compose run wii-u-sdk powerpc-eabi-readelf --debug-dump=info out/build/wii-u-debug/keeperfx | head -50`

### Variables show as "optimized out"
- You're using a Release or RelWithDebInfo build
- Switch to **wii-u-debug** preset (`-O0 -g`) for full variable visibility

### Crashes during build
- Wii U PowerPC compilation can be resource-intensive
- Reduce parallel build jobs: `cmake --build --preset wii-u-debug -j2`

## Notes

- GDB debugging support depends on available Wii U homebrew tools
- File-based logging is the most reliable fallback approach
- Keep both `wii-u-debug` and `wii-u-reldebug` builds for different scenarios:
  - Use `wii-u-debug` for interactive debugging when GDB stub is available
  - Use `wii-u-reldebug` for production profiling and crash analysis
  - Use file-based logging for comprehensive execution tracing
