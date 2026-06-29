#include "kfx_memory.h"
#include "pre_inc.h"
#include "config_keeperfx.h"
#include "cdrom.h"
#include "bflib_sndlib.h"
#include "bflib_datetm.h"
#include "bflib_sound.h"
#include "bflib_fileio.h"
#include <AL/al.h>
#include <AL/alc.h>
#include <AL/alext.h>
#include <SDL3/SDL.h>
#include <SDL3_mixer/SDL_mixer.h>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

// Single-header MP3 decoder (no external DLL dependency)
#define DR_MP3_IMPLEMENTATION
#include "../deps/dr_mp3.h"
#include <memory>
#include <vector>
#include <fstream>
#include <string>
#include <algorithm>
#include <utility>
#include <array>
#include <unordered_map>
#include <deque>
#include <mutex>
#include <atomic>
#include <set>
#include <thread>
#include <exception>
#include "kfx/profiling/KfxProfiling.h"
#include "post_inc.h"

namespace {

struct device_deleter {
    void operator()(ALCdevice* device) { alcCloseDevice(device); }
};

struct context_deleter {
    void operator()(ALCcontext* context) {
        alcMakeContextCurrent(nullptr);
        alcDestroyContext(context);
    }
};

using ALCdevice_ptr = std::unique_ptr<ALCdevice, device_deleter>;
using ALCcontext_ptr = std::unique_ptr<ALCcontext, context_deleter>;

SoundVolume g_master_volume = 0;
SoundVolume g_music_volume = 0;
ALCdevice_ptr g_openal_device;
ALCcontext_ptr g_openal_context;
MIX_Mixer* g_mixer = nullptr;
MIX_Track* g_music_track = nullptr;
MIX_Audio* g_music_audio = nullptr;
MIX_Track* g_speech_track = nullptr;
MIX_Audio* g_speech_audio = nullptr;
std::set<SoundSmplTblID> g_tick_samples;

bool g_bb_king_mode = false;

enum source_flags {
    bb_king_mode = 1,
};

const char* alErrorStr(ALenum code) {
    switch (code) {
    case AL_NO_ERROR:
        return "No error";
    case AL_INVALID_NAME:
        return "Invalid name";
    case AL_INVALID_ENUM:
        return "Invalid enum value";
    case AL_INVALID_VALUE:
        return "Invalid value";
    case AL_INVALID_OPERATION:
        return "Invalid operation";
    case AL_OUT_OF_MEMORY:
        return "Out of memory";
    }
    return "Unknown";
}

class openal_error : public std::runtime_error {
  public:
    inline openal_error(const char* description, ALenum errcode = alGetError())
        : runtime_error(std::string("OpenAL error: ") + description + ": " + alErrorStr(errcode)) {}
};

class openal_buffer {
  public:
    ALuint id = 0;

    openal_buffer() {
        ALuint buffers[1];
        alGenBuffers(1, buffers);
        const auto errcode = alGetError();
        if (errcode != AL_NO_ERROR) {
            throw openal_error("Cannot create buffer", errcode);
        }
        id = buffers[0];
    }

    inline ~openal_buffer() noexcept { alDeleteBuffers(1, &id); }

    openal_buffer(const openal_buffer&) = delete;
    openal_buffer& operator=(const openal_buffer&) = delete;

    inline openal_buffer(openal_buffer&& other) noexcept : id(std::exchange(other.id, 0)) {}

    inline openal_buffer& operator=(openal_buffer&& other) noexcept {
        id = std::exchange(other.id, 0);
        return *this;
    }
};

class openal_source {
  public:
    ALuint id = 0;
    SoundMilesID mss_id = 0;
    SoundEmitterID emit_id = 0;
    SoundSmplTblID smptbl_id = 0;
    int flags = 0;

    openal_source() {
        ALuint sources[1];
        alGenSources(1, sources);
        const auto errcode = alGetError();
        if (errcode != AL_NO_ERROR) {
            throw openal_error("Cannot create source", errcode);
        }
        id = sources[0];
    }

    inline ~openal_source() noexcept { alDeleteSources(1, &id); }

    void play(const openal_buffer& buffer) {
        alSourcei(id, AL_BUFFER, buffer.id);
        auto errcode = alGetError();
        if (errcode != AL_NO_ERROR) {
            throw openal_error("Cannot attach buffer", errcode);
        }
        alSourcePlay(id);
        errcode = alGetError();
        if (errcode != AL_NO_ERROR) {
            throw openal_error("Cannot play source", errcode);
        }
    }

    void stop() {
        alSourceStop(id);
        const auto errcode = alGetError();
        if (errcode != AL_NO_ERROR) {
            throw openal_error("Cannot stop source", errcode);
        }
    }

    void gain(SoundVolume volume) {
        alSourcef(id, AL_GAIN, float(volume) / FULL_LOUDNESS);
        const auto errcode = alGetError();
        if (errcode != AL_NO_ERROR) {
            throw openal_error("Cannot set volume", errcode);
        }
    }

    void pitch(SoundPitch pitch) {
        alSourcef(id, AL_PITCH, float(pitch) / NORMAL_PITCH);
        const auto errcode = alGetError();
        if (errcode != AL_NO_ERROR) {
            throw openal_error("Cannot set pitch", errcode);
        }
    }

    void pan(SoundPan pan) {
        // convert 0..128 (where 64 is center) to -1.0..1.0 and then reduce stereo separation by 50%
        const auto x = (-(float(64 - pan) / 64.0f)) * 0.5f;
        const auto z = -1.0f; // in front of listener
        alSource3f(id, AL_POSITION, x, 0, z);
        const auto errcode = alGetError();
        if (errcode != AL_NO_ERROR) {
            throw openal_error("Cannot set position", errcode);
        }
    }

    void repeat(bool value) {
        alSourcei(id, AL_LOOPING, value ? AL_TRUE : AL_FALSE);
        const auto errcode = alGetError();
        if (errcode != AL_NO_ERROR) {
            throw openal_error("Cannot toggle looping", errcode);
        }
    }

    bool is_playing() const {
        ALint state = 0;
        alGetSourcei(id, AL_SOURCE_STATE, &state);
        const auto errcode = alGetError();
        if (errcode != AL_NO_ERROR) {
            throw openal_error("Cannot get source state", errcode);
        }
        return state == AL_PLAYING;
    }

