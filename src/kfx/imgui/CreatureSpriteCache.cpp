/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file CreatureSpriteCache.cpp
 *     Self-contained CPU cache of decoded creature sprite frames — implementation.
 */
/******************************************************************************/
#include "pre_inc.h"

#include "kfx/imgui/CreatureSpriteCache.h"

#include <atomic>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "bflib_basics.h"
#include "bflib_fileio.h"
#include "config.h"
#include "creature_graphics.h"   // creature_table, creature_table_length, creature_table_add
#include "engine_render.h"       // keepersprite_add, KEEPERSPRITE_ADD_OFFSET/NUM, TbSpriteData

#include "post_inc.h"
/******************************************************************************/

namespace {

/** Max supported sprite dimension (matches the engine's keeper-sprite scratch). */
constexpr int k_max_dim = 256;

struct Frame {
    std::vector<uint8_t> pixels;   // w*h palette indices, row stride = w
    int w = 0;
    int h = 0;
};

std::mutex                          s_mutex;        // guards s_frames
std::unordered_map<int, Frame>      s_frames;       // kspr index -> decoded frame

std::atomic<bool> s_want_load{false};   // set by RequestLoad (any thread)
std::atomic<int>  s_data_gen{0};        // bumped by Invalidate (data changed)
std::atomic<int>  s_view_gen{0};        // bumped when s_frames changes (for viewer)
int               s_loaded_gen = -1;    // s_data_gen value the cache reflects (game thread only)

/** Decode keeper-sprite RLE into a tight w*h palette-index buffer.
 *  Format is identical to TbSprite.Data: negative cmd = transparent skip,
 *  positive cmd = run of palette bytes, 0 = end of row. `end` bounds the input. */
void decode_rle(uint8_t* dst, const uint8_t* data, const uint8_t* end, int w, int h)
{
    std::memset(dst, 0, (size_t)w * h);
    const signed char* sp     = reinterpret_cast<const signed char*>(data);
    const signed char* sp_end = reinterpret_cast<const signed char*>(end);
    for (int y = 0; y < h; ++y) {
        uint8_t* row = dst + (size_t)y * w;
        int x = 0;
        while (true) {
            if (sp >= sp_end) return;
            signed char cmd = *sp++;
            if (cmd == 0) break;              // end of row
            if (cmd < 0) {
                x += (int)(-cmd);             // transparent run
            } else {
                int count = (int)cmd;
                for (int i = 0; i < count; ++i) {
                    if (sp >= sp_end) return;
                    if (x < w) row[x] = (uint8_t)(*sp);
                    ++sp;
                    ++x;
                }
            }
        }
    }
}

bool frame_dims_ok(const struct KeeperSprite& ks)
{
    return ks.SWidth > 0 && ks.SHeight > 0 &&
           ks.SWidth <= k_max_dim && ks.SHeight <= k_max_dim;
}

/** Read the whole creature.jty into memory (with trailing zero padding so a
 *  slightly-overrunning decode can never read past the buffer). Returns empty
 *  on failure. */
std::vector<uint8_t> read_jty_file()
{
    std::vector<uint8_t> buf;
    const char* fname = prepare_file_path(FGrp_StdData, "creature.jty");
    if (fname == nullptr)
        return buf;
    long len = LbFileLength(fname);
    if (len <= 0)
        return buf;
    TbFileHandle fh = LbFileOpen(fname, Lb_FILE_MODE_READ_ONLY);
    if (fh == NULL)
        return buf;
    buf.resize((size_t)len + k_max_dim);   // pad tail with zeros
    std::memset(buf.data() + len, 0, k_max_dim);
    long got = LbFileRead(fh, buf.data(), len);
    LbFileClose(fh);
    if (got != len) {
        WARNLOG("CreatureSpriteCache: short read of creature.jty (%ld/%ld)", got, len);
        buf.clear();
        return buf;
    }
    return buf;
}

/** Rebuild the whole frame map from disk + RAM. Runs on the game thread. */
void do_load(int for_gen)
{
    std::unordered_map<int, Frame> frames;

    // --- Vanilla frames: decoded straight from our private creature.jty read. ---
    std::vector<uint8_t> jty = read_jty_file();
    if (!jty.empty() && creature_table != nullptr) {
        const uint8_t* base = jty.data();
        const uint8_t* end  = jty.data() + jty.size();
        for (size_t i = 0; i < creature_table_length; ++i) {
            const struct KeeperSprite& ks = creature_table[i];
            if (!frame_dims_ok(ks))
                continue;
            if ((size_t)ks.DataOffset >= jty.size())
                continue;
            Frame f;
            f.w = ks.SWidth;
            f.h = ks.SHeight;
            f.pixels.resize((size_t)f.w * f.h);
            decode_rle(f.pixels.data(), base + ks.DataOffset, end, f.w, f.h);
            frames[(int)i] = std::move(f);
        }
    }

    // --- Custom sprites: pixel blobs already resident in RAM (KfxAlloc'd). ---
    for (int i = 0; i < KEEPERSPRITE_ADD_NUM; ++i) {
        const TbSpriteData data = keepersprite_add[i];
        if (data == nullptr)
            continue;
        const struct KeeperSprite& ks = creature_table_add[i];
        if (!frame_dims_ok(ks))
            continue;
        // Upper bound for decode: generous worst-case for valid RLE.
        const uint8_t* d   = reinterpret_cast<const uint8_t*>(data);
        const uint8_t* end = d + (size_t)ks.SWidth * ks.SHeight * 3 + ks.SHeight + 1;
        Frame f;
        f.w = ks.SWidth;
        f.h = ks.SHeight;
        f.pixels.resize((size_t)f.w * f.h);
        decode_rle(f.pixels.data(), d, end, f.w, f.h);
        frames[KEEPERSPRITE_ADD_OFFSET + i] = std::move(f);
    }

    const size_t count = frames.size();
    {
        std::lock_guard<std::mutex> lk(s_mutex);
        s_frames.swap(frames);
    }
    s_loaded_gen = for_gen;
    s_view_gen.fetch_add(1);
    SYNCLOG("CreatureSpriteCache: loaded %zu creature sprite frames", count);
}

} // namespace

