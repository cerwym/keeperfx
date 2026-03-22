# Nintendo Switch Debugging Guide for KeeperFX

Remote debugging of KeeperFX running on Switch via aarch64 GDB over network connection or file-based logging.

## Architecture

```
VS Code (cppdbg)  ──►  aarch64-linux-gnu-gdb (Docker)  ──►  TCP (Network)
                                                              │
                                                         Switch GDB Stub
                                                              │
                                                         keeperfx (target process)
```

GDB runs inside a Docker container via `pipeTransport` — no local aarch64 toolchain install needed.

## Prerequisites

1. **Docker Desktop** running
2. **Switch GDB Stub** running on Switch (via loaded homebrew or kernel module)
3. **Network connection** to Switch debug interface
4. **keeperfx.nsp** (switch-debug build) installed on the Switch
5. PC and Switch on the same network

## Quick Start: Network GDB Debugging

### 1. Determine Switch IP Address

```bash
# Access Switch system settings or check router
# Example IP: 192.168.1.100
```

### 2. Connect via GDB

```bash
docker compose run switch-sdk bash -c 'aarch64-linux-gnu-gdb out/build/switch-debug/keeperfx'
```

Then in GDB prompt:

```gdb
target remote <switch-ip>:4242
```

### 3. Set Breakpoints and Continue

```gdb
break main
break my_function
continue
```

## Debugging Approach

### GDB over Network

Direct GDB debugging via network connection to Switch:

| Feature | Status |
|---------|--------|
| Source-level breakpoints | ✅ Set breakpoints in VS Code, they hit on Switch |
| Call stack / backtrace | ✅ Full stack via DWARF CFI |
| Local variables | ✅ Hover or Watch panel (requires `-O0` build) |
| Register inspection | ✅ aarch64 registers in Registers panel |
| Step over / step into | ✅ Single-step and continue |
| Thread support | ✅ Full multithreading support via GDB stub |

### File-Based Logging

When GDB debugging is unavailable, use file-based logging:

1. Inject logging statements into code
2. Write logs to Switch storage (SD card)
3. Extract logs after execution
4. Analyze logs to determine behavior/failure point

## Step-by-Step Debugging Setup

### Option A: Network GDB Debugging

1. **Load GDB stub on Switch**:
   - Use existing Switch homebrew GDB stub (if available)
   - Or develop custom module that exposes GDB protocol over network

2. **Configure GDB stub**:
   - Listen on port 4242 (TCP, network debugging)
   - Ensure stub is running before launching keeperfx

3. **Enable debug mode in code**:
   ```c
   #ifdef PLATFORM_SWITCH
   #ifdef DEBUG
   // Debug logging enabled
   syslog(LOG_DEBUG, "Debug message\n");
   #endif
   #endif
   ```

4. **Build with debug symbols**:
   ```bash
   cmake --preset switch-debug
   cmake --build --preset switch-debug
   ```

5. **Launch GDB session**:
   ```bash
   docker compose run switch-sdk bash -c \
     'aarch64-linux-gnu-gdb -ex "target remote <switch-ip>:4242" \
                            out/build/switch-debug/keeperfx'
   ```

6. **Interact with debugger**:
   ```gdb
   (gdb) break main
   (gdb) continue
   # ... debugging continues on Switch
   ```

### Option B: File-Based Logging (Recommended)

File-based logging is the most practical debugging approach for Switch:

1. **Add logging to code**:
   ```c
   #ifdef PLATFORM_SWITCH
   #ifdef DEBUG
   FILE *log_file = fopen("/sdcard/keeperfx_debug.log", "a");
   if (log_file) {
       fprintf(log_file, "[%08x] Debug: %s\n", svc_get_tick(), message);
       fflush(log_file);
       fclose(log_file);
   }
   #endif
   #endif
   ```

2. **Build with debug symbols**:
   ```bash
   cmake --preset switch-debug
   cmake --build --preset switch-debug
   ```

3. **Run on Switch**:
   - Install keeperfx.nsp via homebrew
   - Launch application
   - Let it run to completion or crash

4. **Extract log file**:
   ```bash
   # Use homebrew tools to access Switch SD card
   # Log file: /sdcard/keeperfx_debug.log
   ```

5. **Analyze logs**:
   - Review log output to trace execution flow
   - Timestamps help identify performance bottlenecks
   - Identify point of failure or unexpected behavior