    openal_source(const openal_source&) = delete;
    openal_source& operator=(const openal_source&) = delete;

    inline openal_source(openal_source&& other) noexcept
        : id(std::exchange(other.id, 0)), mss_id(std::exchange(other.mss_id, 0)),
          emit_id(std::exchange(other.emit_id, 0)), smptbl_id(std::exchange(other.smptbl_id, 0)) {}

    inline openal_source& operator=(openal_source&& other) noexcept {
        id = std::exchange(other.id, 0);
        mss_id = std::exchange(other.mss_id, 0);
        emit_id = std::exchange(other.emit_id, 0);
        smptbl_id = std::exchange(other.smptbl_id, 0);
        return *this;
    }
};

inline uint32_t make_fourcc(const char (&code)[5]) {
    return (uint32_t(code[0]) << 0) | (uint32_t(code[1]) << 8) | (uint32_t(code[2]) << 16) | (uint32_t(code[3]) << 24);
}

#define WAVE_FORMAT_PCM 1
#define WAVE_FORMAT_ADPCM 2

#pragma pack(1)
struct riff_chunk_t {
    uint32_t tag;
    uint32_t size;
    // zero or more bytes of data
    // padding byte if data size not a multiple of two
};
#pragma pack()

#pragma pack(1)
struct WAVEFORMATEX {
    uint16_t wFormatTag;
    uint16_t nChannels;
    uint32_t nSamplesPerSec;
    uint32_t nAvgBytesPerSec;
    uint16_t nBlockAlign;
    uint16_t wBitsPerSample;
    // uint16_t cbSize;
};
#pragma pack()

class wave_file {
  public:
    wave_file(std::ifstream& stream, uint32_t max_data_size = UINT32_MAX) : m_max_data_size(max_data_size) {
        riff_chunk_t riff_header;
        stream.read(reinterpret_cast<char*>(&riff_header), sizeof(riff_header));
        if (riff_header.tag != make_fourcc("RIFF")) {
            throw std::runtime_error("Expected RIFF chunk");
        }
        uint32_t filetype;
        stream.read(reinterpret_cast<char*>(&filetype), sizeof(filetype));
        if (filetype != make_fourcc("WAVE")) {
            throw std::runtime_error("Expected WAVE chunk");
        }
        riff_chunk_t chunk;
        for (bool have_format = false, have_data = false; !(have_format && have_data);) {
            if (!stream.read(reinterpret_cast<char*>(&chunk), sizeof(chunk))) {
                throw std::runtime_error("Unexpected end of WAVE stream");
            }
            if (chunk.tag == make_fourcc("fmt ")) {
                if (chunk.size < sizeof(WAVEFORMATEX)) {
                    throw std::runtime_error("Expected WAVEFORMATEX struct");
                }
                WAVEFORMATEX formatex;
                stream.read(reinterpret_cast<char*>(&formatex), sizeof(formatex));
                if (!(formatex.wFormatTag == WAVE_FORMAT_PCM || formatex.wFormatTag == WAVE_FORMAT_ADPCM)) {
                    throw std::runtime_error("Unsupported format");
                } else if (formatex.nChannels == 1 && formatex.wBitsPerSample == 4) {
                    m_format = AL_FORMAT_MONO_MSADPCM_SOFT;
                } else if (formatex.nChannels == 1 && formatex.wBitsPerSample == 8) {
                    m_format = AL_FORMAT_MONO8;
                } else if (formatex.nChannels == 1 && formatex.wBitsPerSample == 16) {
                    m_format = AL_FORMAT_MONO16;
                } else if (formatex.nChannels == 2 && formatex.wBitsPerSample == 4) {
                    m_format = AL_FORMAT_STEREO_MSADPCM_SOFT;
                } else if (formatex.nChannels == 2 && formatex.wBitsPerSample == 8) {
                    m_format = AL_FORMAT_STEREO8;
                } else if (formatex.nChannels == 2 && formatex.wBitsPerSample == 16) {
                    m_format = AL_FORMAT_STEREO16;
                } else {
                    throw std::runtime_error("Unsupported format");
                }
                m_samplerate = formatex.nSamplesPerSec;
                if (chunk.size > sizeof(formatex)) {
                    stream.seekg(chunk.size - sizeof(formatex), std::ios::cur);
                }
                have_format = true;
            } else if (chunk.tag == make_fourcc("data")) {
                if (chunk.size > m_max_data_size) {
                    throw std::runtime_error(std::string("WAVE data chunk size ") + std::to_string(chunk.size) +
                                             " exceeds declared sample size " + std::to_string(m_max_data_size) +
                                             " — seek offset is likely wrong");
                }
                m_pcm.resize(chunk.size);
                stream.read(reinterpret_cast<char*>(m_pcm.data()), m_pcm.size());
                have_data = true;
            } else {
                stream.seekg(chunk.size, std::ios::cur);
            }
        }
    }

    inline const std::vector<uint8_t>& pcm() const { return m_pcm; }

    inline int samplerate() const { return m_samplerate; }

    inline ALenum format() const { return m_format; }

  protected:
    int m_samplerate = 0;
    uint32_t m_max_data_size = UINT32_MAX;
    ALenum m_format = 0;
    std::vector<uint8_t> m_pcm;
};

// Holds decoded PCM for one sample — no OpenAL objects; safe to fill from any thread.
struct decoded_sample {
    std::string name;
    SoundSFXID sfx_id;
    ALenum format = 0;
    int samplerate = 0;
    std::vector<uint8_t> pcm;
};

struct sound_sample {

    std::string name;
    SoundSFXID sfx_id;
    openal_buffer buffer;

    // Constructor 1: For pre-decoded sound banks (async or sync fallback)
    sound_sample(decoded_sample&& d) {
        name = std::move(d.name);
        sfx_id = d.sfx_id;
        alBufferData(buffer.id, d.format, d.pcm.data(), (ALsizei)d.pcm.size(), d.samplerate);
        const auto errcode = alGetError();
        if (errcode != AL_NO_ERROR) {
            throw openal_error("Cannot buffer sample data", errcode);
        }
    }

