/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file IGLShaderCompilable.h
 *     Interface for OpenGL renderer sub-systems that own GLSL programs.
 */
/******************************************************************************/
#pragma once

/**
 * Interface implemented by every OpenGL renderer component that owns one or
 * more GLSL programs.
 *
 * Life cycle:
 *   Init()           — allocate non-shader GPU resources (VAO, VBO, textures)
 *   CompileShaders() — compile and link GLSL programs, cache uniform locations
 *
 * The bootstrapper in RendererManager::RendererInit() calls CompileShaders()
 * on every registered implementation after all renderer objects have been
 * constructed, producing uniform error handling and a single place to extend
 * when a new sub-renderer is added.
 */
struct IGLShaderCompilable {
    virtual bool        CompileShaders() = 0;
    virtual const char* RendererName()   const = 0;
    virtual ~IGLShaderCompilable() = default;
};
