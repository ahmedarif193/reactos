#include <math.h>

double
_logb(double x)
{
    return __builtin_logb(x);
}

float
_logbf(float x)
{
    return __builtin_logbf(x);
}

double
logb(double x)
{
    return _logb(x);
}

float
logbf(float x)
{
    return _logbf(x);
}