    // Constructor 2: For custom runtime sound loading (e.g. MP3, WAV, etc.)
    sound_sample(const char* _name, SoundSFXID _sfx_id, const std::vector<uint8_t>& pcm, ALenum format,
                 int samplerate) {
        name = _name;
        sfx_id = _sfx_id;
        alBufferData(buffer.id, format, pcm.data(), (ALsizei)pcm.size(), samplerate);
        const auto errcode = alGetError();
        if (errcode != AL_NO_ERROR) {
            throw openal_error("Cannot buffer sample data", errcode);
        }
    }

    sound_sample(const sound_sample&) = delete;
    sound_sample& operator=(const sound_sample&) = delete;

    inline sound_sample(sound_sample&& other) noexcept
        : name(std::move(other.name)), sfx_id(other.sfx_id), buffer(std::move(other.buffer)) {}

    inline sound_sample& operator=(sound_sample&& other) noexcept {
        name = std::move(other.name);
        sfx_id = other.sfx_id;
        buffer = std::move(other.buffer);
        return *this;
    }
};

#pragma pack(1)
struct SoundBankHead { // sizeof = 18
    uint8_t signature[14];
    uint32_t version;
};
#pragma pack()

#pragma pack(1)
struct SoundBankSample { // sizeof = 32
    /** Name of the sound file the sample comes from. */
    char filename[18];
    /** Offset of the sample data. */
    uint32_t data_offset;
    uint32_t sample_rate;
    /** Size of the sample file. */
    uint32_t data_size;
    SoundSFXID sfxid;
    uint8_t format_flags;
};
#pragma pack()

#pragma pack(1)
struct SoundBankEntry { // sizeof = 16
    uint32_t first_sample_offset;
    uint32_t first_data_offset;
    uint32_t total_samples_size;
    uint32_t entries_count;
};
#pragma pack()

// Reads and decodes all samples from a sound bank file into raw PCM structs.
// No OpenAL calls — safe to run on any thread before a context exists.
std::vector<decoded_sample> decode_sound_bank(const char* filename) {
    const int directory_index = 2; // a5 was always 1622
    std::ifstream stream(filename, std::ios::in | std::ios::binary);
    if (!stream.is_open()) {
        throw std::runtime_error("Cannot open sound bank file");
    }
    stream.seekg(-4, std::ios::end);
    uint32_t head_offset;
    stream.read(reinterpret_cast<char*>(&head_offset), sizeof(head_offset));
    stream.seekg(head_offset, std::ios::beg);
    SoundBankHead bhead;
    stream.read(reinterpret_cast<char*>(&bhead), sizeof(bhead));
    SoundBankEntry bentries[9];
    stream.read(reinterpret_cast<char*>(bentries), sizeof(bentries));
    const auto& directory = bentries[directory_index];
    if (directory.first_sample_offset == 0) {
        throw std::runtime_error("Invalid sample offset");
    }
    if (directory.total_samples_size < sizeof(SoundBankSample)) {
        throw std::runtime_error("Invalid samples size");
    }
    const int sample_count = directory.total_samples_size / sizeof(SoundBankSample);
    JUSTLOG("Loading %s: dir[%d] first_sample=%u first_data=%u total_size=%u count=%d", filename, directory_index,
            directory.first_sample_offset, directory.first_data_offset, directory.total_samples_size, sample_count);
    if (sample_count <= 0 || sample_count > 65535) {
        // I've seen this from copying over the sound bank file from the steam release. Unsure what it's reading at this
        // point that's subtly different
        throw std::runtime_error(std::string("Implausible sample count ") + std::to_string(sample_count) +
                                 " — directory_index=" + std::to_string(directory_index) +
                                 " total_samples_size=" + std::to_string(directory.total_samples_size));
    }
    stream.seekg(directory.first_sample_offset, std::ios::beg);
    std::vector<decoded_sample> buffers;
    buffers.reserve(sample_count);
    SoundBankSample sample;
    for (int i = 0; i < sample_count; ++i) {
        stream.seekg(directory.first_sample_offset + (sizeof(sample) * i), std::ios::beg);
        stream.read(reinterpret_cast<char*>(&sample), sizeof(sample));
        const uint32_t data_seek = directory.first_data_offset + sample.data_offset;
        stream.seekg(data_seek, std::ios::beg);
        try {
            wave_file wav(stream, sample.data_size);
            decoded_sample d;
            d.name = sample.filename;
            d.sfx_id = sample.sfxid;
            d.samplerate = wav.samplerate();
            const auto fmt = wav.format();
            if (fmt == AL_FORMAT_MONO_MSADPCM_SOFT) {
                // Needed for heart6a.wav
                const auto& raw = wav.pcm();
                d.pcm.resize(raw.size() * 2);
                for (size_t j = 0; j < raw.size(); ++j) {
                    d.pcm[(j * 2) + 0] = (raw[j] >> 4) * 2;
                    d.pcm[(j * 2) + 1] = (raw[j] & 0x7) * 2;
                }
                d.format = AL_FORMAT_MONO8;
            } else if (fmt == AL_FORMAT_STEREO_MSADPCM_SOFT) {
                throw std::runtime_error("Format not implemented");
            } else {
                d.pcm = wav.pcm();
                d.format = fmt;
            }
            buffers.push_back(std::move(d));
        } catch (const std::exception& ex) {
            ERRORLOG("Sample %d '%s' from '%s' failed at offset %u (data_size=%u): %s", i, sample.filename, filename,
                     data_seek, sample.data_size, ex.what());
            throw;
        }
    }
    JUSTLOG("Decoded %d sound samples from %s", (int)buffers.size(), filename);
    return buffers;
}

std::vector<openal_source> g_sources;
std::array<std::vector<sound_sample>, 2> g_banks;
// Filled by decode_sound_bank on the preload thread; consumed by upload_decoded_bank on the main thread.
static std::array<std::vector<decoded_sample>, 2> g_pending_banks;
std::vector<sound_sample> g_custom_bank; // Third bank for custom sounds loaded at runtime
SoundSmplTblID g_speech_offset = 0;      // Unified ID start of speech bank
SoundSmplTblID g_custom_offset = 0;      // Unified ID start of custom bank

// Redirect table: maps a raw sound.dat effect ID to a custom bank ID.
// Populated by sound_register_id_redirect() when sounds.cfg contains a numeric-key entry.
// Applied in play_sample() before bank dispatch — does not affect speech or already-custom IDs.
static std::unordered_map<SoundSmplTblID, SoundSmplTblID> g_id_redirects;

// Background thread for async sound bank preloading.
static std::thread g_sound_preload_thread;
static std::exception_ptr g_sound_preload_exception;

extern "C" void SoundBanks_StartAsyncLoad(void) {
    if (SoundDisabled)
        return;
    if (g_sound_preload_thread.joinable())
        return; // already started

    // Compute file paths on the main thread — static-buffer helpers are not thread-safe.
    char snd_fname[2048] = {};
    char spc_fname[2048] = {};
    prepare_file_path_buf(snd_fname, sizeof(snd_fname), FGrp_LrgSound, "sound.dat");
    {
        char* spc = get_game_file_path_fmt(FGrp_LrgSound, "speech_%s.dat", get_language_lwrstr(install_info.lang_id));
        if (!spc || !LbFileExists(spc))
            spc = prepare_file_path(FGrp_LrgSound, "speech.dat");
        if (!spc || !LbFileExists(spc))
            spc = get_game_file_path_fmt(FGrp_LrgSound, "speech_%s.dat", get_language_lwrstr(1));
        if (spc)
            snprintf(spc_fname, sizeof(spc_fname), "%s", spc);
    }

    g_sound_preload_thread = std::thread([s = std::string(snd_fname), p = std::string(spc_fname)]() {
        try {
#ifdef TRACY_ENABLE
            tracy::SetThreadName("SoundPreload");
#endif
            {
                KFX_ZONE_COLOR("SoundPreload::decode_sound_bank(sound.dat)", KFX_COLOR_RENDER_CPU);
                g_pending_banks[0] = decode_sound_bank(s.c_str());
            }
            try {
                if (!p.empty()) {
                    KFX_ZONE_COLOR("SoundPreload::decode_sound_bank(speech)", KFX_COLOR_RENDER_CPU);
                    g_pending_banks[1] = decode_sound_bank(p.c_str());
                }
            } catch (const std::exception& e) {
                WARNLOG("Speech bank async preload failed: %s", e.what());
                g_pending_banks[1].clear();
            }
        } catch (const std::exception&) {
            g_sound_preload_exception = std::current_exception();
        }
    });
    SYNCLOG("SoundBanks_StartAsyncLoad: preloading sound banks on background thread");
}

void load_sound_banks() {
    char snd_fname[2048];
    prepare_file_path_buf(snd_fname, sizeof(snd_fname), FGrp_LrgSound, "sound.dat");
    // language-specific speech file
    char* spc_fname = get_game_file_path_fmt(FGrp_LrgSound, "speech_%s.dat", get_language_lwrstr(install_info.lang_id));
    // default speech file
    if (!spc_fname || !LbFileExists(spc_fname)) {
        spc_fname = prepare_file_path(FGrp_LrgSound, "speech.dat");
    }
    // speech file for english
    if (!spc_fname || !LbFileExists(spc_fname)) {
        spc_fname = get_game_file_path_fmt(FGrp_LrgSound, "speech_%s.dat", get_language_lwrstr(1));
    }
    g_pending_banks[0] = decode_sound_bank(snd_fname);
    try {
        g_pending_banks[1] = decode_sound_bank(spc_fname);
    } catch (const std::exception& e) {
        WARNLOG("Speech bank failed to load, speech will be unavailable: %s", e.what());
        g_pending_banks[1].clear();
    }
}

// Upload decoded PCM banks into OpenAL buffers. Must be called with a current context.
std::vector<sound_sample> upload_decoded_bank(std::vector<decoded_sample> decoded) {
    std::vector<sound_sample> result;
    result.reserve(decoded.size());
    for (auto& d : decoded) {
        result.push_back(sound_sample(std::move(d)));
    }
    return result;
}

void print_device_info() {
    if (alcIsExtensionPresent(nullptr, "ALC_ENUMERATE_ALL_EXT")) {
        const auto devices = alcGetString(nullptr, ALC_ALL_DEVICES_SPECIFIER);
        for (auto device = devices; device[0] != 0; device += strlen(device)) {
            // Device enumeration
        }
    } else if (alcIsExtensionPresent(nullptr, "ALC_ENUMERATION_EXT")) {
        const auto devices = alcGetString(nullptr, ALC_DEVICE_SPECIFIER);
        for (auto device = devices; device[0] != 0; device += strlen(device)) {
            // Device enumeration
        }
    } else {
        // Cannot enumerate devices :(
    }
}

std::mutex g_mix_mutex;

} // namespace

