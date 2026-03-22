/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file audio_vita.c
 *     PlayStation Vita audio implementation — one DMA port + software mixer.
 * @par Purpose:
 *     Loads .dat sound banks (same binary format as bflib_sndlib.cpp), decodes
 *     embedded WAV files to S16 mono, and mixes them into 48 kHz stereo PCM
 *     via a background DMA thread.  OGG music streaming via libvorbisfile when
 *     VITA_HAVE_VORBIS is defined (set by CMake when libvorbis/libogg are installed).
 * @author   KeeperFX Team
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "kfx_memory.h"
#include "../pre_inc.h"
#include "audio_interface.h"

#ifdef PLATFORM_VITA

#include <psp2/audioout.h>
#include <psp2/kernel/threadmgr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <malloc.h>

#ifdef VITA_HAVE_VORBIS
#include <vorbis/vorbisfile.h>
#endif

#include "../bflib_sndlib.h"
#include "../bflib_sound.h"
#include "../bflib_fileio.h"
#include "../config.h"
#include "../config_keeperfx.h"
#include "../globals.h"
#include "../post_inc.h"

/* ── Constants ──────────────────────────────────────────────────────────── */
#define VITA_OUT_RATE     48000
#define VITA_DMA_GRAIN    1024        /* samples per DMA output call */
#define VITA_MAX_CHANNELS 16          /* concurrent SFX voices */
#define VITA_NUM_BANKS    2           /* 0 = sound.dat, 1 = speech_*.dat */
#define VITA_MAX_SAMPLES  4096        /* max samples per bank */

/* ── Sound bank binary format (mirrors bflib_sndlib.cpp structs) ─────────── */
#pragma pack(1)
typedef struct { uint8_t sig[14]; uint32_t ver; } VitaBankHead;
typedef struct {
    uint32_t first_sample_offset;
    uint32_t first_data_offset;
    uint32_t total_samples_size;
    uint32_t entries_count;
} VitaBankEntry;
typedef struct {
    char     filename[18];
    uint32_t data_offset;
    uint32_t sample_rate;
    uint32_t data_size;
    uint8_t  sfxid;
    uint8_t  format_flags;
} VitaBankSample;
#pragma pack()

/* ── WAV parsing helpers ─────────────────────────────────────────────────── */
#define FOURCC(a,b,c,d) ((uint32_t)(a)|((uint32_t)(b)<<8)|((uint32_t)(c)<<16)|((uint32_t)(d)<<24))
#define RIFF_RIFF FOURCC('R','I','F','F')
#define RIFF_WAVE FOURCC('W','A','V','E')
#define RIFF_FMT  FOURCC('f','m','t',' ')
#define RIFF_DATA FOURCC('d','a','t','a')
#define WAVE_FORMAT_PCM   1
#define WAVE_FORMAT_ADPCM 2

#pragma pack(1)
typedef struct { uint32_t tag; uint32_t size; } RiffChunk;
typedef struct {
    uint16_t format;
    uint16_t channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
} WaveFmt;
#pragma pack()

/* ── Decoded per-sample data ─────────────────────────────────────────────── */
typedef struct {
    int16_t  *data;       /* S16LE mono PCM, NULL until decoded */
    uint32_t  len;        /* number of int16_t samples */
    uint32_t  src_rate;   /* original sample rate for resampling */
    uint8_t   sfxid;
    /* streaming fields — file offset/length instead of a RAM blob */
    uint32_t  raw_file_offset; /* byte offset of WAV in bank file */
    uint32_t  raw_file_len;    /* bytes to read (data_size + WAV header room) */
    uint8_t   decoded;  /* 0 = not yet decoded, 1 = data/len are valid */
} VitaSample;

/* ── Software mixer channel ──────────────────────────────────────────────── */
typedef struct {
    const int16_t  *pcm;
    uint32_t        pcm_len;
    float           fpos;     /* fractional sample position */
    float           fstep;    /* advance per output sample = src_rate/out_rate * pitch_scale */
    float           vol_l;
    float           vol_r;
    TbBool          loop;
    TbBool          active;
    SoundEmitterID  emitter_id;
    SoundSmplTblID  smpl_id;
    SoundBankID     bank_id;
    SoundMilesID    miles_id;
} SwChannel;

/* ── Module state ────────────────────────────────────────────────────────── */
static VitaSample  s_samples[VITA_NUM_BANKS][VITA_MAX_SAMPLES];
static int         s_sample_counts[VITA_NUM_BANKS];
static TbFileHandle s_bank_fh[VITA_NUM_BANKS];  /* open file handles for on-demand streaming */
static SwChannel   s_sw[VITA_MAX_CHANNELS];
static int32_t     s_mix_buf[VITA_DMA_GRAIN * 2];   /* 32-bit accumulator */
static int16_t     s_out_buf[VITA_DMA_GRAIN * 2];   /* final S16LE stereo */
static int         s_dma_port = -1;
static SceUID      s_dma_thread = -1;
static volatile int s_dma_running = 0;
static float       s_master_vol = 1.0f;
static SoundMilesID s_next_id = 1;
static TbBool      s_initialized = false;

/* ── Music state (used when VITA_HAVE_VORBIS is defined) ────────────────── */
#define MUSIC_GRAIN    2048
#define MUSIC_RATE     44100

#ifdef VITA_HAVE_VORBIS
typedef enum { MUS_NONE, MUS_PLAY, MUS_STOP, MUS_PAUSE, MUS_RESUME } MusicCmd;
static SceUID           s_music_thread  = -1;
static SceUID           s_music_sema    = -1;
static int              s_music_port    = -1;
static int16_t          s_music_buf[MUSIC_GRAIN * 2]; /* stereo */
static float            s_music_vol     = 0.5f;
static char             s_music_path[1024];
static volatile MusicCmd s_music_cmd    = MUS_NONE;
static volatile int     s_music_paused  = 0;
static volatile int     s_music_running = 0;
#endif

