/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file ImGuiSpriteAtlasPanel.cpp
 *     ImGui debug viewer for the desktop-GL sprite atlas — implementation.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "kfx/imgui/ImGuiSpriteAtlasPanel.hpp"

#ifdef KEEPERFX_IMGUI_ENABLED

#include "kfx/imgui/SpriteMaterialise.hpp"
#include "renderer/RendererManager.h"

#include <imgui.h>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#ifdef RENDERER_OPENGL_ENABLED
#  include <glad/glad.h>
#  include <unordered_map>
#  include "renderer/opengl/GLSpriteAtlas.h"
#  include "kfx/imgui/CreatureSpriteInfo.h"
#  include "kfx/imgui/CreatureSpriteCache.h"
#  include "kfx/assets/FxSprSheet.h"
#  include "kfx/assets/VariantCatalogue.h"
#  include "kfx/imgui/NativeFileDialog.hpp"
#  include "config.h"                 // prepare_file_path, FGrp_StdData
#  include <map>
#  include <utility>
#  include <cstring>
#  include <cstdlib>
#endif

#include "post_inc.h"

/******************************************************************************/

namespace {

bool  s_visible  = false;
int   s_mode     = (int)SpriteMaterialiseMode::AsDrawn;
float s_zoom     = 1.0f;   // full-atlas zoom
float s_thumb_h  = 48.0f;  // sprite-grid thumbnail height in pixels
int   s_selected = -1;

#ifdef RENDERER_OPENGL_ENABLED
GLuint        s_texture   = 0;
int           s_tex_w     = 0;
int           s_tex_h     = 0;
AtlasSnapshot s_snap;
bool          s_have_snap = false;
int           s_last_mode = -1;
size_t        s_last_count = (size_t)-1;

// Re-snapshot the atlas and (re)upload a full-atlas RGBA texture in the current
// mode.  Runs on the render thread (caller is inside DebugOverlay_Render), so
// GL calls are valid here.
void RebuildTexture()
{
    GLSpriteAtlas* atlas = RendererGetSpriteAtlas();
    if (!atlas) { s_have_snap = false; return; }

    atlas->Snapshot(s_snap);
    s_have_snap = true;
    if (s_snap.pixels.empty() || s_snap.width <= 0 || s_snap.height <= 0)
        return;

    const unsigned char* pal = RendererGetActivePalette();
    std::vector<uint8_t> rgba = SpriteMaterialiseRGBA(
        s_snap.pixels.data(), s_snap.width, s_snap.height, s_snap.width,
        pal, (SpriteMaterialiseMode)s_mode);
    if (rgba.empty())
        return;

    if (s_texture == 0)
        glGenTextures(1, &s_texture);
    glBindTexture(GL_TEXTURE_2D, s_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, s_snap.width, s_snap.height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    glBindTexture(GL_TEXTURE_2D, 0);

    s_tex_w     = s_snap.width;
    s_tex_h     = s_snap.height;
    s_last_mode = s_mode;
}

void DrawSpriteGrid()
{
    if (!s_have_snap || s_texture == 0) {
        ImGui::TextWrapped("Atlas not available yet.");
        return;
    }

    ImGui::SliderFloat("Thumbnail height", &s_thumb_h, 16.0f, 128.0f, "%.0f px");
    ImGui::Separator();

    ImGui::BeginChild("sprite_grid", ImVec2(0, 0), true,
                      ImGuiWindowFlags_HorizontalScrollbar);

    const ImTextureID tex   = (ImTextureID)(intptr_t)s_texture;
    const float       avail = ImGui::GetContentRegionAvail().x;
    const float       spacing = ImGui::GetStyle().ItemSpacing.x;
    float             line_x = 0.0f;

    for (size_t i = 0; i < s_snap.entries.size(); ++i) {
        const AtlasEntry& e = s_snap.entries[i];
        const int   pw = e.uv.pixel_w;
        const int   ph = e.uv.pixel_h;
        if (pw <= 0 || ph <= 0)
            continue;

        const float scale = s_thumb_h / (float)ph;
        const ImVec2 sz((float)pw * scale, (float)ph * scale);

        if (line_x > 0.0f && line_x + sz.x > avail) {
            line_x = 0.0f;               // wrap to next row
        } else if (i > 0) {
            ImGui::SameLine();
        }

        ImGui::PushID((int)i);
        const bool selected = ((int)i == s_selected);
        ImVec4 tint = selected ? ImVec4(1, 1, 0.5f, 1) : ImVec4(1, 1, 1, 1);
        if (ImGui::ImageButton("spr", tex, sz,
                               ImVec2(e.uv.u0, e.uv.v0),
                               ImVec2(e.uv.u1, e.uv.v1),
                               ImVec4(0, 0, 0, 0), tint)) {
            s_selected = (int)i;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::Text("Index %u", (unsigned)i);
            ImGui::Text("Handle 0x%08X", (unsigned)e.handle);
            ImGui::Text("Size %dx%d", pw, ph);
            ImGui::Text("UV [%.4f, %.4f] - [%.4f, %.4f]",
                        e.uv.u0, e.uv.v0, e.uv.u1, e.uv.v1);
            ImGui::EndTooltip();
        }
        ImGui::PopID();

        line_x += sz.x + spacing;
    }

    ImGui::EndChild();
}

// ---------------------------------------------------------------------------
// Creature (keeper-sprite) view: grouped creature -> action -> frames.
// Source = CreatureSpriteCache, a self-contained CPU cache that decodes every
// creature frame from its own private read of data/creature.jty. It touches no
// game state, so frames appear without any creature being drawn on screen.
// ---------------------------------------------------------------------------

std::unordered_map<int, GLuint>              s_kspr_tex;   // kspr_index -> RGBA texture
int                                          s_kspr_view_gen  = -1; // cache gen our textures reflect
int                                          s_kspr_tex_mode  = -1; // materialise mode of our textures

void ClearKeeperTextures()
{
    for (auto& kv : s_kspr_tex)
        if (kv.second) glDeleteTextures(1, &kv.second);
    s_kspr_tex.clear();
}

// Drop cached GL textures when the cache contents or the display mode change.
void SyncKeeperTextures()
{
    const int gen = CreatureSpriteCache_GetGeneration();
    if (gen != s_kspr_view_gen || s_mode != s_kspr_tex_mode) {
        ClearKeeperTextures();
        s_kspr_view_gen = gen;
        s_kspr_tex_mode = s_mode;
    }
}

// Lazily materialise one cached keeper sprite (by engine index) into an RGBA GL
// texture in the current mode. Returns 0 when the sprite is not cached.
GLuint KeeperTextureForIndex(int kspr_index, int* out_w, int* out_h)
{
    int w = 0, h = 0;
    if (!CreatureSpriteCache_GetFrame(kspr_index, nullptr, 0, &w, &h))
        return 0;
    if (out_w) *out_w = w;
    if (out_h) *out_h = h;

    auto tit = s_kspr_tex.find(kspr_index);
    if (tit != s_kspr_tex.end())
        return tit->second;

    if (w <= 0 || h <= 0)
        return 0;

    std::vector<uint8_t> pixels((size_t)w * h);
    if (!CreatureSpriteCache_GetFrame(kspr_index, pixels.data(), (int)pixels.size(), &w, &h))
        return 0;

    const unsigned char* pal = RendererGetActivePalette();
    std::vector<uint8_t> rgba = SpriteMaterialiseRGBA(
        pixels.data(), w, h, w, pal, (SpriteMaterialiseMode)s_mode);
    if (rgba.empty())
        return 0;

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    s_kspr_tex[kspr_index] = tex;
    return tex;
}

// Draw the frame strip for one animation (all rotation groups x frames).
void DrawAnimationFrames(int base, int frames, int groups, int uid)
{
    const float ph_h = s_thumb_h;
    for (int g = 0; g < groups; ++g) {
        if (groups > 1)
            ImGui::Text("  dir %d", g);
        for (int f = 0; f < frames; ++f) {
            const int idx = base + g * frames + f;
            int w = 0, h = 0;
            GLuint tex = KeeperTextureForIndex(idx, &w, &h);

            if (f > 0) ImGui::SameLine();
            ImGui::PushID(uid * 4096 + g * 64 + f);
            if (tex != 0 && h > 0) {
                const float scale = ph_h / (float)h;
                ImGui::Image((ImTextureID)(intptr_t)tex,
                             ImVec2((float)w * scale, ph_h));
                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::Text("kspr index %d", idx);
                    ImGui::Text("frame %d / %d", f, frames);
                    ImGui::Text("size %dx%d", w, h);
                    ImGui::EndTooltip();
                }
            } else {
                ImGui::Dummy(ImVec2(ph_h, ph_h));
                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::Text("kspr index %d", idx);
                    ImGui::TextUnformatted("not cached\n(enter a level to load)");
                    ImGui::EndTooltip();
                }
            }
            ImGui::PopID();
        }
    }
}

