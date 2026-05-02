#include <math.h>

double
fmod(double x, double y)
{
    return __builtin_fmod(x, y);
}