/* ─────────────────────────────────────────────────────────────────────────
   decode_wav_mem — decode one WAV from a memory buffer.
   Returns malloc'd S16LE mono data (caller owns it), NULL on error.
   Replaces the old FILE*-based decode_wav; no fseek/fread required.
   ───────────────────────────────────────────────────────────────────────── */
static int16_t *decode_wav_mem(const uint8_t *data, size_t size,
                                uint32_t *out_len, uint32_t *out_rate)
{
    if (size < sizeof(RiffChunk) + 4) return NULL;
    const uint8_t *p   = data;
    const uint8_t *end = data + size;

    RiffChunk riff;
    memcpy(&riff, p, sizeof(riff)); p += sizeof(riff);
    if (riff.tag != RIFF_RIFF || p >= end) return NULL;

    uint32_t wave_tag;
    memcpy(&wave_tag, p, 4); p += 4;
    if (wave_tag != RIFF_WAVE) return NULL;

    WaveFmt  fmt    = {0};
    uint8_t *raw    = NULL;
    uint32_t raw_sz = 0;

    while (p + sizeof(RiffChunk) <= end) {
        RiffChunk chunk;
        memcpy(&chunk, p, sizeof(chunk)); p += sizeof(chunk);
        if (p + chunk.size > end) chunk.size = (uint32_t)(end - p);

        if (chunk.tag == RIFF_FMT) {
            size_t to_read = sizeof(WaveFmt) < chunk.size ? sizeof(WaveFmt) : (size_t)chunk.size;
            memcpy(&fmt, p, to_read);
            p += chunk.size;
        } else if (chunk.tag == RIFF_DATA) {
            raw_sz = chunk.size;
            raw = (uint8_t *)KfxAlloc(raw_sz);
            if (!raw) return NULL;
            memcpy(raw, p, raw_sz);
            break;
        } else {
            p += chunk.size;
        }
    }
    if (!raw || fmt.format == 0) { KfxFree(raw); return NULL; }
    *out_rate = fmt.sample_rate;

    /* 4-bit pseudo-ADPCM → S16 (matches bflib_sndlib.cpp) */
    if (fmt.format == WAVE_FORMAT_ADPCM && fmt.bits_per_sample == 4 && fmt.channels == 1) {
        uint32_t slen = raw_sz * 2;
        int16_t *out = (int16_t *)KfxAlloc(slen * sizeof(int16_t));
        if (!out) { KfxFree(raw); return NULL; }
        for (uint32_t i = 0; i < raw_sz; i++) {
            int hi = ((raw[i] >> 4) & 0xF) * 2;
            int lo = (raw[i] & 0x7) * 2;
            out[i*2+0] = (int16_t)((hi - 128) << 8);
            out[i*2+1] = (int16_t)((lo - 128) << 8);
        }
        KfxFree(raw); *out_len = slen; return out;
    }
    /* 8-bit unsigned mono → S16 */
    if (fmt.bits_per_sample == 8 && fmt.channels == 1) {
        int16_t *out = (int16_t *)KfxAlloc(raw_sz * sizeof(int16_t));
        if (!out) { KfxFree(raw); return NULL; }
        for (uint32_t i = 0; i < raw_sz; i++)
            out[i] = (int16_t)((raw[i] - 128) << 8);
        KfxFree(raw); *out_len = raw_sz; return out;
    }
    /* 16-bit signed mono — take ownership */
    if (fmt.bits_per_sample == 16 && fmt.channels == 1) {
        *out_len = raw_sz / 2;
        return (int16_t *)raw;
    }
    /* 8-bit stereo → downmix to mono S16 */
    if (fmt.bits_per_sample == 8 && fmt.channels == 2) {
        uint32_t slen = raw_sz / 2;
        int16_t *out = (int16_t *)KfxAlloc(slen * sizeof(int16_t));
        if (!out) { KfxFree(raw); return NULL; }
        for (uint32_t i = 0; i < slen; i++)
            out[i] = (int16_t)(((raw[i*2]-128) + (raw[i*2+1]-128)) << 7);
        KfxFree(raw); *out_len = slen; return out;
    }
    /* 16-bit stereo → downmix to mono S16 */
    if (fmt.bits_per_sample == 16 && fmt.channels == 2) {
        uint32_t slen = raw_sz / 4;
        int16_t *out = (int16_t *)KfxAlloc(slen * sizeof(int16_t));
        int16_t *src = (int16_t *)raw;
        if (!out) { KfxFree(raw); return NULL; }
        for (uint32_t i = 0; i < slen; i++)
            out[i] = (int16_t)((src[i*2] + src[i*2+1]) / 2);
        KfxFree(raw); *out_len = slen; return out;
    }
    KfxFree(raw);
    return NULL;
}

/* ─────────────────────────────────────────────────────────────────────────
   vita_load_sound_bank — parse one .dat bank and store per-sample file offsets.
   Reads only the small header + sample-table sections; the bulk WAV data is
   NOT loaded into RAM.  Saves 40 MB (sound.dat) + 5.4 MB (speech_eng.dat).
   ───────────────────────────────────────────────────────────────────────── */
