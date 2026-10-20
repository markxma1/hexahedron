// Die orientation as an exact cube rotation (one of 24).
// Board space: +X = right, +Y = up, +Z = toward the viewer (the "front" face).
// Pure, dependency-free C++ (portable to PS3/Android/Godot).
#pragma once
#include <cstdint>

namespace hx {

enum Dir : uint8_t { DirPX = 0, DirNX, DirPY, DirNY, DirPZ, DirNZ };

enum class Roll : uint8_t { Down, Left, Right };

struct Orient {
    // m[i][j] = world component i of the die's local axis j. Entries are -1/0/1.
    int8_t m[3][3];

    static Orient identity() {
        Orient o{};
        o.m[0][0] = o.m[1][1] = o.m[2][2] = 1;
        return o;
    }

    // Local faces: +Z=1, -Z=6, +Y=2, -Y=5, +X=3, -X=4 (opposites sum to 7).
    static int localValue(int axis, int sign) {
        static const int pos[3] = {3, 2, 1};
        return sign > 0 ? pos[axis] : 7 - pos[axis];
    }

    // Value of the face pointing in world direction d.
    int faceValue(Dir d) const {
        int axis = d / 2, sign = (d % 2 == 0) ? 1 : -1;
        // local vector = R^T * world unit vector
        for (int j = 0; j < 3; ++j) {
            int c = m[axis][j] * sign;
            if (c != 0) return localValue(j, c);
        }
        return 0;
    }
    int front() const { return faceValue(DirPZ); }

    Orient mul(const Orient& r) const { // this * r
        Orient o{};
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) {
                int s = 0;
                for (int k = 0; k < 3; ++k) s += m[i][k] * r.m[k][j];
                o.m[i][j] = (int8_t)s;
            }
        return o;
    }

    // Rolling one cell in a direction. Down: top face comes to the front.
    // Right: left face comes to the front. Left: right face comes to the front.
    Orient rolled(Roll r) const {
        Orient rot{};
        switch (r) {
        case Roll::Down: // Rx(+90): y->z, z->-y
            rot.m[0][0] = 1; rot.m[2][1] = 1; rot.m[1][2] = -1; break;
        case Roll::Right: // Ry(+90): z->x, x->-z
            rot.m[1][1] = 1; rot.m[0][2] = 1; rot.m[2][0] = -1; break;
        case Roll::Left: // Ry(-90)
            rot.m[1][1] = 1; rot.m[0][2] = -1; rot.m[2][0] = 1; break;
        }
        return rot.mul(*this);
    }

    bool operator==(const Orient& o) const {
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                if (m[i][j] != o.m[i][j]) return false;
        return true;
    }
};

} // namespace hx
