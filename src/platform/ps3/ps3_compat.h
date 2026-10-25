// Force-included on PS3 only (-include): newlib + libstdc++ there do not export a few
// C99 math functions into std::, which the shared code uses.
#pragma once
#ifdef __cplusplus
#include <cmath>
namespace std {
inline float fmin(float a, float b) { return ::fminf(a, b); }
inline double fmin(double a, double b) { return ::fmin(a, b); }
inline float fmax(float a, float b) { return ::fmaxf(a, b); }
inline double fmax(double a, double b) { return ::fmax(a, b); }
} // namespace std
#endif
