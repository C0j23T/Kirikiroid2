#pragma once

#include <cmath>

namespace emoteplayer::math
{
struct vec3
{
    float x, y, z;
    vec3(float x, float y, float z) : x(x), y(y), z(z) {}
};

struct vec4
{
    float x, y, z, w;
    explicit vec4(float v = 0) : x(v), y(v), z(v), w(v) {}
    vec4(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}
    float& operator[](int i) { return (&x)[i]; }
    const float& operator[](int i) const { return (&x)[i]; }
    vec4& operator+=(const vec4& v)
    {
        x += v.x; y += v.y; z += v.z; w += v.w;
        return *this;
    }
};
inline vec4 operator-(const vec4& a, const vec4& b)
{
    return {a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w};
}

struct mat4
{
    vec4 columns[4];
    explicit mat4(float diagonal = 1)
    {
        for (int i = 0; i < 4; ++i) columns[i][i] = diagonal;
    }
    mat4(float a, float b, float c, float d,
         float e, float f, float g, float h,
         float i, float j, float k, float l,
         float m, float n, float o, float p)
      : columns{{a,b,c,d}, {e,f,g,h}, {i,j,k,l}, {m,n,o,p}} {}
    vec4& operator[](int i) { return columns[i]; }
    const vec4& operator[](int i) const { return columns[i]; }
};
inline vec4 operator*(const mat4& m, const vec4& v)
{
    vec4 out;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) out[r] += m[c][r] * v[c];
    return out;
}
inline mat4 operator*(const mat4& a, const mat4& b)
{
    mat4 out(0);
    for (int c = 0; c < 4; ++c) out[c] = a * b[c];
    return out;
}
inline float radians(float degrees) { return degrees * 0.017453292519943295f; }
inline float degrees(float radians) { return radians * 57.29577951308232f; }
inline mat4 translate(const mat4& m, const vec3& v)
{
    mat4 t;
    t[3] = {v.x, v.y, v.z, 1};
    return m * t;
}
inline mat4 scale(const mat4& m, const vec3& v)
{
    mat4 s;
    s[0][0] = v.x; s[1][1] = v.y; s[2][2] = v.z;
    return m * s;
}
inline mat4 rotate(const mat4& m, float angle, const vec3& axis)
{
    const float length = std::sqrt(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z);
    if (length == 0) return m;
    const float x = axis.x / length, y = axis.y / length, z = axis.z / length;
    const float c = std::cos(angle), s = std::sin(angle), t = 1 - c;
    return m * mat4(t*x*x+c, t*x*y+s*z, t*x*z-s*y, 0,
                    t*x*y-s*z, t*y*y+c, t*y*z+s*x, 0,
                    t*x*z+s*y, t*y*z-s*x, t*z*z+c, 0,
                    0, 0, 0, 1);
}
inline mat4 ortho(float left, float right, float bottom, float top, float near, float far)
{
    mat4 out;
    out[0][0] = 2 / (right - left);
    out[1][1] = 2 / (top - bottom);
    out[2][2] = -2 / (far - near);
    out[3] = {-(right + left) / (right - left), -(top + bottom) / (top - bottom),
              -(far + near) / (far - near), 1};
    return out;
}
}
