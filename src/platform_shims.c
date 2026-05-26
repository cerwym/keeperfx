/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file platform_shims.c
 *     No-op stubs for desktop-only features excluded from homebrew platform builds.
 * @par Purpose:
 *     Provides empty implementations of functions whose full source files
 *     depend on libraries unavailable on Vita/3DS/Switch (SDL_net, ffmpeg,
 *     miniupnpc, Lua, spng, etc.).  Compiled only on homebrew platforms.
 */
/******************************************************************************/
#include "pre_inc.h"

#if defined(PLATFORM_VITA) || defined(PLATFORM_3DS) || defined(PLATFORM_SWITCH)

#include <stdint.h>
#include "api.h"
#include "sounds.h"
#include "bflib_sndlib.h"
#include "bflib_sound.h"
#include "bflib_fmvids.h"
#include "globals.h"
#include "moonphase.h"
#include "custom_sprites.h"
#include "creature_graphics.h"
#include "bflib_sprite.h"
#include "engine_render.h"
#include "engine_arrays.h"
#include "scrcapt.h"
#include "audio/audio_interface.h"
#include "input/input_interface.h"

/* api.c stubs (SDL_net removed) */
int api_init_server(void) { return 0; }
void api_update_server(void) {}
void api_close_server(void) {}
void api_event(const char *event_name) { (void)event_name; }

/* sounds.h streaming stubs (SDL_mixer/bflib_sndlib removed) */
TbBool play_streamed_sample(const char *fname, SoundVolume vol) { (void)fname; (void)vol; return 0; }
void set_streamed_sample_volume(SoundVolume vol) { (void)vol; }
void stop_streamed_samples(void) {}

/* bflib_sndlib.cpp stubs (3DS/Switch only — Vita provides real impls in audio_vita.c) */
#ifndef PLATFORM_VITA
void FreeAudio(void) {}
TbBool play_music(const char *fname) { (void)fname; return 0; }
TbBool play_music_track(int t) { (void)t; return 0; }
void resume_music(void) {}
void stop_music(void) {}
#endif /* !PLATFORM_VITA */

/* steam_api.cpp stubs — steam_api.hpp uses extern "C" so define in C */
int steam_api_init(void) { return 0; }
void steam_api_shutdown(void) {}

/* moonphase.c stubs— astronomy library not available on homebrew */
short is_full_moon = 0;
short is_near_full_moon = 0;
short is_new_moon = 0;
short is_near_new_moon = 0;
short calculate_moon_phase(short do_calculate, short add_to_log) { (void)do_calculate; (void)add_to_log; return 0; }

/* audio_openal.c / input_sdl.c global interface pointers */
AudioInterface* g_audio = NULL;
InputInterface* g_input = NULL;

/* bflib_sndlib.cpp — additional sound engine stubs (3DS/Switch only) */
#ifndef PLATFORM_VITA
void SetSoundMasterVolume(SoundVolume v) { (void)v; }
TbBool GetSoundInstalled(void) { return 0; }
void MonitorStreamedSoundTrack(void) {}
void *GetSoundDriver(void) { return NULL; }
void StopAllSamples(void) {}
TbBool InitAudio(const struct SoundSettings *s) { (void)s; return 0; }
TbBool IsSamplePlaying(SoundMilesID id) { (void)id; return 0; }
void SetSampleVolume(SoundEmitterID e, SoundSmplTblID t, SoundVolume v) { (void)e; (void)t; (void)v; }
void SetSamplePan(SoundEmitterID e, SoundSmplTblID t, SoundPan p) { (void)e; (void)t; (void)p; }
void SetSamplePitch(SoundEmitterID e, SoundSmplTblID t, SoundPitch p) { (void)e; (void)t; (void)p; }
void toggle_bbking_mode(void) {}
void set_music_volume(SoundVolume v) { (void)v; }
void pause_music(void) {}