static void vita_load_sound_bank(const char *filename, int bank_idx)
{
    const int DIR_IDX = 2;
    VitaSample *bank = s_samples[bank_idx];

    /* Release any previously decoded PCM and close old file handle. */
    for (int i = 0; i < VITA_MAX_SAMPLES; i++) {
        if (bank[i].decoded && bank[i].data) KfxFree(bank[i].data);
        memset(&bank[i], 0, sizeof(bank[i]));
    }
    if (s_bank_fh[bank_idx]) { LbFileClose(s_bank_fh[bank_idx]); s_bank_fh[bank_idx] = 0; }
    s_sample_counts[bank_idx] = 0;

    long file_size = LbFileLength(filename);
    if (file_size <= 0) { ERRORLOG("vita audio: empty file %s", filename); return; }

    TbFileHandle fh = LbFileOpen(filename, Lb_FILE_MODE_READ_ONLY);
    if (!fh) { ERRORLOG("vita audio: cannot open %s", filename); return; }

    /* Pass 1: read the last 4 bytes to find the bank header offset. */
    uint32_t head_offset = 0;
    LbFileSeek(fh, file_size - 4, Lb_FILE_SEEK_BEGINNING);
    if (LbFileRead(fh, &head_offset, 4) != 4) {
        LbFileClose(fh); ERRORLOG("vita audio: cannot read header offset in %s", filename); return;
    }

    /* Pass 2: read the bank header + entry table at head_offset. */
    size_t hdr_region_size = sizeof(VitaBankHead) + sizeof(VitaBankEntry) * 9;
    if (head_offset + hdr_region_size > (size_t)file_size) {
        LbFileClose(fh); ERRORLOG("vita audio: header region out of range in %s", filename); return;
    }
    uint8_t hdrbuf[sizeof(VitaBankHead) + sizeof(VitaBankEntry) * 9];
    LbFileSeek(fh, (long)head_offset, Lb_FILE_SEEK_BEGINNING);
    if (LbFileRead(fh, hdrbuf, (unsigned long)hdr_region_size) != (int)hdr_region_size) {
        LbFileClose(fh); ERRORLOG("vita audio: cannot read bank header in %s", filename); return;
    }

    const uint8_t *p = hdrbuf;
    p += sizeof(VitaBankHead); /* skip signature */
    VitaBankEntry bentries[9];
    memcpy(bentries, p, sizeof(bentries));

    VitaBankEntry *dir = &bentries[DIR_IDX];
    if (dir->first_sample_offset == 0 || dir->total_samples_size < sizeof(VitaBankSample)) {
        LbFileClose(fh); ERRORLOG("vita audio: invalid bank directory in %s", filename); return;
    }
    if (dir->first_sample_offset + dir->total_samples_size > (uint32_t)file_size) {
        LbFileClose(fh); ERRORLOG("vita audio: sample table out of range in %s", filename); return;
    }

    /* Pass 3: read the sample table (may be anywhere in the file). */
    int count = (int)(dir->total_samples_size / sizeof(VitaBankSample));
    if (count >= VITA_MAX_SAMPLES) count = VITA_MAX_SAMPLES - 1;

    VitaBankSample *sample_table = (VitaBankSample*)KfxAlloc((size_t)count * sizeof(VitaBankSample));
    if (!sample_table) {
        LbFileClose(fh); ERRORLOG("vita audio: OOM for sample table of %s", filename); return;
    }
    LbFileSeek(fh, (long)dir->first_sample_offset, Lb_FILE_SEEK_BEGINNING);
    if (LbFileRead(fh, sample_table, (unsigned long)((size_t)count * sizeof(VitaBankSample)))
            != (int)((size_t)count * sizeof(VitaBankSample))) {
        KfxFree(sample_table); LbFileClose(fh);
        ERRORLOG("vita audio: cannot read sample table in %s", filename); return;
    }

    /* Store per-sample file offsets — no WAV data loaded into RAM. */
    for (int i = 0; i < count; i++) {
        VitaBankSample *hdr = &sample_table[i];
        uint32_t wav_off = dir->first_data_offset + hdr->data_offset;
        if (wav_off >= (uint32_t)file_size) {
            WARNLOG("vita audio: sample %d offset out of range in %s", i, filename); continue;
        }

        bank[i].raw_file_offset = wav_off;
        /* data_size is the PCM payload; add room for RIFF/fmt/data chunk headers (~64 B) */
        bank[i].raw_file_len    = hdr->data_size + 64;
        bank[i].src_rate        = hdr->sample_rate;
        bank[i].sfxid           = hdr->sfxid;
        bank[i].decoded         = 0;
    }

    KfxFree(sample_table);
    /* Keep file open for on-demand seek+read when each sample is first played. */
    s_bank_fh[bank_idx] = fh;
    s_sample_counts[bank_idx] = count;
    JUSTLOG("vita audio: loaded %d samples from %s (streaming)", count, filename);
}

/* ─────────────────────────────────────────────────────────────────────────
   vita_dma_thread — mixes all active SwChannels into VITA_DMA_GRAIN stereo
   samples and pushes them to the single DMA port.
   ───────────────────────────────────────────────────────────────────────── */
static int vita_dma_thread(SceSize args, void *argp)
{
    (void)args; (void)argp;
    sceKernelChangeThreadVfpException(0x0800009FU, 0x0);

    while (s_dma_running) {
        memset(s_mix_buf, 0, sizeof(s_mix_buf));

        for (int c = 0; c < VITA_MAX_CHANNELS; c++) {
            SwChannel *ch = &s_sw[c];
            if (!ch->active) continue;
            for (int s = 0; s < VITA_DMA_GRAIN; s++) {
                uint32_t idx = (uint32_t)ch->fpos;
                if (idx >= ch->pcm_len) {
                    if (ch->loop) { ch->fpos = 0.0f; idx = 0; }
                    else          { ch->active = false; break; }
                }
                int32_t smp = ch->pcm[idx];
                s_mix_buf[s*2+0] += (int32_t)((float)smp * ch->vol_l);
                s_mix_buf[s*2+1] += (int32_t)((float)smp * ch->vol_r);
                ch->fpos += ch->fstep;
            }
        }

        for (int s = 0; s < VITA_DMA_GRAIN * 2; s++) {
            int32_t v = s_mix_buf[s];
            if (v >  32767) v =  32767;
            if (v < -32768) v = -32768;
            s_out_buf[s] = (int16_t)v;
        }
        sceAudioOutOutput(s_dma_port, s_out_buf);
    }
    return sceKernelExitDeleteThread(0);
}

