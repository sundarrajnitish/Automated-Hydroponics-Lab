// hmath.h - tiny deterministic math for the simulator.
//
// The same C++ is compiled natively (Python analysis) and to freestanding
// WebAssembly (website). WebAssembly has no libm, and we want both builds to
// produce bit-identical traces, so the few transcendental functions the
// reservoir chemistry needs are implemented here with plain IEEE-754
// arithmetic (build with -ffp-contract=off so no FMA sneaks in).
#pragma once
#include <stdint.h>

namespace hm {

static const double LN2 = 0.69314718055994530942;
static const double LN10 = 2.30258509299404568402;

inline double fabs_(double x) { return x < 0 ? -x : x; }
inline double sqrt_(double x) { return __builtin_sqrt(x); }  // one IEEE instruction
inline double min_(double a, double b) { return a < b ? a : b; }
inline double max_(double a, double b) { return a > b ? a : b; }
inline double clamp_(double x, double lo, double hi) { return x < lo ? lo : (x > hi ? hi : x); }

inline double bits_to_double(uint64_t b) { double d; __builtin_memcpy(&d, &b, 8); return d; }
inline uint64_t double_to_bits(double d) { uint64_t b; __builtin_memcpy(&b, &d, 8); return b; }

// e^x, relative error < 1e-15 over the range used here.
inline double exp_(double x) {
  if (x > 709.0) return 8.2e307;
  if (x < -708.0) return 0.0;
  double kd = x / LN2;
  int k = (int)(kd >= 0 ? kd + 0.5 : kd - 0.5);
  double r = x - (double)k * LN2;               // |r| <= ~0.35
  double term = 1.0, sum = 1.0;
  for (int i = 1; i <= 18; ++i) { term *= r / (double)i; sum += term; }
  return sum * bits_to_double((uint64_t)(k + 1023) << 52);
}

// natural log for x > 0
inline double log_(double x) {
  if (!(x > 0)) return -1e308;
  uint64_t b = double_to_bits(x);
  int e = (int)((b >> 52) & 0x7ff) - 1023;
  double m = bits_to_double((b & 0x000fffffffffffffULL) | 0x3ff0000000000000ULL);  // [1,2)
  if (m > 1.41421356237309504880) { m *= 0.5; e += 1; }
  double s = (m - 1.0) / (m + 1.0), s2 = s * s, term = s, sum = 0.0;
  for (int i = 1; i <= 41; i += 2) { sum += term / (double)i; term *= s2; }
  return 2.0 * sum + (double)e * LN2;
}

inline double log10_(double x) { return log_(x) / LN10; }
inline double pow10_(double y) { return exp_(y * LN10); }

inline double sin_(double x) {  // range-reduced Taylor, fine for |x| < 1e6
  const double TWO_PI = 6.28318530717958647692, PI = 3.14159265358979323846;
  double k = (double)(int64_t)(x / TWO_PI);
  x -= k * TWO_PI;
  if (x > PI) x -= TWO_PI;
  if (x < -PI) x += TWO_PI;
  double term = x, sum = x, x2 = x * x;
  for (int i = 1; i <= 12; ++i) { term *= -x2 / (double)((2 * i) * (2 * i + 1)); sum += term; }
  return sum;
}

// splitmix64: small, fast, identical on every platform
struct Rng {
  uint64_t s;
  explicit Rng(uint64_t seed = 1) : s(seed) {}
  uint64_t next() {
    uint64_t z = (s += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
  }
  double uniform() { return (double)(next() >> 11) * (1.0 / 9007199254740992.0); }
  // Irwin-Hall approximation of N(0,1): no transcendental calls needed
  double gauss() {
    double s12 = 0;
    for (int i = 0; i < 12; ++i) s12 += uniform();
    return s12 - 6.0;
  }
};

}  // namespace hm
