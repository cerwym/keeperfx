/*
 * KeeperFX custom vitaSHARk implementation.
 *
 * Problem: sceKernelLoadStartModule("ur0:data/external/libshacccg.suprx") fails
 * with SCE_KERNEL_ERROR_NO_FREE_PHYSICAL_PAGE because KeeperFX's large BSS
 * (~114 MB) exhausts the kernel's codemem pool before vitaSHARk can load the
 * 3.4 MB shader compiler.
 *
 * Fix: try all known paths; if ALL fail, skip module loading entirely and use
 * the SceShaccCg system module that is already imported via our eboot's import
 * stubs (loaded at process start, no extra physical pages required).
 *
 * If the system SceShaccCg doesn't support runtime compilation either, shader
 * compilation will fail gracefully later — but vitaGL will at least initialise.
 *
 * Based on vitaSHARk (LGPL-3.0) by Rinnegatamante.
 * https://github.com/Rinnegatamante/vitaSHARK
 */

#include <vitashark.h>
#include <stdlib.h>
#include <psp2/kernel/modulemgr.h>
#include <shacccg_ext.h>

static void (*shark_log_cb)(const char *msg, shark_log_level msg_level, int line) = NULL;
static shark_warn_level shark_warnings_level = SHARK_WARN_SILENT;

static SceUID shark_module_id = 0;
static uint8_t shark_initialized = 0;
static const SceShaccCgCompileOutput *shark_output = NULL;
static SceShaccCgSourceFile shark_input;
static SceShaccCgCallbackList shark_callbacks;
static SceShaccCgCompileOptions shark_options;
static SceShaccCgLocale shark_locale_mode = SCE_SHACCCG_ENGLISH;

static void *(*shark_malloc)(size_t size) = malloc;
static void (*shark_free)(void *ptr) = free;

static SceShaccCgSourceFile *shark_open_file_cb(
    const char *fileName,
    const SceShaccCgSourceLocation *includedFrom,
    const SceShaccCgCompileOptions *compileOptions,
    const char **errorString)
{
    return &shark_input;
}

void shark_set_allocators(void *(*malloc_func)(size_t size), void (*free_func)(void *ptr)) {
    shark_malloc = malloc_func;
    shark_free = free_func;
}

int shark_init(const char *path) {
    if (shark_initialized)
        return 0;

    /* Try every known path for libshacccg.suprx. */
    static const char *const fallback_paths[] = {
        "ur0:/data/libshacccg.suprx",
        "ur0:data/external/libshacccg.suprx",
        "vs0:sys/external/libshacccg.suprx",
        NULL
    };

    shark_module_id = -1;
    if (path) {
        SceUID uid = sceKernelLoadStartModule(path, 0, NULL, 0, NULL, NULL);
        if (uid >= 0) shark_module_id = uid;
    }
    for (int i = 0; shark_module_id < 0 && fallback_paths[i]; i++) {
        SceUID uid = sceKernelLoadStartModule(fallback_paths[i], 0, NULL, 0, NULL, NULL);
        if (uid >= 0) shark_module_id = uid;
    }

    /*
     * If all explicit loads failed, continue anyway: SceShaccCg is already
     * imported via our eboot's import table (SceShaccCg_stub / SceShaccCgExt_stub).
     * The system module may provide working shader compilation without needing
     * libshacccg.suprx to be separately mapped into our codemem pool.
     */

    sceShaccCgExtEnableExtensions();
    sceShaccCgSetDefaultAllocator(shark_malloc, shark_free);
    sceShaccCgInitializeCallbackList(&shark_callbacks, SCE_SHACCCG_TRIVIAL);
    shark_callbacks.openFile = shark_open_file_cb;
    shark_initialized = 1;
    return 0;
}

void shark_end(void) {
    if (!shark_initialized) return;
    sceShaccCgReleaseCompiler();
    sceShaccCgExtDisableExtensions();
    if (shark_module_id >= 0)
        sceKernelStopUnloadModule(shark_module_id, 0, NULL, 0, NULL, NULL);
    shark_initialized = 0;
}

void shark_install_log_cb(void (*cb)(const char *msg, shark_log_level msg_level, int line)) {
    shark_log_cb = cb;
}

void shark_set_warnings_level(shark_warn_level level) {
    shark_warnings_level = level;
}

void shark_clear_output(void) {
    if (shark_output) {
        sceShaccCgDestroyCompileOutput(shark_output);
        shark_output = NULL;
    }
}

void shark_set_locale(shark_locale locale) {
    shark_locale_mode = (SceShaccCgLocale)locale;
}

SceGxmProgram *shark_compile_shader_extended(
    const char *src, uint32_t *size, shark_type type, shark_opt opt,
    int32_t use_fastmath, int32_t use_fastprecision, int32_t use_fastint)
{
    if (!shark_initialized) return NULL;

    shark_input.fileName = "<built-in>";
    shark_input.text = src;
    shark_input.size = *size;

    sceShaccCgInitializeCompileOptions(&shark_options);
    shark_options.mainSourceFile = shark_input.fileName;
    shark_options.targetProfile = type;
    shark_options.entryFunctionName = "main";
    shark_options.macroDefinitions = NULL;
    shark_options.useFx = 1;
    shark_options.locale = shark_locale_mode;
    shark_options.warningLevel = shark_warnings_level;
    shark_options.optimizationLevel = opt;
    shark_options.useFastmath = use_fastmath;
    shark_options.useFastint = use_fastint;
    shark_options.useFastprecision = use_fastprecision;
    shark_options.pedantic = shark_warnings_level == SHARK_WARN_MAX ? SHARK_ENABLE : SHARK_DISABLE;
    shark_options.performanceWarnings = shark_warnings_level > SHARK_WARN_SILENT ? SHARK_ENABLE : SHARK_DISABLE;

    shark_output = sceShaccCgCompileProgram(&shark_options, &shark_callbacks, 0);
    if (shark_log_cb) {
        for (int i = 0; i < shark_output->diagnosticCount; ++i) {
            const SceShaccCgDiagnosticMessage *log = &shark_output->diagnostics[i];
            shark_log_cb(log->message, log->level, log->location ? log->location->lineNumber : -1);
        }
    }
    if (shark_output->programData) *size = shark_output->programSize;
    return (SceGxmProgram *)shark_output->programData;
}

SceGxmProgram *shark_compile_shader(const char *src, uint32_t *size, shark_type type) {
    return shark_compile_shader_extended(src, size, type,
        SHARK_OPT_DEFAULT, SHARK_DISABLE, SHARK_DISABLE, SHARK_DISABLE);
}

const SceShaccCgCompileOutput *shark_get_internal_compile_output(void) {
    return shark_output;
}