/* ─────────────────────────────────────────────────────────────────────────
   vita_music_thread — OGG streaming via vorbisidec (Tremor).
   Activated only when VITA_HAVE_VORBIS is set by CMake.
   ───────────────────────────────────────────────────────────────────────── */
#ifdef VITA_HAVE_VORBIS
static int vita_music_thread(SceSize args, void *argp)
{
    (void)args; (void)argp;
    sceKernelChangeThreadVfpException(0x0800009FU, 0x0);

    while (s_music_running) {
        /* block until a play command arrives */
        sceKernelWaitSema(s_music_sema, 1, NULL);
        if (!s_music_running) break;
        if (s_music_cmd != MUS_PLAY) { s_music_cmd = MUS_NONE; continue; }
        s_music_cmd = MUS_NONE;
        s_music_paused = 0;

        OggVorbis_File vf;
        if (ov_fopen(s_music_path, &vf) < 0) {
            WARNLOG("vita music: cannot open %s", s_music_path);
            continue;
        }
        vorbis_info *vi = ov_info(&vf, -1);
        JUSTLOG("vita music: playing %s (%ld Hz, %d ch)", s_music_path,
                vi->rate, vi->channels);

        int bitstream = 0;
        int keep_going = 1;
        while (keep_going && s_music_running) {
            MusicCmd cmd = s_music_cmd;
            if (cmd == MUS_STOP) {
                s_music_cmd = MUS_NONE; keep_going = 0; break;
            } else if (cmd == MUS_PLAY) {
                /* new track — signal sema so we loop back to top */
                s_music_cmd = MUS_NONE; keep_going = 0;
                sceKernelSignalSema(s_music_sema, 1);
                break;
            } else if (cmd == MUS_PAUSE) {
                s_music_cmd = MUS_NONE; s_music_paused = 1;
            } else if (cmd == MUS_RESUME) {
                s_music_cmd = MUS_NONE; s_music_paused = 0;
            }

            if (s_music_paused) {
                memset(s_music_buf, 0, sizeof(s_music_buf));
                sceAudioOutOutput(s_music_port, s_music_buf);
                continue;
            }

            /* decode one grain worth of stereo S16 */
            char *dst = (char *)s_music_buf;
            int remaining = (int)sizeof(s_music_buf);
            while (remaining > 0) {
                long got = ov_read(&vf, dst, remaining, 0, 2, 1, &bitstream);
                if (got < 0) { remaining = 0; break; } /* decode error */
                if (got == 0) { ov_pcm_seek(&vf, 0); continue; } /* EOF → loop */
                dst += got; remaining -= (int)got;
            }

            /* apply music volume */
            int n = (int)(sizeof(s_music_buf) / sizeof(s_music_buf[0]));
            for (int i = 0; i < n; i++)
                s_music_buf[i] = (int16_t)((float)s_music_buf[i] * s_music_vol);

            sceAudioOutOutput(s_music_port, s_music_buf);
        }
        ov_clear(&vf);
    }
    return sceKernelExitDeleteThread(0);
}
#endif /* VITA_HAVE_VORBIS */

/* ═══════════════════════════════════════════════════════════════════════════
   bflib_sndlib.h implementations
   ═══════════════════════════════════════════════════════════════════════════ */

TbBool InitAudio(const struct SoundSettings *settings)
{
    (void)settings;
    if (s_initialized) return true;

    /* build bank file paths (mirrors load_sound_banks in bflib_sndlib.cpp) */
    char snd_path[2048];
    prepare_file_path_buf(snd_path, sizeof(snd_path), FGrp_LrgSound, "sound.dat");

    char spc_path[2048];
    const char *spc_auto = get_game_file_path_fmt(FGrp_LrgSound, "speech_%s.dat",
                                                 get_language_lwrstr(install_info.lang_id));
    if (!spc_auto || !LbFileExists(spc_auto))
        spc_auto = prepare_file_path(FGrp_LrgSound, "speech.dat");
    if (!spc_auto || !LbFileExists(spc_auto))
        spc_auto = get_game_file_path_fmt(FGrp_LrgSound, "speech_%s.dat",
                                        get_language_lwrstr(1));
    snprintf(spc_path, sizeof(spc_path), "%s", spc_auto != NULL ? spc_auto : "");

    vita_load_sound_bank(snd_path, 0);
    vita_load_sound_bank(spc_path, 1);

    /* open a single stereo DMA port */
    s_dma_port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_MAIN,
                                     VITA_DMA_GRAIN, VITA_OUT_RATE,
                                     SCE_AUDIO_OUT_MODE_STEREO);
    if (s_dma_port < 0) {
        ERRORLOG("vita audio: sceAudioOutOpenPort failed (%d)", s_dma_port);
        return false;
    }

    memset(s_sw, 0, sizeof(s_sw));
    s_dma_running = 1;
    s_dma_thread  = sceKernelCreateThread("vita_dma", vita_dma_thread,
                                          0x40,         /* user real-time priority */
                                          64 * 1024,    /* 64 KB stack */
                                          0, 0, NULL);
    if (s_dma_thread < 0) {
        ERRORLOG("vita audio: sceKernelCreateThread failed (%d)", (int)s_dma_thread);
        s_dma_running = 0;
        sceAudioOutReleasePort(s_dma_port);
        s_dma_port = -1;
        return false;
    }
    sceKernelStartThread(s_dma_thread, 0, NULL);

#ifdef VITA_HAVE_VORBIS
    /* open a separate port for BGM streaming */
    s_music_port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_MAIN,
                                       MUSIC_GRAIN, MUSIC_RATE,
                                       SCE_AUDIO_OUT_MODE_STEREO);
    if (s_music_port >= 0) {
        s_music_sema   = sceKernelCreateSema("vita_music", 0, 0, 1, NULL);
        s_music_running = 1;
        s_music_thread  = sceKernelCreateThread("vita_music", vita_music_thread,
                                                0x60,          /* lower priority than SFX */
                                                64 * 1024,     /* 64 KB stack (same as DMA thread) */
                                                0, 0, NULL);
        if (s_music_thread >= 0)
            sceKernelStartThread(s_music_thread, 0, NULL);
        else {
            WARNLOG("vita audio: music thread create failed (0x%08X)", (unsigned int)s_music_thread);
            s_music_running = 0;
        }
    } else {
        WARNLOG("vita audio: music port open failed (%d)", s_music_port);
        s_music_port = -1;
    }
