/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file VecMath.h
 *     Lightweight 2D/3D float vector types for the GPU renderer.
 */
/******************************************************************************/
#ifndef VEC_MATH_H
#define VEC_MATH_H

#include <cmath>

struct Vec2f {
    float x, y;

    Vec2f() : x(0), y(0) {}
    Vec2f(float x, float y) : x(x), y(y) {}

    Vec2f operator+(Vec2f v)  const { return {x + v.x, y + v.y}; }
    Vec2f operator-(Vec2f v)  const { return {x - v.x, y - v.y}; }
    Vec2f operator*(float s)  const { return {x * s, y * s}; }
    Vec2f operator/(float s)  const { return {x / s, y / s}; }
    Vec2f operator-()         const { return {-x, -y}; }

    float dot(Vec2f v)   const { return x * v.x + y * v.y; }
    float length()       const { return std::sqrt(x * x + y * y); }
    float length_sq()    const { return x * x + y * y; }
    Vec2f normalized()   const { float l = length(); return l > 0.0f ? *this / l : Vec2f{}; }
    Vec2f perp()         const { return {-y, x}; }
};

inline Vec2f operator*(float s, Vec2f v) { return v * s; }

struct Vec3f {
    float x, y, z;

    Vec3f() : x(0), y(0), z(0) {}
    Vec3f(float x, float y, float z) : x(x), y(y), z(z) {}

    Vec3f operator+(Vec3f v)  const { return {x + v.x, y + v.y, z + v.z}; }
    Vec3f operator-(Vec3f v)  const { return {x - v.x, y - v.y, z - v.z}; }
    Vec3f operator*(float s)  const { return {x * s, y * s, z * s}; }
    Vec3f operator/(float s)  const { return {x / s, y / s, z / s}; }
    Vec3f operator-()         const { return {-x, -y, -z}; }

    float  dot(Vec3f v)   const { return x * v.x + y * v.y + z * v.z; }
    Vec3f  cross(Vec3f v) const { return {y*v.z - z*v.y, z*v.x - x*v.z, x*v.y - y*v.x}; }
    float  length()       const { return std::sqrt(x * x + y * y + z * z); }
    float  length_sq()    const { return x * x + y * y + z * z; }
    Vec3f  normalized()   const { float l = length(); return l > 0.0f ? *this / l : Vec3f{}; }
};

inline Vec3f operator*(float s, Vec3f v) { return v * s; }

/** Convert screen pixel coordinates to NDC (normalised device coordinates).
 *  Screen Y increases downward; NDC Y increases upward. */
inline Vec2f ScreenToNDC(float px, float py, float screen_w, float screen_h)
{
    return Vec2f(px / screen_w * 2.0f - 1.0f,
                 1.0f - py / screen_h * 2.0f);
}

#endif // VEC_MATH_H
