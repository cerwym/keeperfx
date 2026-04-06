#pragma once
#include <string>

/// Gets a GLSL shader source by name from embedded shaders.
/// Returns the shader source on success, or an empty string if not found.
std::string get_embedded_shader_source(const char* shader_name);

/// Loads a GLSL shader source file from the <exe_dir>/shaders/ directory.
/// Returns the file content on success, or an empty string on failure (error is logged).
/// This function is kept for backward compatibility but embedded shaders should be preferred.
std::string load_shader_source(const char* filename);