#endif

    s_initialized = true;
    JUSTLOG("vita audio: initialised (sfx_port=%d sfx_thread=%d)", s_dma_port, (int)s_dma_thread);
    return true;
}

void FreeAudio(void)
{
    if (!s_initialized) return;
#ifdef VITA_HAVE_VORBIS
    if (s_music_running) {
        s_music_running = 0;
        s_music_cmd = MUS_STOP;
        sceKernelSignalSema(s_music_sema, 1);
        sceKernelWaitThreadEnd(s_music_thread, NULL, NULL);
        sceKernelDeleteSema(s_music_sema);
        sceAudioOutReleasePort(s_music_port);
        s_music_thread = -1; s_music_port = -1; s_music_sema = -1;
    }
#endif
    s_dma_running = 0;
    sceKernelWaitThreadEnd(s_dma_thread, NULL, NULL);
    s_dma_thread = -1;
    sceAudioOutReleasePort(s_dma_port);
    s_dma_port = -1;
    for (int b = 0; b < VITA_NUM_BANKS; b++) {
        for (int i = 0; i < VITA_MAX_SAMPLES; i++) {
            if (s_samples[b][i].decoded && s_samples[b][i].data)
                KfxFree(s_samples[b][i].data);
        }
        if (s_bank_fh[b]) { LbFileClose(s_bank_fh[b]); s_bank_fh[b] = 0; }
        memset(s_samples[b], 0, sizeof(s_samples[b]));
    }
    s_initialized = false;
}

TbBool GetSoundInstalled(void) { return s_initialized; }

void SetSoundMasterVolume(SoundVolume volume) { s_master_vol = (float)volume / 256.0f; }

SoundVolume GetCurrentSoundMasterVolume(void) { return (SoundVolume)(s_master_vol * 256.0f); }

/* ─────────────────────────────────────────────────────────────────────────
   vita_ensure_sample_decoded — decode a sample on first use (streaming).
   Seeks to the sample's position in the open bank file, reads just that
   sample's WAV bytes into a temp buffer, decodes, then frees the temp buffer.
   No-op if already decoded or slot is empty. Called from play_sample.
   ───────────────────────────────────────────────────────────────────────── */
static void vita_ensure_sample_decoded(int bank_idx, int sample_idx)
{
    VitaSample *smp = &s_samples[bank_idx][sample_idx];
    if (smp->decoded) return;
    if (smp->raw_file_len == 0) return;

    TbFileHandle fh = s_bank_fh[bank_idx];
    if (!fh) {
        WARNLOG("vita audio: bank %d file not open for sample %d", bank_idx, sample_idx);
        return;
    }

    uint8_t *tmpbuf = (uint8_t*)KfxAlloc(smp->raw_file_len);
    if (!tmpbuf) {
        WARNLOG("vita audio: OOM reading sample bank %d idx %d", bank_idx, sample_idx);
        return;
    }

    LbFileSeek(fh, (long)smp->raw_file_offset, Lb_FILE_SEEK_BEGINNING);
    long nread = LbFileRead(fh, tmpbuf, smp->raw_file_len);
    if (nread <= 0) {
        KfxFree(tmpbuf);
        WARNLOG("vita audio: read failed bank %d sample %d", bank_idx, sample_idx);
        return;
    }

    uint32_t pcm_len = 0, pcm_rate = 0;
    int16_t *pcm = decode_wav_mem(tmpbuf, (size_t)nread, &pcm_len, &pcm_rate);
    KfxFree(tmpbuf);

    if (!pcm) {
        WARNLOG("vita audio: lazy decode failed for bank %d sample %d", bank_idx, sample_idx);
        return;
    }
    smp->data    = pcm;
    smp->len     = pcm_len;
    if (pcm_rate) smp->src_rate = pcm_rate;
    smp->decoded = 1;
}

SoundMilesID play_sample(SoundEmitterID emit_id, SoundSmplTblID smpl_id,
                          SoundVolume vol, SoundPan pan, SoundPitch pitch,
                          char repeats, unsigned char ctype, SoundBankID bank_id)
{
    (void)ctype;
    if (!s_initialized || emit_id <= 0 || smpl_id == 0) return 0;
    if (bank_id >= VITA_NUM_BANKS) return 0;
    if (smpl_id < 0 || smpl_id >= s_sample_counts[bank_id]) return 0;
    VitaSample *smp = &s_samples[bank_id][smpl_id];
    vita_ensure_sample_decoded(bank_id, smpl_id);
    if (!smp->data) return 0;

    int c = -1;
    for (int i = 0; i < VITA_MAX_CHANNELS; i++)
        if (!s_sw[i].active) { c = i; break; }
    if (c < 0) return 0;

    SwChannel *ch = &s_sw[c];
    ch->pcm     = smp->data;
    ch->pcm_len = smp->len;
    ch->fpos    = 0.0f;
    /* pitch: 100 = normal speed */
    ch->fstep = ((float)smp->src_rate / (float)VITA_OUT_RATE) * ((float)pitch / 100.0f);
    /* pan: 0..128 where 64 = centre (matches bflib_sndlib.cpp formula) */
    float fvol = ((float)vol / 256.0f) * s_master_vol;
    float sep  = (-(float)(64 - pan) / 64.0f) * 0.5f;  /* -0.5..0.5, reduced separation */
    ch->vol_l  = fvol * (1.0f - (sep > 0.0f ? sep : 0.0f));
    ch->vol_r  = fvol * (1.0f + (sep < 0.0f ? sep : 0.0f));
    ch->loop       = (repeats == -1);
    ch->emitter_id = emit_id;
    ch->smpl_id    = smpl_id;
    ch->bank_id    = bank_id;
    ch->miles_id   = s_next_id;
    if (++s_next_id == 0) s_next_id = 1;
    ch->active = true;
    return ch->miles_id;
}