extern "C" void FreeAudio() {
    SYNCDBG(6, "Starting audio cleanup");

    {
        std::lock_guard<std::mutex> guard(g_mix_mutex);
        if (g_mixer) {
            SYNCDBG(7, "SDL_mixer active, stopping all tracks");
            MIX_StopAllTracks(g_mixer, 0);
        }
        if (g_music_audio) {
            MIX_DestroyAudio(g_music_audio);
            g_music_audio = nullptr;
            SYNCDBG(8, "Freed SDL_mixer music audio");
        }
        if (g_speech_audio) {
            MIX_DestroyAudio(g_speech_audio);
            g_speech_audio = nullptr;
            SYNCDBG(8, "Freed SDL_mixer speech audio");
        }
    }

    if (g_mixer) {
        ShutDownSDLAudio();
        SYNCDBG(7, "SDL_mixer shutdown complete");
    }

    // Clear OpenAL sources and buffers while context is still current
    g_sources.clear();
    g_banks[0].clear();
    g_banks[1].clear();
    g_pending_banks[0].clear();
    g_pending_banks[1].clear();
    g_custom_bank.clear();  // Clear custom sounds when cleaning up audio
    g_id_redirects.clear(); // Clear raw-ID redirects alongside custom bank
    SYNCDBG(7, "Cleared OpenAL sources and sound banks");

    // Now destroy OpenAL context and device (unique_ptr handles proper cleanup)
    g_openal_context = nullptr;
    g_openal_device = nullptr;
    SYNCDBG(6, "Audio cleanup complete");
}

extern "C" void custom_sound_bank_clear() {
    g_custom_bank.clear();
    g_id_redirects.clear();
}