/* bflib_sound.h — sample playback stubs (defined in bflib_sndlib.cpp) */
SoundMilesID play_sample(SoundEmitterID e, SoundSmplTblID t, SoundVolume v, SoundPan p, SoundPitch pi, char rep, unsigned char ctype, SoundBankID b) { (void)e; (void)t; (void)v; (void)p; (void)pi; (void)rep; (void)ctype; (void)b; return 0; }
void stop_sample(SoundEmitterID e, SoundSmplTblID t, SoundBankID b) { (void)e; (void)t; (void)b; }
SoundSFXID get_sample_sfxid(SoundSmplTblID t, SoundBankID b) { (void)t; (void)b; return 0; }
#endif /* !PLATFORM_VITA */

/* bflib_fmvids.cpp — video playback stub (Vita uses real bflib_fmvids.cpp with FFmpeg-vita) */
#ifndef PLATFORM_VITA
TbBool play_smk(const char *filename, int flags) { (void)filename; (void)flags; return 0; }
#endif

/* scrcapt.c — screenshot / movie stubs */
unsigned char screenshot_format = 1;
TbBool movie_record_start(void) { return 0; }
TbBool movie_record_stop(void) { return 0; }
TbBool take_screenshot(char *fname) { (void)fname; return 0; }
TbBool perform_any_screen_capturing(void) { return 0; }
TbBool cumulative_screen_shot(void) { return 0; }

// On constrained platforms (Vita/3DS/Switch) big_scratch is allocated in
// PlatformVita/3DS/Switch SystemInit. On PC it is a static array in custom_sprites.c.
unsigned char *big_scratch = NULL;

// Sprite table arrays used on all platforms (also referenced by Vita sprite cache).
short iso_td_add[KEEPERSPRITE_ADD_NUM];
short td_iso_add[KEEPERSPRITE_ADD_NUM];

#ifndef PLATFORM_VITA
struct TbSpriteSheet *gui_panel_sprites = NULL;
short bad_icon_id = INT16_MAX;
int total_sprite_zip_count = 0;
TbSpriteData keepersprite_add[KEEPERSPRITE_ADD_NUM] = { 0 };
struct KeeperSprite creature_table_add[KEEPERSPRITE_ADD_NUM] = { {0} };

const struct TbSprite *get_panel_sprite(short sprite_idx) { (void)sprite_idx; return NULL; }
const struct TbSprite *get_button_sprite(short sprite_idx) { (void)sprite_idx; return NULL; }
const struct TbSprite *get_button_sprite_for_player(short sprite_idx, PlayerNumber plyr_idx) { (void)sprite_idx; (void)plyr_idx; return NULL; }
const struct TbSprite *get_frontend_sprite(short sprite_idx) { (void)sprite_idx; return NULL; }
const struct TbSprite *get_new_icon_sprite(short sprite_idx) { (void)sprite_idx; return NULL; }
short get_icon_id(const char *name) { (void)name; return 0; }
short get_anim_id_(const char *name) { (void)name; return 0; }
short get_anim_id(const char *name, struct ObjectConfigStats *objst) { (void)name; (void)objst; return 0; }
const struct LensOverlayData *get_lens_overlay_data(const char *name) { (void)name; return NULL; }
const struct LensMistData *get_lens_mist_data(const char *name) { (void)name; return NULL; }
#endif /* !PLATFORM_VITA */

/* input_sdl.c / audio_openal.c — init functions */
void input_sdl_initialize(void) {}
void audio_openal_initialize(void) {}

/* bflib_sndlib.cpp — missing volume getter */
#ifndef PLATFORM_VITA
SoundVolume GetCurrentSoundMasterVolume(void) { return 0; }
#endif

/* custom_sprites.c — sprite system init stubs (3DS/Switch only; see above TODO) */
#ifndef PLATFORM_VITA
void init_custom_sprites(LevelNumber level_no) { (void)level_no; }
int  is_custom_icon(short icon_idx) { (void)icon_idx; return 0; }
#endif /* !PLATFORM_VITA */

