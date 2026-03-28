/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file GLShaderLoader.cpp
 *     Loads GLSL shader source files from the shaders/ directory adjacent
 *     to the running executable, enabling runtime shader modification.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "renderer/opengl/GLShaderLoader.h"

#include "bflib_basics.h"  // LbErrorLog
#include "globals.h"       // ERRORLOG

#include <SDL2/SDL.h>
#include <fstream>
#include <sstream>
#include "post_inc.h"

/******************************************************************************/

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