extern "C" void sound_register_id_redirect(SoundSmplTblID from_id, SoundSmplTblID to_id) {
    g_id_redirects[from_id] = to_id;
    SYNCDBG(7, "Registered ID redirect: %d -> %d", from_id, to_id);
}

extern "C" void sound_clear_id_redirects(void) {
    g_id_redirects.clear();
}

static std::unordered_map<SoundSmplTblID, SoundSmplTblID> g_id_redirects_snapshot;
static size_t g_custom_bank_watermark = 0;

extern "C" void sound_save_id_redirect_snapshot(void) {
    g_id_redirects_snapshot = g_id_redirects;
    g_custom_bank_watermark = g_custom_bank.size();
    SYNCDBG(7, "Saved sound snapshot: %" PRIuSIZE " redirects, %" PRIuSIZE " custom bank entries",
            SZCAST(g_id_redirects_snapshot.size()), SZCAST(g_custom_bank_watermark));
}

extern "C" void sound_restore_id_redirect_snapshot(void) {
    g_id_redirects = g_id_redirects_snapshot;
    if (g_custom_bank.size() > g_custom_bank_watermark) {
        SYNCDBG(7, "Trimming custom bank from %" PRIuSIZE " to %" PRIuSIZE " entries", SZCAST(g_custom_bank.size()),
                SZCAST(g_custom_bank_watermark));
        g_custom_bank.erase(g_custom_bank.begin() + (ptrdiff_t)g_custom_bank_watermark, g_custom_bank.end());
    }
    SYNCDBG(7, "Restored sound snapshot: %" PRIuSIZE " redirects", SZCAST(g_id_redirects.size()));
}

extern "C" void SetSoundMasterVolume(SoundVolume volume) {
    try {
        // Set OpenAL listener gain to maximum so we can split up the mentor speech volume slider from the sound effects
        // volume slider
        alListenerf(AL_GAIN, 1.0f);
        const auto errcode = alGetError();
        if (errcode != AL_NO_ERROR) {
            throw openal_error("Cannot set master volume", errcode);
        }
        g_master_volume = volume;
    } catch (const std::exception& e) {
        ERRORLOG("%s", e.what());
    }
}

extern "C" void set_music_volume(SoundVolume value) {
    g_music_volume = value;
    SetRedbookVolume(value);
    if (g_music_track)
        MIX_SetTrackGain(g_music_track, float(value) / FULL_LOUDNESS);
}

extern "C" TbBool play_music(const char* fname) {
    std::lock_guard<std::mutex> guard(g_mix_mutex);
    if (strcmp(game.music_fname, fname) == 0)
        return false;
    game.music_track = -1;
    snprintf(game.music_fname, sizeof(game.music_fname), "%s", fname);
    if (!g_mixer || !g_music_track)
        return false;
    MIX_Audio* new_audio = MIX_LoadAudio(g_mixer, game.music_fname, false);
    if (!new_audio) {
        WARNLOG("Cannot load music from %s: %s", game.music_fname, SDL_GetError());
        return false;
    }
    // MIX_SetTrackAudio replaces any currently-playing audio; old audio is no longer used after this.
    MIX_SetTrackAudio(g_music_track, new_audio);
    MIX_Audio* old_audio = g_music_audio;
    g_music_audio = new_audio;
    if (old_audio)
        MIX_DestroyAudio(old_audio);
    if (!MIX_PlayTrack(g_music_track, 0)) {
        WARNLOG("Cannot play music from %s: %s", game.music_fname, SDL_GetError());
        return false;
    }
    MIX_SetTrackLoops(g_music_track, -1);
    return true;
}

extern "C" TbBool play_music_track(int track) {
    game.music_track = track;
    memset(game.music_fname, 0, sizeof(game.music_fname));
    if (game.music_track == 0) {
        stop_music();
        return true;
    } else if (features_enabled & Ft_NoCdMusic) {
        char* music_fname = get_game_file_path_fmt(FGrp_Music, "keeper%02d.ogg", track);
        return (music_fname != NULL && play_music(music_fname));
    } else {
        if (PlayRedbookTrack(track)) {
            return true;
        } else {
            WARNLOG("Cannot play track %d", game.music_track);
            return false;
        }
    }
}

extern "C" void pause_music() {
    if (features_enabled & Ft_NoCdMusic) {
        if (g_music_track) MIX_PauseTrack(g_music_track);
    } else {
        PauseRedbookTrack();
    }
}

extern "C" void resume_music() {
    if (features_enabled & Ft_NoCdMusic) {
        if (g_music_track) MIX_ResumeTrack(g_music_track);
    } else {
        ResumeRedbookTrack();
    }
}

extern "C" void stop_music() {
    game.music_track = 0;
    memset(game.music_fname, 0, sizeof(game.music_fname));
    if (features_enabled & Ft_NoCdMusic) {
        if (g_music_track) {
            Sint64 fade_frames = MIX_TrackMSToFrames(g_music_track, 1000);
            MIX_StopTrack(g_music_track, fade_frames);
        }
    } else {
        StopRedbookTrack();
    }
}

extern "C" TbBool GetSoundInstalled() {
    return g_openal_device && g_openal_context;
}

// This function gets called every tick
extern "C" void MonitorStreamedSoundTrack() {
    for (auto& source : g_sources) {
        try {
            if (source.emit_id > 0 && !source.is_playing()) {
                source.emit_id = 0;
                source.smptbl_id = 0;
            }
        } catch (const std::exception& e) {
            ERRORLOG("%s", e.what());
        }
    }
    g_tick_samples.clear();
}

extern "C" void* GetSoundDriver() {
    // This just needs to return any non-null pointer. FMV library appears to have standalone audio
    static int dummy = 0;
    return &dummy;
}

extern "C" void StopAllSamples() {
    for (auto& source : g_sources) {
        try {
            source.stop();
        } catch (const std::exception& e) {
            ERRORLOG("%s", e.what());
        }
    }
}

