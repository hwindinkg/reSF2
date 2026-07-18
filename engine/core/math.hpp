#pragma once

#include <cmath>
#include <cstdint>

namespace resf2::core {

struct Vec2 {
    float x = 0, y = 0;
    Vec2() = default;
    Vec2(float x, float y) : x(x), y(y) {}

    Vec2 operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
    Vec2 operator*(float s) const { return {x * s, y * s}; }
    Vec2 operator/(float s) const { return {x / s, y / s}; }
    Vec2& operator+=(Vec2 o) { x += o.x; y += o.y; return *this; }
    Vec2& operator-=(Vec2 o) { x -= o.x; y -= o.y; return *this; }
    Vec2& operator*=(float s) { x *= s; y *= s; return *this; }

    float len() const { return std::sqrt(x * x + y * y); }
    float len_sq() const { return x * x + y * y; }
    Vec2 norm() const { float l = len(); return l > 0 ? *this / l : Vec2{}; }
    float dot(Vec2 o) const { return x * o.x + y * o.y; }
    float cross(Vec2 o) const { return x * o.y - y * o.x; }
};

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
    bool contains(float px, float py) const {
        return px >= x && px <= x + w && py >= y && py <= y + h;
    }
};

struct Color {
    float r = 1, g = 1, b = 1, a = 1;
    Color() = default;
    Color(float r, float g, float b, float a = 1) : r(r), g(g), b(b), a(a) {}

    static Color white() { return {1,1,1,1}; }
    static Color black() { return {0,0,0,1}; }
    static Color red()   { return {1,0,0,1}; }
    static Color green() { return {0,1,0,1}; }
    static Color blue()  { return {0,0,1,1}; }
    static Color clear() { return {0,0,0,0}; }

    uint32_t to_rgba() const {
        return ((uint32_t)(r * 255) << 24) |
               ((uint32_t)(g * 255) << 16) |
               ((uint32_t)(b * 255) << 8) |
               ((uint32_t)(a * 255));
    }
};

struct Mat4 {
    float m[16] = {
        1,0,0,0,
        0,1,0,0,
        0,0,1,0,
        0,0,0,1
    };

    static Mat4 ortho(float l, float r, float b, float t, float n = -1, float f = 1);
    static Mat4 translate(float tx, float ty);
    static Mat4 scale(float sx, float sy);
    static Mat4 rotate(float deg);

    Mat4 operator*(const Mat4& o) const;
};

} // namespace resf2::core