void stop_sample(SoundEmitterID emit_id, SoundSmplTblID smpl_id, SoundBankID bank_id)
{
    for (int i = 0; i < VITA_MAX_CHANNELS; i++) {
        SwChannel *ch = &s_sw[i];
        if (ch->active && ch->emitter_id == emit_id &&
            ch->smpl_id == smpl_id && ch->bank_id == bank_id)
            ch->active = false;
    }
}

TbBool IsSamplePlaying(SoundMilesID id)
{
    if (!id) return false;
    for (int i = 0; i < VITA_MAX_CHANNELS; i++)
        if (s_sw[i].active && s_sw[i].miles_id == id) return true;
    return false;
}

void SetSampleVolume(SoundEmitterID emit_id, SoundSmplTblID smpl_id, SoundVolume vol)
{
    for (int i = 0; i < VITA_MAX_CHANNELS; i++) {
        SwChannel *ch = &s_sw[i];
        if (!ch->active || ch->emitter_id != emit_id || ch->smpl_id != smpl_id) continue;
        float fvol = ((float)vol / 256.0f) * s_master_vol;
        /* preserve existing pan ratio */
        float total = ch->vol_l + ch->vol_r;
        float lr = (total > 0.0f) ? (ch->vol_l / total) : 0.5f;
        ch->vol_l = fvol * lr;
        ch->vol_r = fvol * (1.0f - lr);
    }
}

void SetSamplePan(SoundEmitterID emit_id, SoundSmplTblID smpl_id, SoundPan pan)
{
    for (int i = 0; i < VITA_MAX_CHANNELS; i++) {
        SwChannel *ch = &s_sw[i];
        if (!ch->active || ch->emitter_id != emit_id || ch->smpl_id != smpl_id) continue;
        float fvol = (ch->vol_l + ch->vol_r) / 2.0f;
        float sep  = (-(float)(64 - pan) / 64.0f) * 0.5f;
        ch->vol_l  = fvol * (1.0f - (sep > 0.0f ? sep : 0.0f));
        ch->vol_r  = fvol * (1.0f + (sep < 0.0f ? sep : 0.0f));
    }
}

void SetSamplePitch(SoundEmitterID emit_id, SoundSmplTblID smpl_id, SoundPitch pitch)
{
    for (int i = 0; i < VITA_MAX_CHANNELS; i++) {
        SwChannel *ch = &s_sw[i];
        if (!ch->active || ch->emitter_id != emit_id || ch->smpl_id != smpl_id) continue;
        VitaSample *smp = &s_samples[ch->bank_id][ch->smpl_id];
        ch->fstep = ((float)smp->src_rate / (float)VITA_OUT_RATE) * ((float)pitch / 100.0f);
    }
}

void StopAllSamples(void)
{
    for (int i = 0; i < VITA_MAX_CHANNELS; i++) s_sw[i].active = false;
}

SoundSFXID get_sample_sfxid(SoundSmplTblID smpl_id, SoundBankID bank_id)
{
    if (bank_id >= VITA_NUM_BANKS) return 0;
    if (smpl_id < 0 || smpl_id >= s_sample_counts[bank_id]) return 0;
    return s_samples[bank_id][smpl_id].sfxid;
}

void MonitorStreamedSoundTrack(void) {}
void *GetSoundDriver(void) { return NULL; }
void toggle_bbking_mode(void) {}

/* ── Music — wired when VITA_HAVE_VORBIS, stubs otherwise ───────────────── */
void set_music_volume(SoundVolume v)
{
#ifdef VITA_HAVE_VORBIS
    s_music_vol = (float)v / 256.0f;
#else
    (void)v;
#endif
}

TbBool play_music(const char *fname)
{
#ifdef VITA_HAVE_VORBIS
    if (!s_initialized || s_music_port < 0 || s_music_sema < 0 || !fname) return false;
    snprintf(s_music_path, sizeof(s_music_path), "%s", fname);
    s_music_cmd = MUS_PLAY;
    /* If thread is already blocked on sema, signal it.  If it's mid-decode,
       it will pick up the MUS_PLAY command on the next grain check. */
    sceKernelSignalSema(s_music_sema, 1);
    return true;
#else
    (void)fname; return false;
#endif
}

TbBool play_music_track(int t)
{
    if (t == 0) { stop_music(); return true; }
    char *music_fname = get_game_file_path_fmt(FGrp_Music, "keeper%02d.ogg", t);
    return (music_fname != NULL && play_music(music_fname));
}

void pause_music(void)
{
#ifdef VITA_HAVE_VORBIS
    s_music_cmd = MUS_PAUSE;
#endif
}

void resume_music(void)
{
#ifdef VITA_HAVE_VORBIS
    s_music_cmd = MUS_RESUME;
#endif
}

void stop_music(void)
{
#ifdef VITA_HAVE_VORBIS
    if (s_music_running && s_music_sema >= 0) {
        s_music_cmd = MUS_STOP;
        sceKernelSignalSema(s_music_sema, 1);
    }
#endif
}

/* ── FMV audio (dedicated SCE port for Smacker playback) ────────────────── */
#define FMV_DMA_GRAIN   2048   /* samples per sceAudioOutOutput call */
#define FMV_RING_GRAINS 4      /* ring buffer capacity: 4 × ~42ms = ~170ms */

static int     s_fmv_port         = -1;
static int     s_fmv_grain        = FMV_DMA_GRAIN;
static int     s_fmv_output_rate  = 48000;
static int16_t s_fmv_accum[FMV_DMA_GRAIN * 2];   /* accumulator for partial grains */
static int     s_fmv_accum_count  = 0;

