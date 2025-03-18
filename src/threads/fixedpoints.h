#ifndef THREADS_FIXEDPOINTS_H
#define THREADS_FIXEDPOINTS_H

#include <stdint.h>

#define F (1 << 14)

typedef int fp;

#define int_to_fp(N) (N * F)
#define fp_to_int_zero(X) (X / F)
#define fp_to_int_round(X) (X >= 0 ? \
                            ((X) + F / 2) / F : ((X) - F / 2) / F)
#define fp_mul(X, Y) (((int64_t) (X)) * (Y) / F)
#define fp_div(X, Y) (((int64_t) (X)) * F / (Y))

#endif 