#ifndef _RODNIX_USERLAND_MATH_H
#define _RODNIX_USERLAND_MATH_H

#define HUGE_VAL  (__builtin_huge_val())
#define HUGE_VALF (__builtin_huge_valf())
#define HUGE_VALL (__builtin_huge_vall())

double fabs(double x);
float fabsf(float x);
long double fabsl(long double x);

double ldexp(double x, int exp);
float ldexpf(float x, int exp);
long double ldexpl(long double x, int exp);

#define isnan(x) __builtin_isnan(x)
#define isinf(x) __builtin_isinf_sign(x)

#endif /* _RODNIX_USERLAND_MATH_H */
