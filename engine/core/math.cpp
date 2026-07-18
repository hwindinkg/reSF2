#include "math.hpp"
#include <cstring>

namespace resf2::core {

Mat4 Mat4::ortho(float l, float r, float b, float t, float n, float f) {
    Mat4 out{};
    out.m[0] = 2 / (r - l);
    out.m[5] = 2 / (t - b);
    out.m[10] = -2 / (f - n);
    out.m[12] = -(r + l) / (r - l);
    out.m[13] = -(t + b) / (t - b);
    out.m[14] = -(f + n) / (f - n);
    return out;
}

Mat4 Mat4::translate(float tx, float ty) {
    Mat4 out{};
    out.m[12] = tx;
    out.m[13] = ty;
    return out;
}

Mat4 Mat4::scale(float sx, float sy) {
    Mat4 out{};
    out.m[0] = sx;
    out.m[5] = sy;
    return out;
}

Mat4 Mat4::rotate(float deg) {
    float rad = deg * 3.14159265f / 180.0f;
    float c = cosf(rad), s = sinf(rad);
    Mat4 out{};
    out.m[0] = c; out.m[4] = -s;
    out.m[1] = s; out.m[5] = c;
    return out;
}

Mat4 Mat4::operator*(const Mat4& o) const {
    Mat4 r{};
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) {
            float sum = 0;
            for (int k = 0; k < 4; k++)
                sum += m[i + k * 4] * o.m[k + j * 4];
            r.m[i + j * 4] = sum;
        }
    return r;
}

} // namespace resf2::core