### Option C: Crash Dump Symbolication

1. **Extract crash dump**:
   - Switch may generate crash report when error handler is installed
   - Location typically: `/sdcard/Nintendo/Errors/reports/`

2. **Symbolicate addresses with aarch64-linux-gnu-addr2line**:
   ```bash
   docker compose run switch-sdk bash -c \
     'aarch64-linux-gnu-addr2line -e out/build/switch-debug/keeperfx <address>'
   ```

   Example:
   ```bash
   docker compose run switch-sdk bash -c \
     'aarch64-linux-gnu-addr2line -e out/build/switch-debug/keeperfx 0x7100123456'
   # Output: src/game.c:456
   ```

## GDB Commands Reference

Common commands when debugging:

```gdb
# Connection
target remote <switch-ip>:4242          # Connect to Switch GDB stub
disconnect                              # Close connection

# Execution
break function_name                     # Set breakpoint at function
break src/file.c:123                    # Set breakpoint at line
continue                                # Resume execution
step                                    # Step into function
next                                    # Step over function
finish                                  # Run until return

# Inspection
info registers                          # Show aarch64 register values
info threads                            # Show all threads
thread <n>                              # Switch to thread N
print variable_name                     # Print variable value
p *array_ptr@10                         # Print array of 10 elements
backtrace                               # Show call stack

# Memory Inspection
x/16xb 0x7100000000                    # Dump 16 bytes at address as hex
dump memory output.bin 0x7100000000 0x7110000000  # Dump memory region to file
```

## Build Artifacts for Debugging

- `out/build/switch-debug/keeperfx.elf` — ELF executable with full debug symbols
- `out/build/switch-debug/keeperfx.nsp` — NSP executable (loadable on Switch)
- `out/build/switch-reldebug/keeperfx.elf` — ELF with optimized code but retained symbols

## Tools Available

- `aarch64-linux-gnu-gdb` — GNU Debugger for ARMv8 (in Docker container)
- `aarch64-linux-gnu-addr2line` — Convert addresses to source file:line
- `aarch64-linux-gnu-readelf` — Read ELF symbol table and debug info
- `aarch64-linux-gnu-nm` — List symbols in ELF file

## Logging Best Practices

### Log Levels

```c
#define LOG_ERROR   0  /* Critical errors */
#define LOG_WARN    1  /* Warnings */
#define LOG_INFO    2  /* General information */
#define LOG_DEBUG   3  /* Debug-level verbosity */

#ifdef DEBUG
#define DLOG(level, fmt, ...) \
    if (level <= LOG_DEBUG) { \
        FILE *f = fopen("/sdcard/keeperfx.log", "a"); \
        if (f) { \
            fprintf(f, "[%s] " fmt "\n", #level, ##__VA_ARGS__); \
            fclose(f); \
        } \
    }
#else
#define DLOG(...)  /* noop in release */
#endif
```

### Performance Considerations

- File I/O on every log statement can impact performance
- Consider buffering logs in memory and flushing periodically
- Use conditional logging for expensive operations

## Troubleshooting

### "Unable to establish connection"
- Is the GDB stub running on Switch?
- Is the network connection established?
- Try pinging the Switch: `ping <switch-ip>`
- Check firewall rules for port 4242

### Breakpoints show as "unverified"
- Ensure you built with the **switch-debug** preset (`CMAKE_BUILD_TYPE: Debug`)
- Verify DWARF info: `docker compose run switch-sdk aarch64-linux-gnu-readelf --debug-dump=info out/build/switch-debug/keeperfx | head -50`

### Variables show as "optimized out"
- You're using a Release or RelWithDebInfo build
- Switch to **switch-debug** preset (`-O0 -g`) for full variable visibility

### Log files not being written
- Verify SD card is present and accessible
- Check file permissions and available space
- Ensure path is correct: `/sdcard/keeperfx_debug.log`

## Notes

- Network GDB debugging is the most reliable approach for Switch
- File-based logging provides comprehensive execution tracing
- Keep both `switch-debug` and `switch-reldebug` builds for different scenarios:
  - Use `switch-debug` for interactive GDB debugging when available
  - Use `switch-reldebug` for production profiling and crash analysis
  - Use file-based logging for comprehensive execution analysis and troubleshooting
- The Switch's more capable hardware makes debugging easier than other platforms
