// Minimal vector/matrix/quaternion math. Column-major matrices (OpenGL style),
// right-handed, no dependencies.
#pragma once
#include <cmath>

namespace hx {

const float kPi = 3.14159265358979f;

struct Vec2 { float x = 0, y = 0; };
inline Vec2 v2(float x, float y) { Vec2 v; v.x = x; v.y = y; return v; }

struct Vec3 {
    float x = 0, y = 0, z = 0;
    Vec3() {}
    Vec3(float a, float b, float c) : x(a), y(b), z(c) {}
    Vec3 operator+(const Vec3& o) const { return Vec3(x + o.x, y + o.y, z + o.z); }
    Vec3 operator-(const Vec3& o) const { return Vec3(x - o.x, y - o.y, z - o.z); }
    Vec3 operator*(float s) const { return Vec3(x * s, y * s, z * s); }
    Vec3 operator*(const Vec3& o) const { return Vec3(x * o.x, y * o.y, z * o.z); }
    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
};
inline float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return Vec3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
inline float length(const Vec3& a) { return std::sqrt(dot(a, a)); }
inline Vec3 normalize(const Vec3& a) { float l = length(a); return l > 0 ? a * (1.0f / l) : a; }
inline Vec3 lerp(const Vec3& a, const Vec3& b, float t) { return a + (b - a) * t; }

struct Vec4 { float x = 0, y = 0, z = 0, w = 0; };
inline Vec4 v4(float x, float y, float z, float w) { Vec4 v; v.x = x; v.y = y; v.z = z; v.w = w; return v; }

inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float smooth(float t) { t = clampf(t, 0, 1); return t * t * (3 - 2 * t); }
inline float easeOutBack(float t) { float c1 = 1.70158f, c3 = c1 + 1; float u = t - 1; return 1 + c3 * u * u * u + c1 * u * u; }
// Frame-rate independent exponential approach.
inline float approach(float cur, float target, float rate, float dt) { return target + (cur - target) * std::exp(-rate * dt); }

struct Mat4 {
    float m[16]; // column-major: m[col*4 + row]
    static Mat4 identity() { Mat4 r{}; r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1; return r; }
    Mat4 operator*(const Mat4& b) const {
        Mat4 r{};
        for (int c = 0; c < 4; ++c)
            for (int rr = 0; rr < 4; ++rr) {
                float s = 0;
                for (int k = 0; k < 4; ++k) s += m[k * 4 + rr] * b.m[c * 4 + k];
                r.m[c * 4 + rr] = s;
            }
        return r;
    }
    Vec4 operator*(const Vec4& v) const {
        return v4(m[0] * v.x + m[4] * v.y + m[8] * v.z + m[12] * v.w,
                  m[1] * v.x + m[5] * v.y + m[9] * v.z + m[13] * v.w,
                  m[2] * v.x + m[6] * v.y + m[10] * v.z + m[14] * v.w,
                  m[3] * v.x + m[7] * v.y + m[11] * v.z + m[15] * v.w);
    }
    static Mat4 translate(const Vec3& t) { Mat4 r = identity(); r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z; return r; }
    static Mat4 scale(const Vec3& s) { Mat4 r = identity(); r.m[0] = s.x; r.m[5] = s.y; r.m[10] = s.z; return r; }
    static Mat4 rotateZ(float a) {
        Mat4 r = identity(); float c = std::cos(a), s = std::sin(a);
        r.m[0] = c; r.m[1] = s; r.m[4] = -s; r.m[5] = c; return r;
    }
    static Mat4 perspective(float fovy, float aspect, float zn, float zf) {
        Mat4 r{}; float f = 1.0f / std::tan(fovy * 0.5f);
        r.m[0] = f / aspect; r.m[5] = f; r.m[10] = (zf + zn) / (zn - zf); r.m[11] = -1;
        r.m[14] = 2 * zf * zn / (zn - zf); return r;
    }
    static Mat4 ortho(float l, float rgt, float b, float t, float zn, float zf) {
        Mat4 r = identity();
        r.m[0] = 2 / (rgt - l); r.m[5] = 2 / (t - b); r.m[10] = -2 / (zf - zn);
        r.m[12] = -(rgt + l) / (rgt - l); r.m[13] = -(t + b) / (t - b); r.m[14] = -(zf + zn) / (zf - zn);
        return r;
    }
    static Mat4 lookAt(const Vec3& eye, const Vec3& at, const Vec3& up) {
        Vec3 f = normalize(at - eye), s = normalize(cross(f, up)), u = cross(s, f);
        Mat4 r = identity();
        r.m[0] = s.x; r.m[4] = s.y; r.m[8] = s.z;
        r.m[1] = u.x; r.m[5] = u.y; r.m[9] = u.z;
        r.m[2] = -f.x; r.m[6] = -f.y; r.m[10] = -f.z;
        r.m[12] = -dot(s, eye); r.m[13] = -dot(u, eye); r.m[14] = dot(f, eye);
        return r;
    }
};

struct Quat {
    float x = 0, y = 0, z = 0, w = 1;
    static Quat axisAngle(const Vec3& axis, float a) {
        Vec3 n = normalize(axis); float s = std::sin(a * 0.5f);
        Quat q; q.x = n.x * s; q.y = n.y * s; q.z = n.z * s; q.w = std::cos(a * 0.5f); return q;
    }
    Quat operator*(const Quat& b) const {
        Quat r;
        r.w = w * b.w - x * b.x - y * b.y - z * b.z;
        r.x = w * b.x + x * b.w + y * b.z - z * b.y;
        r.y = w * b.y - x * b.z + y * b.w + z * b.x;
        r.z = w * b.z + x * b.y - y * b.x + z * b.w;
        return r;
    }
    // From a rotation matrix given as rows r[i][j].
    static Quat fromRows(const float r[3][3]) {
        Quat q; float tr = r[0][0] + r[1][1] + r[2][2];
        if (tr > 0) {
            float s = std::sqrt(tr + 1.0f) * 2;
            q.w = 0.25f * s; q.x = (r[2][1] - r[1][2]) / s; q.y = (r[0][2] - r[2][0]) / s; q.z = (r[1][0] - r[0][1]) / s;
        } else if (r[0][0] > r[1][1] && r[0][0] > r[2][2]) {
            float s = std::sqrt(1.0f + r[0][0] - r[1][1] - r[2][2]) * 2;
            q.w = (r[2][1] - r[1][2]) / s; q.x = 0.25f * s; q.y = (r[0][1] + r[1][0]) / s; q.z = (r[0][2] + r[2][0]) / s;
        } else if (r[1][1] > r[2][2]) {
            float s = std::sqrt(1.0f + r[1][1] - r[0][0] - r[2][2]) * 2;
            q.w = (r[0][2] - r[2][0]) / s; q.x = (r[0][1] + r[1][0]) / s; q.y = 0.25f * s; q.z = (r[1][2] + r[2][1]) / s;
        } else {
            float s = std::sqrt(1.0f + r[2][2] - r[0][0] - r[1][1]) * 2;
            q.w = (r[1][0] - r[0][1]) / s; q.x = (r[0][2] + r[2][0]) / s; q.y = (r[1][2] + r[2][1]) / s; q.z = 0.25f * s;
        }
        return q;
    }
    Mat4 toMat4() const {
        Mat4 r = Mat4::identity();
        float xx = x * x, yy = y * y, zz = z * z, xy = x * y, xz = x * z, yz = y * z, wx = w * x, wy = w * y, wz = w * z;
        r.m[0] = 1 - 2 * (yy + zz); r.m[1] = 2 * (xy + wz); r.m[2] = 2 * (xz - wy);
        r.m[4] = 2 * (xy - wz); r.m[5] = 1 - 2 * (xx + zz); r.m[6] = 2 * (yz + wx);
        r.m[8] = 2 * (xz + wy); r.m[9] = 2 * (yz - wx); r.m[10] = 1 - 2 * (xx + yy);
        return r;
    }
};
inline Quat slerp(Quat a, Quat b, float t) {
    float d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (d < 0) { b.x = -b.x; b.y = -b.y; b.z = -b.z; b.w = -b.w; d = -d; }
    Quat r;
    if (d > 0.9995f) {
        r.x = a.x + (b.x - a.x) * t; r.y = a.y + (b.y - a.y) * t; r.z = a.z + (b.z - a.z) * t; r.w = a.w + (b.w - a.w) * t;
    } else {
        float th = std::acos(d), s = std::sin(th), wa = std::sin((1 - t) * th) / s, wb = std::sin(t * th) / s;
        r.x = a.x * wa + b.x * wb; r.y = a.y * wa + b.y * wb; r.z = a.z * wa + b.z * wb; r.w = a.w * wa + b.w * wb;
    }
    float l = std::sqrt(r.x * r.x + r.y * r.y + r.z * r.z + r.w * r.w);
    r.x /= l; r.y /= l; r.z /= l; r.w /= l;
    return r;
}

} // namespace hx
