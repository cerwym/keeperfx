/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file GLShaderLoader.cpp
 *     Provides access to embedded GLSL shader sources, with fallback
 *     to loading shader files from the shaders/ directory for development.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "renderer/opengl/GLShaderLoader.h"
#include "renderer/opengl/GLShaders.h"

#include "bflib_basics.h"  // LbErrorLog
#include "globals.h"       // ERRORLOG

#include <SDL2/SDL.h>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include "post_inc.h"

/******************************************************************************/

std::string get_embedded_shader_source(const char* shader_name)
{
    static const std::unordered_map<std::string, const char*> shader_map = {
        {"text_vert.glsl", TEXT_VERTEX_SHADER},
        {"text_frag.glsl", TEXT_FRAGMENT_SHADER},
        {"kspr_vert.glsl", KSPR_VERTEX_SHADER},
        {"kspr_frag.glsl", KSPR_FRAGMENT_SHADER},
        {"kspr_array_frag.glsl", KSPR_ARRAY_FRAGMENT_SHADER},
        {"kspr_glow_frag.glsl", KSPR_GLOW_FRAGMENT_SHADER},
        {"kspr_outline_frag.glsl",       KSPR_OUTLINE_FRAGMENT_SHADER},
        {"kspr_array_outline_frag.glsl", KSPR_ARRAY_OUTLINE_FRAGMENT_SHADER},
        {"palette_blit_vert.glsl", PALETTE_BLIT_VERTEX_SHADER},
        {"palette_blit_frag.glsl", PALETTE_BLIT_FRAGMENT_SHADER},
        {"rawimage_blit_frag.glsl",  RAWIMAGE_BLIT_FRAGMENT_SHADER},
        {"overhead_map_frag.glsl",   OVERHEAD_MAP_FRAGMENT_SHADER},
        {"zoom_tile_frag.glsl",      ZOOM_TILE_FRAGMENT_SHADER},
        {"landview_zoom_frag.glsl", LANDVIEW_ZOOM_FRAGMENT_SHADER},
        {"screen_tint_vert.glsl", SCREEN_TINT_VERTEX_SHADER},
        {"screen_tint_frag.glsl", SCREEN_TINT_FRAGMENT_SHADER},
        {"shadow_vert.glsl", SHADOW_VERTEX_SHADER},
        {"shadow_frag.glsl", SHADOW_FRAGMENT_SHADER},
        {"world_vert.glsl", WORLD_VERTEX_SHADER},
        {"world_frag.glsl", WORLD_FRAGMENT_SHADER},
        {"flatpoly_vert.glsl", FLATPOLY_VERTEX_SHADER},
        {"flatpoly_frag.glsl", FLATPOLY_FRAGMENT_SHADER},
        {"passthrough_frag.glsl", PASSTHROUGH_FRAGMENT_SHADER},
        {"lens_displacement_frag.glsl", LENS_DISPLACEMENT_FRAGMENT_SHADER},
        {"lens_mist_frag.glsl", LENS_MIST_FRAGMENT_SHADER},
        {"lens_flyeye_frag.glsl", LENS_FLYEYE_FRAGMENT_SHADER},
        {"lens_palette_frag.glsl", LENS_PALETTE_FRAGMENT_SHADER},
        {"lens_overlay_frag.glsl", LENS_OVERLAY_FRAGMENT_SHADER}
    };

    auto it = shader_map.find(shader_name);
    if (it != shader_map.end())
    {
        return it->second;
    }

    ERRORLOG("get_embedded_shader_source: shader '%s' not found", shader_name);
    return {};
}

std::string load_shader_source(const char* filename)
{
    // SDL_GetBasePath() returns the directory of the running executable
    // with a trailing path separator, e.g. "C:\game\.deploy\".
    // Shaders are expected at <exe_dir>/shaders/<filename>.
    char* base = SDL_GetBasePath();
    std::string path;
    if (base)
    {
        path = std::string(base) + "shaders/" + filename;
        SDL_free(base);
    }
    else
    {
        path = std::string("shaders/") + filename;
    }

    std::ifstream f(path);
    if (!f.is_open())
    {
        ERRORLOG("load_shader_source: cannot open '%s'", path.c_str());
        return {};
    }

    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
