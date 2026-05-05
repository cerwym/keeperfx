#ifndef PLATFORM_H
#define PLATFORM_H

#include <stddef.h>

// Platform-specific stuff is declared here

#ifdef __cplusplus
extern "C" {
#endif

int kfxmain(int argc, char *argv[]);
const char * get_os_version(void);
const void * get_image_base(void);
const char * get_wine_version(void);
const char * get_wine_host(void);

/** Returns the root directory where game data files are located.
 *  Desktop: directory of the executable.  Vita: "ux0:data/keeperfx".
 *  Do not free the returned pointer. */
const char * PlatformManager_GetDataPath(void);

/** Returns the directory where save files should be written.
 *  Do not free the returned pointer. */
const char * PlatformManager_GetSavePath(void);

/** Must be called once with argc/argv before any path queries.
 *  Desktop platforms use argv[0] to find the executable directory. */
void PlatformManager_SetArgv(int argc, char** argv);

/** Returns the size in bytes to allocate for the polygon pool.
 *  Desktop: 16MB (original), Vita: 4MB (reduced for BSS). */
size_t PlatformManager_GetPolyPoolSize(void);

/** Returns the refresh rate (Hz) of the display the game window is on, or 0 if unavailable. */
int PlatformManager_GetDisplayRefreshRate(void);

/******************************************************************************/
/* OpenGL context management (implemented per-platform for the GL backend)    */
/******************************************************************************/

/** Returns the address of an OpenGL function by name.
 *  Wraps SDL_GL_GetProcAddress on SDL2 platforms.
 *  Must only be called after a GL context has been created. */
void* platform_gl_get_proc_address(const char* proc);

/** Create an OpenGL context for the given SDL_Window.
 *  Sets the GL context as current on success.
 *  @return Non-zero on success, 0 on failure. */
int platform_create_gl_context(void *sdl_window);

/** Destroy the active OpenGL context. */
void platform_destroy_gl_context(void);

/** Present the back buffer to screen (SDL_GL_SwapWindow equivalent).
 *  @param sdl_window  The SDL_Window to swap. */
void platform_swap_gl_buffers(void *sdl_window);

/** Returns non-zero if RenderDoc is injected into this process.
 *  When true, Tracy GPU timer-query profiling must be disabled to prevent
 *  RenderDoc's per-frame GL state serialisation from corrupting Tracy's
 *  query pool and crashing in TracyGpuCollect. */
int platform_is_renderdoc_present(void);

#ifdef __cplusplus
}
#endif

#endif // PLATFORM_H