/* Audio clock.
 * s_fmv_samples_played: cumulative stereo samples submitted to sceAudioOutOutput.
 *   After the Nth call returns, (N-1) grains have *finished* playing and grain N
 *   is now playing.  So the real listener position is (s_fmv_samples_played - grain)
 *   plus time elapsed since grain N started.
 * s_fmv_grain_start_us: sceKernelGetProcessTimeWide() result at the moment
 *   sceAudioOutOutput returned for the current grain (i.e. when it started playing).
 *   Used to interpolate within the grain for sub-42ms clock accuracy. */
static volatile uint64_t s_fmv_samples_played  = 0;
static volatile uint64_t s_fmv_grain_start_us  = 0;

/* Dedicated output thread — two-semaphore bounded buffer pattern:
     s_fmv_sema_empty = free slot count  (starts at FMV_RING_GRAINS)
     s_fmv_sema_full  = filled slot count (starts at 0)
   Producer: WaitSema(empty, block) → write ring[wr] → advance wr → Signal(full)
   Consumer: WaitSema(full, block) → read ring[rd] → DMA → advance rd → Signal(empty)
   No race: a slot is atomically claimed (empty--, committed to producer) before writing,
   and atomically published (full++) after writing and only then readable by consumer. */
static SceUID  s_fmv_sema_empty  = -1;   /* counts free ring slots */
static SceUID  s_fmv_sema_full   = -1;   /* counts filled ring slots */
static SceUID  s_fmv_thread      = -1;
static int     s_fmv_thread_run  = 0;
static int16_t s_fmv_ring[FMV_RING_GRAINS][FMV_DMA_GRAIN * 2];
static int     s_fmv_ring_wr     = 0;   /* written by decode thread */
static int     s_fmv_ring_rd     = 0;   /* read by audio thread */

static int vita_fmv_audio_thread(SceSize args, void *argp)
{
    (void)args; (void)argp;
    while (s_fmv_thread_run) {
        sceKernelWaitSema(s_fmv_sema_full, 1, NULL);
        if (!s_fmv_thread_run) break;
        int rd = s_fmv_ring_rd;
        sceAudioOutOutput(s_fmv_port, s_fmv_ring[rd]);
        /* sceAudioOutOutput just returned: the previous grain has finished and
           the grain we submitted is now playing.  Record the wall-clock moment
           (microseconds) so vita_fmv_audio_pts_ns() can interpolate within the
           current grain, giving sub-42ms clock accuracy. */
        s_fmv_grain_start_us = sceKernelGetSystemTimeWide();
        s_fmv_ring_rd = (rd + 1) % FMV_RING_GRAINS;
        s_fmv_samples_played += (uint64_t)s_fmv_grain;
        sceKernelSignalSema(s_fmv_sema_empty, 1);
    }
    return sceKernelExitDeleteThread(0);
}

/** Returns nanoseconds of audio that have been output to the speaker.
 *  s_fmv_samples_played includes the grain currently playing, so we subtract
 *  one grain and add back the elapsed wall-clock time within that grain.
 *  This gives sub-grain (~1ms) accuracy instead of 42ms quantisation steps. */
int64_t vita_fmv_audio_pts_ns(void)
{
    uint64_t played = s_fmv_samples_played;
    uint64_t start  = s_fmv_grain_start_us;
    uint32_t rate   = (uint32_t)s_fmv_output_rate;
    uint32_t grain  = (uint32_t)s_fmv_grain;

    /* (played - grain) is the number of samples that have fully finished.
       On the first DMA call played==grain so this correctly returns ~0. */
    int64_t base_ns = (played >= grain)
        ? (int64_t)((played - grain) * 1000000000ULL / rate)
        : 0;

    /* Add elapsed time within the currently-playing grain. */
    uint64_t now_us   = sceKernelGetSystemTimeWide();
    int64_t  grain_ns = (int64_t)grain * 1000000000LL / (int64_t)rate;
    int64_t  elapsed  = (int64_t)(now_us - start) * 1000LL; /* µs → ns */
    if (elapsed < 0)       elapsed = 0;
    if (elapsed > grain_ns) elapsed = grain_ns;

    return base_ns + elapsed;
}

int vita_fmv_audio_open(int freq, int channels)
{
    if (s_fmv_port >= 0) {
        /* already open — close and reopen with new parameters */
        sceAudioOutReleasePort(s_fmv_port);
        s_fmv_port = -1;
    }
    /* Vita only supports 48000 and 44100 for MAIN ports */
    int rate = (freq == 44100) ? 44100 : 48000;
    s_fmv_grain = FMV_DMA_GRAIN;
    s_fmv_port  = sceAudioOutOpenPort(
        SCE_AUDIO_OUT_PORT_TYPE_MAIN,
        s_fmv_grain,
        rate,
        (channels >= 2) ? SCE_AUDIO_OUT_MODE_STEREO
                        : SCE_AUDIO_OUT_MODE_MONO);
    if (s_fmv_port < 0) {
        WARNLOG("vita_fmv_audio_open: sceAudioOutOpenPort failed (0x%08X)", s_fmv_port);
        s_fmv_port = -1;
        return 0;
    }
    sceAudioOutSetConfig(s_fmv_port, s_fmv_grain, rate,
        (channels >= 2) ? SCE_AUDIO_OUT_MODE_STEREO
                        : SCE_AUDIO_OUT_MODE_MONO);

    s_fmv_accum_count    = 0;
    s_fmv_ring_wr        = 0;
    s_fmv_ring_rd        = 0;
    s_fmv_samples_played = 0;
    s_fmv_grain_start_us = 0;
    s_fmv_output_rate    = rate;

    /* empty sema: starts at FMV_RING_GRAINS (all slots free) */
    s_fmv_sema_empty = sceKernelCreateSema("vita_fmv_empty", 0,
                                            FMV_RING_GRAINS, FMV_RING_GRAINS, NULL);
    /* full sema: starts at 0 (no filled slots yet) */
    s_fmv_sema_full  = sceKernelCreateSema("vita_fmv_full",  0,
                                            0, FMV_RING_GRAINS, NULL);
    if (s_fmv_sema_empty < 0 || s_fmv_sema_full < 0) {
        WARNLOG("vita_fmv_audio_open: sceKernelCreateSema failed (%08X / %08X)",
                s_fmv_sema_empty, s_fmv_sema_full);
        if (s_fmv_sema_empty >= 0) { sceKernelDeleteSema(s_fmv_sema_empty); s_fmv_sema_empty = -1; }
        if (s_fmv_sema_full  >= 0) { sceKernelDeleteSema(s_fmv_sema_full);  s_fmv_sema_full  = -1; }
        sceAudioOutReleasePort(s_fmv_port);
        s_fmv_port = -1;
        return 0;
    }

    s_fmv_thread_run = 1;
    s_fmv_thread = sceKernelCreateThread(
        "vita_fmv_audio", vita_fmv_audio_thread,
        0x40, 64 * 1024, 0, 0, NULL);
    if (s_fmv_thread < 0) {
        WARNLOG("vita_fmv_audio_open: sceKernelCreateThread failed (0x%08X)", s_fmv_thread);
        sceKernelDeleteSema(s_fmv_sema_empty); s_fmv_sema_empty = -1;
        sceKernelDeleteSema(s_fmv_sema_full);  s_fmv_sema_full  = -1;
        sceAudioOutReleasePort(s_fmv_port);
        s_fmv_thread = -1;
        s_fmv_port   = -1;
        return 0;
    }
    sceKernelStartThread(s_fmv_thread, 0, NULL);
    return 1;
}