void DrawCreaturesTab()
{
    // Ask the game thread to (re)build the self-contained cache while this tab
    // is open. Cheap and idempotent; the actual load happens off this thread and
    // touches no game state.
    CreatureSpriteCache_RequestLoad();
    SyncKeeperTextures();

    ImGui::TextWrapped("Creature sprites are decoded from data/creature.jty into "
                       "a self-contained debug cache. Enter a level to populate "
                       "this view \u2014 no creature needs to be on screen.");

    const int count = CreatureSpriteCache_GetCount();
    ImGui::Text("%d creature sprite frames cached", count);
    ImGui::SliderFloat("Frame height", &s_thumb_h, 16.0f, 128.0f, "%.0f px");
    ImGui::Separator();

    if (count == 0) {
        ImGui::TextWrapped("No creature sprites cached yet. Enter a level; the "
                           "cache loads automatically within a frame or two.");
        return;
    }

    ImGui::BeginChild("creature_list", ImVec2(0, 0), true);
    const int models  = dbg_creature_model_count();
    const int actions = dbg_creature_action_count();
    for (int m = 0; m < models; ++m) {
        if (!dbg_creature_has_graphics(m))
            continue;
        const char* name = dbg_creature_name(m);
        ImGui::PushID(m);
        if (ImGui::CollapsingHeader(name ? name : "?")) {
            for (int a = 0; a < actions; ++a) {
                const int anim = dbg_creature_action_anim(m, a);
                if (anim < 0)
                    continue;
                const int base   = dbg_anim_base_index(anim);
                const int frames = dbg_anim_frames(anim);
                const int rot    = dbg_anim_rotable(anim);
                const int groups = dbg_anim_rot_groups(rot);
                ImGui::Text("%-12s  anim %d  frames %d  rotable %d",
                            dbg_action_name(a), anim, frames, rot);
                if (frames > 0)
                    DrawAnimationFrames(base, frames, groups, m * 64 + a);
                ImGui::Separator();
            }
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
}

// ---------------------------------------------------------------------------
// FXSPR view: loads a standalone `.fxspr` truecolour container off disk and
// uploads each sprite's RGBA8 payload directly (no palette materialise — the
// container is already truecolour). This validates the kfx::FxSprSheet loader
// end-to-end in-engine, independent of the game render path.
// ---------------------------------------------------------------------------

kfx::FxSprSheet                 s_fxspr;
bool                            s_fxspr_loaded  = false;
bool                            s_fxspr_ok      = false;
std::string                     s_fxspr_status;
std::unordered_map<int, GLuint> s_fxspr_tex;    // entry index -> RGBA texture
char                            s_fxspr_path[256] = "fxspr/gui1-32.fxspr";

// ── Comparison viewer state (A7) ──────────────────────────────────────────────
int         s_fxspr_selected = -1;      // selected entry in the current sheet
bool        s_catalogue_loaded = false; // VariantCatalogue::loadDefaultPacks() done
char        s_fxspr_search[128] = "";
std::vector<std::string> s_search_hits;

// Lazily-loaded sheets + textures for the OTHER variant files referenced by the
// catalogue (keyed by their FGrp_StdData-relative path / "file#entry").
std::map<std::string, kfx::FxSprSheet> s_variant_sheets;
std::map<std::string, GLuint>          s_variant_tex;

void ClearFxsprTextures()
{
    for (auto& kv : s_fxspr_tex)
        if (kv.second) glDeleteTextures(1, &kv.second);
    s_fxspr_tex.clear();
}

void ClearVariantCaches()
{
    for (auto& kv : s_variant_tex)
        if (kv.second) glDeleteTextures(1, &kv.second);
    s_variant_tex.clear();
    s_variant_sheets.clear();
}

// Strip directory, ".fxspr", an optional "-tc"/"-idx" colour marker and any
// scale suffix from the current path to get the collection base (mirrors
// tools/fxspr/make_manifest.py split_base_scale). Works for absolute paths
// (e.g. from the Browse dialog) as well as data-relative ones.
std::string FxsprCurrentBase()
{
    std::string p = s_fxspr_path;
    const size_t slash = p.find_last_of("/\\");
    std::string stem = (slash == std::string::npos) ? p : p.substr(slash + 1);
    const size_t dot = stem.rfind('.');
    if (dot != std::string::npos)
        stem = stem.substr(0, dot);
    // Colour marker first (the colour itself is read from the file header).
    for (const char* mk : {"-tc", "-idx"}) {
        const size_t ml = std::strlen(mk);
        if (stem.size() > ml && stem.compare(stem.size() - ml, ml, mk) == 0) {
            stem = stem.substr(0, stem.size() - ml);
            break;
        }
    }
    for (const char* suf : {"-128", "-64", "-32"}) {
        const size_t sl = std::strlen(suf);
        if (stem.size() > sl && stem.compare(stem.size() - sl, sl, suf) == 0)
            return stem.substr(0, stem.size() - sl);
    }
    if ((stem.rfind("pointer", 0) == 0 || stem.rfind("points", 0) == 0) &&
        stem.size() > 2) {
        const std::string tail = stem.substr(stem.size() - 2);
        if (tail == "32" || tail == "64")
            return stem.substr(0, stem.size() - 2);
    }
    return stem;
}

// Get (loading + caching) an RGBA texture for `entry` of the variant `file`
// (FGrp_StdData-relative). Returns 0 if unavailable; fills out_w/out_h.
GLuint VariantTextureFor(const std::string& file, int entry, int* out_w, int* out_h)
{
    if (out_w) *out_w = 0;
    if (out_h) *out_h = 0;

    auto sit = s_variant_sheets.find(file);
    if (sit == s_variant_sheets.end()) {
        kfx::FxSprSheet sheet;
        const char* full = prepare_file_path(FGrp_StdData, file.c_str());
        if (full != nullptr)
            sheet.loadFromFile(full);
        sit = s_variant_sheets.emplace(file, std::move(sheet)).first;
    }
    kfx::FxSprSheet& sheet = sit->second;
    if (!sheet.valid())
        return 0;

    kfx::FxSprSprite spr;
    if (!sheet.sprite(entry, spr))
        return 0;
    if (out_w) *out_w = spr.width;
    if (out_h) *out_h = spr.height;
    if (spr.width == 0 || spr.height == 0 || spr.rgba == nullptr)
        return 0;

    char key[288];
    snprintf(key, sizeof(key), "%s#%d", file.c_str(), entry);
    auto tit = s_variant_tex.find(key);
    if (tit != s_variant_tex.end())
        return tit->second;

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, spr.width, spr.height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, spr.rgba);
    glBindTexture(GL_TEXTURE_2D, 0);
    s_variant_tex[key] = tex;
    return tex;
}

void LoadFxspr()
{
    ClearFxsprTextures();
    ClearVariantCaches();
    s_fxspr = kfx::FxSprSheet();
    s_fxspr_loaded = true;
    s_fxspr_ok = false;
    s_fxspr_selected = -1;

    // An absolute path (e.g. from the Browse dialog) is loaded directly;
    // otherwise the path is treated as relative to the game's data/ folder.
    auto is_absolute = [](const char* p) -> bool {
        if (p == nullptr || p[0] == '\0') return false;
        if (p[0] == '/' || p[0] == '\\') return true;
        return (p[1] == ':' && (p[2] == '\\' || p[2] == '/')); // C:\ or C:/
    };
    const char* full = is_absolute(s_fxspr_path)
                           ? s_fxspr_path
                           : prepare_file_path(FGrp_StdData, s_fxspr_path);
    if (full == nullptr) {
        s_fxspr_status = "Could not resolve data path.";
        return;
    }
    s_fxspr_ok = s_fxspr.loadFromFile(full);
    if (s_fxspr_ok) {
        char buf[128];
        snprintf(buf, sizeof(buf), "Loaded: %d entries, kind %u, flags 0x%04X",
                 s_fxspr.count(), (unsigned)s_fxspr.kind(), (unsigned)s_fxspr.flags());
        s_fxspr_status = buf;
    } else {
        s_fxspr_status = "Load failed (see log). Check the path is under data/.";
    }
}

GLuint FxsprTextureForIndex(int index, int* out_w, int* out_h)
{
    kfx::FxSprSprite spr;
    if (!s_fxspr.sprite(index, spr))
        return 0;
    if (out_w) *out_w = spr.width;
    if (out_h) *out_h = spr.height;
    if (spr.width == 0 || spr.height == 0 || spr.rgba == nullptr)
        return 0;

    auto it = s_fxspr_tex.find(index);
    if (it != s_fxspr_tex.end())
        return it->second;

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, spr.width, spr.height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, spr.rgba);
    glBindTexture(GL_TEXTURE_2D, 0);
    s_fxspr_tex[index] = tex;
    return tex;
}

