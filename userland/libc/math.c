#include "math.h"

static long double scale_pow2(long double value, int exp) {
    long double factor = 1.0L;
    unsigned int mag = (exp < 0) ? (unsigned int)(-exp) : (unsigned int)exp;

    while (mag != 0U) {
        if ((mag & 1U) != 0U) {
            if (exp < 0) {
                value /= factor;
            } else {
                value *= factor;
            }
        }
        factor *= 2.0L;
        mag >>= 1U;
    }

    return value;
}

double fabs(double x) {
    return (x < 0.0) ? -x : x;
}

float fabsf(float x) {
    return (x < 0.0f) ? -x : x;
}

long double fabsl(long double x) {
    return (x < 0.0L) ? -x : x;
}

double ldexp(double x, int exp) {
    return (double)scale_pow2((long double)x, exp);
}

float ldexpf(float x, int exp) {
    return (float)scale_pow2((long double)x, exp);
}

long double ldexpl(long double x, int exp) {
    return scale_pow2(x, exp);
}