void vita_fmv_audio_queue(const void *pcm, int bytes)
{
    if (s_fmv_port < 0 || s_fmv_thread < 0 || !pcm || bytes <= 0) return;
    /* Incoming data is always S16 stereo: 2 ch × 2 bytes = 4 bytes per stereo sample.
       Accumulate samples until a full grain is ready, then hand off to the audio thread.
       The audio thread owns sceAudioOutOutput — this function never blocks on DMA. */
    const int16_t *src = (const int16_t *)pcm;
    int src_count = bytes / (2 * (int)sizeof(int16_t)); /* stereo sample count */
    while (src_count > 0) {
        int space = s_fmv_grain - s_fmv_accum_count;
        int copy  = (src_count < space) ? src_count : space;
        memcpy(s_fmv_accum + s_fmv_accum_count * 2, src,
               (size_t)copy * 2 * sizeof(int16_t));
        s_fmv_accum_count += copy;
        src       += copy * 2;
        src_count -= copy;
        if (s_fmv_accum_count >= s_fmv_grain) {
            /* Claim a free slot — blocking wait.  This provides back-pressure
               that paces the decode thread to the hardware audio clock,
               preventing grain drops and keeping audio/video in sync. */
            sceKernelWaitSema(s_fmv_sema_empty, 1, NULL);
            int wr = s_fmv_ring_wr;
            memcpy(s_fmv_ring[wr], s_fmv_accum,
                   (size_t)FMV_DMA_GRAIN * 2 * sizeof(int16_t));
            s_fmv_ring_wr = (wr + 1) % FMV_RING_GRAINS;
            sceKernelSignalSema(s_fmv_sema_full, 1);
            s_fmv_accum_count = 0;
        }
    }
}

void vita_fmv_audio_close(void)
{
    if (s_fmv_thread >= 0) {
        s_fmv_thread_run = 0;
        sceKernelSignalSema(s_fmv_sema_full, 1);  /* wake audio thread if blocked */
        sceKernelWaitThreadEnd(s_fmv_thread, NULL, NULL);
        sceKernelDeleteThread(s_fmv_thread);
        s_fmv_thread = -1;
    }
    if (s_fmv_sema_empty >= 0) { sceKernelDeleteSema(s_fmv_sema_empty); s_fmv_sema_empty = -1; }
    if (s_fmv_sema_full  >= 0) { sceKernelDeleteSema(s_fmv_sema_full);  s_fmv_sema_full  = -1; }
    s_fmv_accum_count = 0;
    s_fmv_ring_wr     = 0;
    s_fmv_ring_rd     = 0;
    if (s_fmv_port >= 0) {
        sceAudioOutReleasePort(s_fmv_port);
        s_fmv_port = -1;
    }
}

/* ── SDL audio stubs (symbols expected by the linker from bflib_sndlib.cpp) ── */
int  InitialiseSDLAudio(void) { return 1; }
void ShutDownSDLAudio(void)   {}

/* ── AudioInterface vtable (kept for g_audio compatibility) ──────────────── */
static TbResult avi_init(void)     { return InitAudio(NULL) ? Lb_SUCCESS : Lb_FAIL; }
static void     avi_shutdown(void) { FreeAudio(); }
static void avi_play_sample(int id, long x, long y, long z, int vol)
{
    /* AudioInterface is a simplified 3D shim; 3D position discarded, pan=64 (centre) */
    (void)x; (void)y; (void)z;
    play_sample(1, (SoundSmplTblID)id, (SoundVolume)vol, 64, 100, 0, 2, 0);
}
static void avi_play_music(int t) { play_music_track(t); }
static void avi_set_listener(long x, long y, long z, int a) { (void)x; (void)y; (void)z; (void)a; }
static void avi_set_volume(int m, int mu, int s) {
    if (m  >= 0) SetSoundMasterVolume(m);
    if (mu >= 0) set_music_volume(mu);
    (void)s;
}

static AudioInterface audio_vita_impl = {
    avi_init, avi_shutdown, avi_play_sample,
    avi_play_music, avi_set_listener, avi_set_volume,
};

void audio_vita_initialize(void)
{
    g_audio = &audio_vita_impl;
}

#endif /* PLATFORM_VITA */