// Draw the side-by-side variant strip for the currently selected entry.
void DrawComparison()
{
    kfx::VariantCatalogue& cat = kfx::VariantCatalogue::instance();
    const std::string base = FxsprCurrentBase();

    ImGui::TextDisabled("Comparison");
    if (s_fxspr_selected < 0) {
        ImGui::TextWrapped("Click a sprite in the grid to compare its quality "
                           "variants.");
        return;
    }

    kfx::SpriteIdentity id;
    if (!cat.resolve(base, s_fxspr_selected, id)) {
        ImGui::TextWrapped("No catalogue entry for %s/%d. (Is spritepacks/"
                           "base.json deployed?)", base.c_str(), s_fxspr_selected);
        return;
    }

    ImGui::Text("ID: %s", id.id.c_str());
    if (!id.name.empty())
        ImGui::Text("Name: %s", id.name.c_str());
    if (!id.category.empty())
        ImGui::Text("Category: %s", id.category.c_str());
    if (!id.tags.empty()) {
        std::string tags;
        for (const std::string& t : id.tags) {
            if (!tags.empty()) tags += ", ";
            tags += t;
        }
        ImGui::Text("Tags: %s", tags.c_str());
    }
    ImGui::Text("%zu variant(s)", id.variants.size());
    ImGui::Separator();

    for (const kfx::SpriteVariant& v : id.variants) {
        int w = 0, h = 0;
        GLuint tex = VariantTextureFor(v.file, v.entry, &w, &h);

        ImGui::PushID(v.file.c_str());
        ImGui::BeginGroup();
        const char* colour = (v.colour == kfx::FxColourKind::Truecolour)
                                 ? "truecolour" : "indexed";
        if (v.scale > 0)
            ImGui::Text("%dpx %s", v.scale, colour);
        else
            ImGui::Text("%s", colour);

        if (tex != 0 && w > 0 && h > 0) {
            const float target = 96.0f;
            const float scale = (h > 0) ? (target / (float)h) : 1.0f;
            ImGui::Image((ImTextureID)(intptr_t)tex,
                         ImVec2((float)w * scale, (float)h * scale));
        } else {
            ImGui::TextDisabled("[missing]");
        }
        ImGui::Text("%dx%d", w, h);
        if (!v.provenance.empty())
            ImGui::TextDisabled("%s", v.provenance.c_str());
        ImGui::EndGroup();
        ImGui::PopID();
        ImGui::SameLine();
    }
    ImGui::NewLine();
}

