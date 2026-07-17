/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file audio_interface.h
 *     Audio abstraction interface for platform portability.
 * @par Purpose:
 *     Provides a platform-independent interface for audio operations.
 * @par Comment:
 *     None.
 * @author   KeeperFX Team
 * @date     12 Feb 2026
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef AUDIO_INTERFACE_H
#define AUDIO_INTERFACE_H

#include "../bflib_basics.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/

typedef struct AudioInterface {
    TbResult (*init)(void);
    void (*shutdown)(void);
    void (*play_sample)(int id, long x, long y, long z, int volume);
    void (*play_music)(int track);
    void (*set_listener)(long x, long y, long z, int angle);
    void (*set_volume)(int master, int music, int sfx);
} AudioInterface;

extern AudioInterface* g_audio;

void audio_openal_initialize(void);
void audio_vita_initialize(void);
void audio_switch_initialize(void);

/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