/******************************************************************************/

extern "C" void CreatureSpriteCache_RequestLoad(void)
{
    s_want_load.store(true);
}

extern "C" void CreatureSpriteCache_Invalidate(void)
{
    s_data_gen.fetch_add(1);
}

extern "C" void CreatureSpriteCache_Service(void)
{
    if (!s_want_load.load())
        return;
    const int gen = s_data_gen.load();
    if (s_loaded_gen == gen) {
        s_want_load.store(false);   // already current
        return;
    }
    // Creature data is only present inside a level; wait until it is loaded.
    if (creature_table == nullptr || creature_table_length == 0)
        return;

    do_load(gen);
    s_want_load.store(false);
}

extern "C" int CreatureSpriteCache_GetGeneration(void)
{
    return s_view_gen.load();
}

extern "C" int CreatureSpriteCache_GetCount(void)
{
    std::lock_guard<std::mutex> lk(s_mutex);
    return (int)s_frames.size();
}

extern "C" int CreatureSpriteCache_GetFrame(int kspr_idx, unsigned char* out_pixels,
                                            int out_cap, int* out_w, int* out_h)
{
    std::lock_guard<std::mutex> lk(s_mutex);
    auto it = s_frames.find(kspr_idx);
    if (it == s_frames.end())
        return 0;
    const Frame& f = it->second;
    if (out_w) *out_w = f.w;
    if (out_h) *out_h = f.h;
    if (out_pixels != nullptr) {
        const int need = f.w * f.h;
        if (out_cap < need || (int)f.pixels.size() < need)
            return 0;
        std::memcpy(out_pixels, f.pixels.data(), (size_t)need);
    }
    return 1;
}
/******************************************************************************/