void DrawFxsprTab()
{
    if (!s_catalogue_loaded) {
        kfx::VariantCatalogue::instance().loadDefaultPacks();
        s_catalogue_loaded = true;
    }

    ImGui::SetNextItemWidth(360.0f);
    ImGui::InputText("Path (under data/)", s_fxspr_path, sizeof(s_fxspr_path));
    ImGui::SameLine();
    if (ImGui::Button("Load") || !s_fxspr_loaded)
        LoadFxspr();
    ImGui::SameLine();
    if (ImGui::Button("Browse...")) {
        // Start in the game's fxspr data folder if it resolves, else data/.
        char initial[512] = "";
        const char* dir = prepare_file_path(FGrp_StdData, "fxspr");
        if (dir != nullptr)
            snprintf(initial, sizeof(initial), "%s", dir);
        char picked[512] = "";
        if (kfx::OpenFileDialog("Open .fxspr sprite sheet", initial,
                                "FXSPR sprites|*.fxspr|All files|*.*",
                                picked, sizeof(picked)) && picked[0] != '\0') {
            snprintf(s_fxspr_path, sizeof(s_fxspr_path), "%s", picked);
            LoadFxspr();
        }
    }

    // Search-by-name / id across the catalogue.
    ImGui::SetNextItemWidth(240.0f);
    ImGui::InputText("Search", s_fxspr_search, sizeof(s_fxspr_search));
    ImGui::SameLine();
    if (ImGui::Button("Find"))
        s_search_hits = kfx::VariantCatalogue::instance().search(s_fxspr_search);
    if (!s_search_hits.empty()) {
        ImGui::SameLine();
        if (ImGui::Button("Clear")) s_search_hits.clear();
        ImGui::BeginChild("fxspr_hits", ImVec2(0, 80), true);
        for (const std::string& hit : s_search_hits) {
            if (ImGui::Selectable(hit.c_str())) {
                // "base/entry" -> load base's first variant + select entry.
                const size_t sl = hit.rfind('/');
                if (sl != std::string::npos) {
                    kfx::SpriteIdentity id;
                    if (kfx::VariantCatalogue::instance().resolveId(hit, id) &&
                        !id.variants.empty()) {
                        snprintf(s_fxspr_path, sizeof(s_fxspr_path), "%s",
                                 id.variants.front().file.c_str());
                        LoadFxspr();
                        s_fxspr_selected = std::atoi(hit.substr(sl + 1).c_str());
                    }
                }
            }
        }
        ImGui::EndChild();
    }

    ImGui::TextWrapped("%s", s_fxspr_status.c_str());
    if (!s_fxspr_ok)
        return;

    ImGui::SliderFloat("Thumbnail height", &s_thumb_h, 16.0f, 128.0f, "%.0f px");
    ImGui::Separator();

    // Left: selectable sprite grid. Right: comparison strip.
    const float right_w = 360.0f;
    ImGui::BeginChild("fxspr_grid", ImVec2(-right_w, 0), true,
                      ImGuiWindowFlags_HorizontalScrollbar);

    const float avail   = ImGui::GetContentRegionAvail().x;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    float       line_x  = 0.0f;
    bool        first   = true;

    const int n = s_fxspr.count();
    for (int i = 0; i < n; ++i) {
        int w = 0, h = 0;
        GLuint tex = FxsprTextureForIndex(i, &w, &h);
        if (w <= 0 || h <= 0 || tex == 0)
            continue;   // skip empty / sentinel entries

        const float  scale = s_thumb_h / (float)h;
        const ImVec2 sz((float)w * scale, (float)h * scale);

        if (!first && line_x > 0.0f && line_x + sz.x > avail)
            line_x = 0.0f;               // wrap to next row
        else if (!first)
            ImGui::SameLine();
        first = false;

        ImGui::PushID(i);
        const bool  selected = (i == s_fxspr_selected);
        const ImVec4 tint = selected ? ImVec4(1, 1, 0.5f, 1) : ImVec4(1, 1, 1, 1);
        if (ImGui::ImageButton("fx", (ImTextureID)(intptr_t)tex, sz,
                               ImVec2(0, 0), ImVec2(1, 1),
                               ImVec4(0, 0, 0, 0), tint)) {
            s_fxspr_selected = i;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::Text("Entry %d", i);
            ImGui::Text("Size %dx%d", w, h);
            const char* nm = s_fxspr.name(i);
            if (nm != nullptr && nm[0] != '\0')
                ImGui::Text("Name %s", nm);
            ImGui::EndTooltip();
        }
        ImGui::PopID();

        line_x += sz.x + spacing;
    }

    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("fxspr_compare", ImVec2(0, 0), true);
    DrawComparison();
    ImGui::EndChild();
}
#endif // RENDERER_OPENGL_ENABLED

} // namespace

