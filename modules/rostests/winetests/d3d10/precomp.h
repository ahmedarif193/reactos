#pragma once

#include <math.h>

static inline float reactos_exp2f(float value) { return powf(2.0f, value); }

#define exp2f reactos_exp2f
