/*
 * vita-uvdb — intra-process GDB remote stub for PS Vita
 * Original: https://github.com/sleirsgoevy/vita-uvdb (sleirsgoevy)
 * Fork: breakpoint (Z0/z0), single-step (s), vCont, detach (D) support
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* uvdb_enter acts as a software breakpoint.  On first hit the program
 * blocks until GDB connects on TCP port 1234.  On subsequent hits it
 * acts as a software breakpoint (SIGTRAP). */
void uvdb_enter(void);

/* GDB remote-syscall API — call whitelisted syscalls on the host.
 * Example: uvdb_remote_syscall("write", 3, 1, "Hello\n", 6); */
int uvdb_remote_syscall(const char* name, int nargs, ... /* int arg1, int arg2, ... */);

/* Redirect stdout/stderr through uvdb_remote_syscall.
 * Note: uses newlib APIs, not SCE ones. */
int uvdb_redirect_stdio(void);

#ifdef __cplusplus
}
#endif