extern "C" TbBool InitAudio(const SoundSettings* settings) {
    try {
        if (game.easter_eggs_enabled) {
            TbDate date;
            LbDate(&date);
            g_bb_king_mode |= ((date.Day == 1) && (date.Month == 2));
        }
        if (SoundDisabled) {
            WARNLOG("Sound is disabled, skipping OpenAL initialization");
            return false;
        }
        if (g_openal_device || g_openal_context) {
            WARNLOG("OpenAL already initialized");
            return true;
        }
        print_device_info();
        ALCdevice_ptr device(alcOpenDevice(nullptr));
        if (!device) {
            throw openal_error("Cannot open default audio device");
        }
        ALCcontext_ptr context(alcCreateContext(device.get(), nullptr));
        if (!context) {
            throw openal_error("Cannot create context");
        } else if (!alcMakeContextCurrent(context.get())) {
            throw openal_error("Cannot make context current");
        }
        g_sources.resize(settings->max_number_of_samples);
        for (size_t i = 0; i < g_sources.size(); ++i) {
            g_sources[i].mss_id = i + 1;
        }
        if (g_sound_preload_thread.joinable()) {
            SYNCLOG("InitAudio: joining async sound preload thread");
            KFX_ZONE_COLOR("InitAudio::join_preload", KFX_COLOR_RENDER_CPU);
            g_sound_preload_thread.join();
            if (g_sound_preload_exception) {
                // Async decode failed — retry synchronously.
                ERRORLOG("InitAudio: async sound preload failed, retrying synchronously");
                g_sound_preload_exception = nullptr;
                g_pending_banks[0].clear();
                g_pending_banks[1].clear();
                load_sound_banks();
            }
        } else {
            load_sound_banks();
        }
        KFX_ZONE_COLOR("InitAudio::upload_banks", KFX_COLOR_RENDER_CPU);
        g_banks[0] = upload_decoded_bank(std::move(g_pending_banks[0]));
        g_banks[1] = upload_decoded_bank(std::move(g_pending_banks[1]));

        g_speech_offset = (SoundSmplTblID)g_banks[0].size();
        g_custom_offset = g_speech_offset + (SoundSmplTblID)g_banks[1].size();

        SYNCLOG("InitAudio: sound banks ready (banks: %u + %u samples)", (unsigned)g_banks[0].size(),
                (unsigned)g_banks[1].size());
        g_openal_device = std::move(device);
        g_openal_context = std::move(context);
        return true;
    } catch (const std::exception& e) {
        ERRORLOG("%s", e.what());
    }
    SoundDisabled = true;
    return false;
}

extern "C" TbBool IsSamplePlaying(SoundMilesID mss_id) {
    try {
        for (const auto& source : g_sources) {
            if (source.mss_id == mss_id) {
                return source.is_playing();
            }
        }
    } catch (const std::exception& e) {
        ERRORLOG("%s", e.what());
    }
    return false;
}

extern "C" SoundVolume GetCurrentSoundMasterVolume() {
    return g_master_volume;
}

extern "C" void SetSampleVolume(SoundEmitterID emit_id, SoundSmplTblID smptbl_id, SoundVolume volume) {
    for (auto& source : g_sources) {
        if (source.emit_id == emit_id && source.smptbl_id == smptbl_id) {
            try {
                source.gain(volume);
            } catch (const std::exception& e) {
                ERRORLOG("%s", e.what());
            }
        }
    }
}

extern "C" void SetSamplePan(SoundEmitterID emit_id, SoundSmplTblID smptbl_id, SoundPan pan) {
    for (auto& source : g_sources) {
        if (source.emit_id == emit_id && source.smptbl_id == smptbl_id) {
            try {
                source.pan(pan);
            } catch (const std::exception& e) {
                ERRORLOG("%s", e.what());
            }
        }
    }
}

extern "C" void SetSamplePitch(SoundEmitterID emit_id, SoundSmplTblID smptbl_id, SoundPitch pitch) {
    for (auto& source : g_sources) {
        if (source.emit_id == emit_id && source.smptbl_id == smptbl_id) {
            try {
                if (source.flags & bb_king_mode) {
                    return; // ben enjoyed dofi's stream so much I made random pitch an easter egg
                } else {
                    source.pitch(pitch);
                }
            } catch (const std::exception& e) {
                ERRORLOG("%s", e.what());
            }
        }
    }
}

extern "C" SoundMilesID play_sample(SoundEmitterID emit_id, SoundSmplTblID smptbl_id, SoundVolume volume, SoundPan pan,
                                    SoundPitch pitch,
                                    char repeats,       // possible values: -1, 0
                                    unsigned char ctype // possible values: 2, 3
) {
    if (emit_id <= 0) {
        ERRORLOG("Can't play sample %d, invalid emitter ID", smptbl_id);
        return 0;
    }
    // Apply raw-ID redirect before bank dispatch (only for effect-bank IDs)
    if (smptbl_id > 0 && smptbl_id < g_speech_offset) {
        auto redir = g_id_redirects.find(smptbl_id);
        if (redir != g_id_redirects.end()) {
            smptbl_id = redir->second;
        }
    }

    if (g_tick_samples.count(smptbl_id) > 0) {
        return 0; // don't play the same sample multiple times on the same tick
    }

    // Resolve sample data from unified ID space
    const openal_buffer* buf = nullptr;
    if (smptbl_id >= g_custom_offset) {
        const SoundSmplTblID idx = smptbl_id - g_custom_offset;
        if (idx < 0 || idx >= (SoundSmplTblID)g_custom_bank.size()) {
            ERRORLOG("Can't play custom sample %d, out of range", smptbl_id);
            return 0;
        }
        buf = &g_custom_bank[idx].buffer;
    } else if (smptbl_id >= g_speech_offset) {
        const SoundSmplTblID idx = smptbl_id - g_speech_offset;
        if (idx <= 0 || idx >= (SoundSmplTblID)g_banks[1].size()) {
            ERRORLOG("Can't play speech sample %d, out of range", smptbl_id);
            return 0;
        }
        buf = &g_banks[1][idx].buffer;
    } else {
        if (smptbl_id <= 0 || smptbl_id >= (SoundSmplTblID)g_banks[0].size()) {
            if (smptbl_id != 0) {
                ERRORLOG("Can't play effect sample %d, out of range", smptbl_id);
            }
            return 0;
        }
        buf = &g_banks[0][smptbl_id].buffer;
    }
    try {
        g_tick_samples.emplace(smptbl_id);

        // ctype 2/3: if this emitter is already playing the same sample, restart it in-place
        // rather than allocating a new source (mirrors MSS single-voice-per-slot behaviour and
        // prevents sounds from stacking — e.g. hailstorm projectiles all hitting the same target).
        if (ctype == 2 || ctype == 3) {
            for (auto& source : g_sources) {
                if (source.emit_id == emit_id && source.smptbl_id == smptbl_id) {
                    source.stop();
                    source.gain(volume);
                    source.pan(pan);
                    source.repeat(repeats == -1);
                    source.pitch(pitch);
                    source.play(*buf);
                    return source.mss_id;
                }
            }
        }
        for (auto& source : g_sources) {
            if (source.emit_id == 0) {
                source.gain(volume);
                source.pan(pan);
                source.repeat(repeats == -1);
                if (g_bb_king_mode) {
                    // ben enjoyed dofi's stream so much I made random pitch an easter egg
                    if (SOUND_RANDOM(10000) <= 3) { // ~0.03% of the time
                        source.flags |= bb_king_mode;
                        source.pitch((NORMAL_PITCH / 2) + SOUND_RANDOM(NORMAL_PITCH));
                    } else {
                        source.flags &= ~bb_king_mode;
                        source.pitch(pitch);
                    }
                } else {
                    source.pitch(pitch);
                }
                source.play(*buf);
                source.emit_id = emit_id;
                source.smptbl_id = smptbl_id;
                return source.mss_id;
            }
        }
        if (game.frame_skip < 2) {
            ERRORLOG("Can't play sample %d, too many samples playing at once", smptbl_id);
        }
        return 0;
    } catch (const std::exception& e) {
        ERRORLOG("%s", e.what());
    }
    return 0;
}