/******************************************************************************/

extern "C" void ImGuiSpriteAtlasPanel_SetVisible(int visible) { s_visible = (visible != 0); }
extern "C" int  ImGuiSpriteAtlasPanel_IsVisible(void)         { return s_visible ? 1 : 0; }

extern "C" void ImGuiSpriteAtlasPanel_Draw(void)
{
    if (!s_visible)
        return;

    ImGui::SetNextWindowSize(ImVec2(760, 580), ImGuiCond_FirstUseEver);
    bool open = s_visible;
    if (!ImGui::Begin("Sprite Atlas Viewer", &open)) {
        ImGui::End();
        s_visible = open;
        return;
    }
    s_visible = open;

#ifndef RENDERER_OPENGL_ENABLED
    ImGui::TextWrapped("The sprite atlas viewer is only available with the OpenGL renderer.");
    ImGui::End();
#else
    GLSpriteAtlas* atlas = RendererGetSpriteAtlas();
    if (!atlas) {
        ImGui::TextWrapped("No GL sprite atlas is active. Switch to the OpenGL renderer to inspect sprites.");
        ImGui::End();
        return;
    }

    const char* modes[] = {
        "As drawn (indexed)",
        "Raw index (grayscale)",
        "Truecolor (stub)",
    };
    ImGui::SetNextItemWidth(220.0f);
    ImGui::Combo("Mode", &s_mode, modes, IM_ARRAYSIZE(modes));
    ImGui::SameLine();
    const bool refresh = ImGui::Button("Refresh");

    // Auto-refresh cheaply when the atlas contents change (sprite count), when
    // the mode changes, on first open, or on explicit Refresh.  A manual
    // Refresh also re-materialises against the live palette (fades, etc.).
    const size_t count = atlas->GetRegisteredCount();
    if (s_texture == 0 || s_mode != s_last_mode || count != s_last_count ||
        refresh || !s_have_snap) {
        RebuildTexture();
        s_last_count = count;
        if (s_selected >= (int)s_snap.entries.size())
            s_selected = -1;
    }

    ImGui::Text("Atlas %dx%d   |   %d sprites packed",
                s_tex_w, s_tex_h, (int)s_snap.entries.size());
    ImGui::Separator();

    if (ImGui::BeginTabBar("AtlasViewTabs")) {
        if (ImGui::BeginTabItem("Full Atlas")) {
            ImGui::SetNextItemWidth(220.0f);
            ImGui::SliderFloat("Zoom", &s_zoom, 0.25f, 8.0f, "%.2fx");
            if (s_texture) {
                ImGui::BeginChild("atlas_scroll", ImVec2(0, 0), true,
                                  ImGuiWindowFlags_HorizontalScrollbar);
                ImGui::Image((ImTextureID)(intptr_t)s_texture,
                             ImVec2((float)s_tex_w * s_zoom, (float)s_tex_h * s_zoom));
                ImGui::EndChild();
            } else {
                ImGui::TextWrapped("Atlas texture not available yet.");
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Sprites")) {
            DrawSpriteGrid();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Creatures")) {
            DrawCreaturesTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("FXSPR")) {
            DrawFxsprTab();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::End();
#endif // RENDERER_OPENGL_ENABLED
}

#endif // KEEPERFX_IMGUI_ENABLED
