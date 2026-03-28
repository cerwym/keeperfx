#pragma once
#include <string>

/// Loads a GLSL shader source file from the <exe_dir>/shaders/ directory.
/// Returns the file content on success, or an empty string on failure (error is logged).
std::string load_shader_source(const char* filename);