/* Lua stubs— only needed on platforms where LuaJIT is not available (3DS, Switch).
 * On Vita, LuaJIT-Vita (SonicMastr fork) is used and lua source files are compiled normally.
 * On 3DS/Switch, build LuaJIT 2.1 with LUAJIT_DISABLE_JIT for devkitARM/devkitA64 to lift these. */
#ifndef KEEPERFX_LUA_AVAILABLE
#include "config.h"
#include "lua_base.h"
#include "lua_triggers.h"
#include "lua_cfg_funcs.h"
#include "console_cmd.h"

TbBool open_lua_script(LevelNumber lvnum) { (void)lvnum; return 0; }
void close_lua_script(void) {}
const char* get_lua_serialized_data(size_t *len) { if (len) *len = 0; return NULL; }
void set_lua_serialized_data(const char* data, size_t len) { (void)data; (void)len; }
TbBool execute_lua_code_from_console(const char* code) { (void)code; return 0; }
TbBool execute_lua_code_from_script(const char* code) { (void)code; return 0; }
const char* lua_get_serialised_data(size_t *len) { if (len) *len = 0; return NULL; }
void lua_set_serialised_data(const char *data, size_t len) { (void)data; (void)len; }
void cleanup_serialized_data(void) {}
void lua_set_random_seed(unsigned int seed) { (void)seed; }
void generate_lua_types_file(void) {}

void lua_on_chatmsg(PlayerNumber plyr_idx, char *msg) { (void)plyr_idx; (void)msg; }
void lua_on_game_start(void) {}
void lua_on_game_tick(void) {}
void lua_on_power_cast(PlayerNumber plyr_idx, PowerKind pwkind, unsigned short splevel, MapSubtlCoord stl_x, MapSubtlCoord stl_y, struct Thing *thing) { (void)plyr_idx; (void)pwkind; (void)splevel; (void)stl_x; (void)stl_y; (void)thing; }
void lua_on_special_box_activate(PlayerNumber plyr_idx, struct Thing *cratetng) { (void)plyr_idx; (void)cratetng; }
void lua_on_dungeon_destroyed(PlayerNumber plyr_idx) { (void)plyr_idx; }
void lua_on_creature_death(struct Thing *crtng) { (void)crtng; }
void lua_on_creature_rebirth(struct Thing *crtng) { (void)crtng; }
void lua_on_trap_placed(struct Thing *traptng) { (void)traptng; }
void lua_on_apply_damage_to_thing(struct Thing *thing, HitPoints dmg, PlayerNumber dealing_plyr_idx) { (void)thing; (void)dmg; (void)dealing_plyr_idx; }
void lua_on_level_up(struct Thing *thing) { (void)thing; }

short luafunc_crstate_func(FuncIdx func_idx, struct Thing *thing) { (void)func_idx; (void)thing; return 0; }
short luafunc_thing_update_func(FuncIdx func_idx, struct Thing *thing) { (void)func_idx; (void)thing; return 0; }
TbResult luafunc_magic_use_power(FuncIdx func_idx, PlayerNumber plyr_idx, PowerKind pwkind,
    unsigned short splevel, MapSubtlCoord stl_x, MapSubtlCoord stl_y, struct Thing *thing, unsigned long allow_flags)
    { (void)func_idx; (void)plyr_idx; (void)pwkind; (void)splevel; (void)stl_x; (void)stl_y; (void)thing; (void)allow_flags; return Lb_FAIL; }
FuncIdx get_function_idx(const char *func_name, const struct NamedCommand *Cfuncs) { (void)func_name; (void)Cfuncs; return 0; }

void cmd_auto_completion(PlayerNumber plyr_idx, char *cmd_str, size_t cmd_size) { (void)plyr_idx; (void)cmd_str; (void)cmd_size; }
TbBool cmd_exec(PlayerNumber plyr_idx, char *msg) { (void)plyr_idx; (void)msg; return 0; }
#endif // !KEEPERFX_LUA_AVAILABLE

#endif // PLATFORM_VITA || PLATFORM_3DS || PLATFORM_SWITCH

#include "post_inc.h"