extern "C" void stop_sample(SoundEmitterID emit_id, SoundSmplTblID smptbl_id) {
    for (auto& source : g_sources) {
        if (emit_id == source.emit_id && smptbl_id == source.smptbl_id) {
            try {
                source.stop();
                source.emit_id = 0;
                source.smptbl_id = 0;
            } catch (const std::exception& e) {
                ERRORLOG("%s", e.what());
            }
        }
    }
}

extern "C" SoundSFXID get_sample_sfxid(SoundSmplTblID smptbl_id) {
    if (smptbl_id >= g_custom_offset) {
        return 0;
    } else if (smptbl_id >= g_speech_offset) {
        const SoundSmplTblID idx = smptbl_id - g_speech_offset;
        if (idx <= 0 || idx >= (SoundSmplTblID)g_banks[1].size())
            return 0;
        return g_banks[1][idx].sfx_id;
    } else {
        if (smptbl_id <= 0 || smptbl_id >= (SoundSmplTblID)g_banks[0].size())
            return 0;
        return g_banks[0][smptbl_id].sfx_id;
    }
}

extern "C" SoundSmplTblID get_speech_offset(void) {
    return g_speech_offset;
}
extern "C" SoundSmplTblID get_custom_offset(void) {
    return g_custom_offset;
}

extern "C" int InitialiseSDLAudio() {
    if (!SDL_Init(SDL_INIT_AUDIO)) {
        ERRORLOG("Unable to initialise SDL audio subsystem: %s", SDL_GetError());
        return 0;
    }
    if (!MIX_Init()) {
        ERRORLOG("Could not initialise SDL3_mixer: %s", SDL_GetError());
        return 0;
    }
    SDL_AudioSpec spec;
    spec.format   = SDL_AUDIO_S16;
    spec.channels = 2;
    spec.freq     = 44100;
    g_mixer = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec);
    if (!g_mixer) {
        ERRORLOG("Could not open audio device for SDL mixer: %s", SDL_GetError());
        MIX_Quit();
        return 0;
    }
    g_music_track = MIX_CreateTrack(g_mixer);
    g_speech_track = MIX_CreateTrack(g_mixer);
    if (!g_music_track || !g_speech_track) {
        ERRORLOG("Could not create SDL mixer tracks: %s", SDL_GetError());
        MIX_DestroyMixer(g_mixer);
        g_mixer = nullptr;
        MIX_Quit();
        return 0;
    }
    return 1;
}

extern "C" void ShutDownSDLAudio() {
    g_music_track = nullptr;
    g_speech_track = nullptr;
    if (g_mixer) {
        MIX_DestroyMixer(g_mixer);  // also destroys all tracks created for this mixer
        g_mixer = nullptr;
    }
    MIX_Quit();
}

extern "C" TbBool play_streamed_sample(const char* fname, SoundVolume volume) {
    if (SoundDisabled || fname == nullptr || strlen(fname) == 0 || !g_mixer || !g_speech_track)
        return false;
    MIX_Audio* audio = MIX_LoadAudio(g_mixer, fname, true);
    if (!audio) {
        ERRORLOG("Cannot load \"%s\": %s", fname, SDL_GetError());
        return false;
    }
    MIX_SetTrackAudio(g_speech_track, audio);
    MIX_SetTrackGain(g_speech_track, float(volume) / FULL_LOUDNESS);
    if (!MIX_PlayTrack(g_speech_track, 0)) {
        MIX_DestroyAudio(audio);
        ERRORLOG("Cannot play \"%s\": %s", fname, SDL_GetError());
        return false;
    }
    std::lock_guard<std::mutex> guard(g_mix_mutex);
    MIX_Audio* old = std::exchange(g_speech_audio, audio);
    if (old)
        MIX_DestroyAudio(old);
    return true;
}

extern "C" void stop_streamed_samples() {
    if (g_speech_track) MIX_StopTrack(g_speech_track, 0);
    std::lock_guard<std::mutex> guard(g_mix_mutex);
    MIX_Audio* old = std::exchange(g_speech_audio, nullptr);
    if (old)
        MIX_DestroyAudio(old);
}

extern "C" void set_streamed_sample_volume(SoundVolume volume) {
    if (g_speech_track)
        MIX_SetTrackGain(g_speech_track, float(volume) / FULL_LOUDNESS);
}

extern "C" TbBool is_speech_playing(void) {
    return g_speech_track && MIX_TrackPlaying(g_speech_track);
}

extern "C" void toggle_bbking_mode() {
    g_bb_king_mode = !g_bb_king_mode;
}

// Bridge functions for custom sound loading from C++ sound_manager
extern "C" int custom_sound_bank_size() {
    return g_custom_bank.size();
}

// Decode data as MP3 (via dr_mp3) and push it into g_custom_bank.
// data/size may point into a larger buffer (e.g. after stripping a BMU header).
static TbBool decode_mp3_and_store(const char* filepath, int sample_id, const uint8_t* data, size_t size) {
    drmp3_config cfg = {};
    drmp3_uint64 frame_count = 0;
    drmp3_int16* mp3_pcm = drmp3_open_memory_and_read_pcm_frames_s16(data, size, &cfg, &frame_count, nullptr);
    if (!mp3_pcm || frame_count == 0) {
        if (mp3_pcm)
            drmp3_free(mp3_pcm, nullptr);
        ERRORLOG("Cannot decode MP3 data from %s", filepath);
        return false;
    }

    const ALenum al_fmt = (cfg.channels == 1) ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16;
    const size_t byte_count = (size_t)frame_count * cfg.channels * sizeof(drmp3_int16);

    try {
        const std::vector<uint8_t> pcm(reinterpret_cast<const uint8_t*>(mp3_pcm),
                                       reinterpret_cast<const uint8_t*>(mp3_pcm) + byte_count);
        g_custom_bank.push_back(sound_sample(filepath, sample_id, pcm, al_fmt, (int)cfg.sampleRate));
    } catch (...) {
        drmp3_free(mp3_pcm, nullptr);
        return false;
    }
    drmp3_free(mp3_pcm, nullptr);
    return true;
}

// Load a WAV, OGG, FLAC, or MP3 file and append it to g_custom_bank as a signed-16-bit
// OpenAL buffer.
//   - WAV              : SDL_LoadWAV_IO + SDL_AudioStream S16 conversion → OpenAL buffer
//   - Plain MP3        : dr_mp3 single-header decoder (compiled in, no external DLLs)
//   - BMU V1.0         : 8-byte wrapper used by some campaigns; contains a plain MP3
//                        stream after the header — stripped and decoded via dr_mp3.
extern "C" TbBool custom_sound_load_wav(const char* filepath, int sample_id) {
    // Resolve to absolute path so the decoders can find the file regardless of
    // process CWD (keeperfx changes directories at startup).
    char abs_buf[4096];
#ifdef _WIN32
    if (_fullpath(abs_buf, filepath, sizeof(abs_buf)) == nullptr)
        snprintf(abs_buf, sizeof(abs_buf), "%s", filepath);
#else
    if (realpath(filepath, abs_buf) == nullptr)
        snprintf(abs_buf, sizeof(abs_buf), "%s", filepath);
#endif

    // Read the whole file once so we can inspect the magic bytes and route to
    // the right decoder without reopening.
    std::vector<uint8_t> file_data;
    {
        std::ifstream f(abs_buf, std::ios::binary | std::ios::ate);
        if (!f) {
            ERRORLOG("Cannot open audio file %s", filepath);
            return false;
        }
        const auto sz = f.tellg();
        if (sz <= 0) {
            ERRORLOG("Empty audio file %s", filepath);
            return false;
        }
        file_data.resize((size_t)sz);
        f.seekg(0);
        if (!f.read(reinterpret_cast<char*>(file_data.data()), sz)) {
            ERRORLOG("Cannot read audio file %s", filepath);
            return false;
        }
    }

    const uint8_t* data = file_data.data();
    const size_t size = file_data.size();

    // Detect BMU V1.0 wrapper (8-byte ASCII prefix used by some campaigns).
    // After the prefix the payload is a standard MP3 (often with an ID3 tag).
    static const uint8_t bmu_magic[8] = {'B', 'M', 'U', ' ', 'V', '1', '.', '0'};
    if (size > 8 && memcmp(data, bmu_magic, 8) == 0) {
        return decode_mp3_and_store(filepath, sample_id, data + 8, size - 8);
    }

    // Detect MP3 by ID3v2 tag or sync-word (0xFF 0xE? / 0xFF 0xF?).
    // Also handle plain .mp3 extension as a hint.
    const bool looks_like_mp3 = (size >= 3 && data[0] == 'I' && data[1] == 'D' && data[2] == '3') ||
                                (size >= 2 && data[0] == 0xFF && (data[1] & 0xE0) == 0xE0);
    if (looks_like_mp3) {
        return decode_mp3_and_store(filepath, sample_id, data, size);
    }

    // --- WAV path: SDL_LoadWAV_IO + SDL_AudioStream conversion to S16 for OpenAL ---
    // OGG/FLAC custom sounds are not supported here — SDL3_mixer 3.2 no longer exposes
    // raw PCM from MIX_Audio. Use WAV or MP3 for custom sounds.
    SDL_IOStream* rw = SDL_IOFromConstMem(data, (int)size);
    if (!rw) {
        ERRORLOG("Cannot create IOStream for %s: %s", filepath, SDL_GetError());
        return false;
    }

    SDL_AudioSpec wav_spec = {};
    Uint8* wav_data = nullptr;
    Uint32 wav_len = 0;
    if (!SDL_LoadWAV_IO(rw, true, &wav_spec, &wav_data, &wav_len)) {
        ERRORLOG("Cannot decode audio file %s (WAV expected; OGG/FLAC not supported in custom bank): %s", filepath, SDL_GetError());
        return false;
    }

    SDL_AudioSpec dst_spec = { SDL_AUDIO_S16, wav_spec.channels, wav_spec.freq };
    SDL_AudioStream* stream = SDL_CreateAudioStream(&wav_spec, &dst_spec);
    if (!stream) {
        SDL_free(wav_data);
        ERRORLOG("Cannot create audio conversion stream for %s: %s", filepath, SDL_GetError());
        return false;
    }
    SDL_PutAudioStreamData(stream, wav_data, (int)wav_len);
    SDL_FlushAudioStream(stream);
    SDL_free(wav_data);

    const int available = SDL_GetAudioStreamAvailable(stream);
    const ALenum al_fmt = (wav_spec.channels == 1) ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16;
    try {
        std::vector<uint8_t> pcm((size_t)available);
        SDL_GetAudioStreamData(stream, pcm.data(), available);
        SDL_DestroyAudioStream(stream);
        g_custom_bank.push_back(sound_sample(filepath, sample_id, pcm, al_fmt, wav_spec.freq));
    } catch (...) {
        SDL_DestroyAudioStream(stream);
        ERRORLOG("Out of memory buffering audio %s", filepath);
        return false;
    }
    return true;
}