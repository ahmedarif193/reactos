/*
 * ARM64 Math Function Stubs for CRT
 * TODO: Implement proper math functions or link to math library
 */

/* log10 - Stub implementation */
double log10(double x)
{
    /* TODO: Implement proper log10 */
    if (x <= 0) return -1e308; /* negative infinity approximation */
    
    /* Very rough approximation for positive numbers */
    double result = 0;
    double temp = x;
    while (temp >= 10.0) {
        temp /= 10.0;
        result += 1.0;
    }
    /* Add fractional part approximation */
    if (temp > 1.0) {
        result += (temp - 1.0) / 9.0;
    }
    return result;
}

/* pow - Stub implementation */
double pow(double base, double exponent)
{
    /* TODO: Implement proper pow */
    if (exponent == 0.0) return 1.0;
    if (exponent == 1.0) return base;
    if (exponent == 2.0) return base * base;
    if (base == 0.0) return 0.0;
    
    /* Handle negative exponents */
    if (exponent < 0.0) {
        return 1.0 / pow(base, -exponent);
    }
    
    /* Very simple integer exponent handling */
    int exp_int = (int)exponent;
    if (exponent == (double)exp_int) {
        double result = 1.0;
        for (int i = 0; i < exp_int; i++) {
            result *= base;
        }
        return result;
    }
    
    /* Return approximation for other cases */
    return base;
}

/* sqrt - Stub implementation */
double sqrt(double x)
{
    /* TODO: Implement proper sqrt */
    if (x < 0) return 0.0; /* NaN approximation */
    if (x == 0) return 0.0;
    if (x == 1.0) return 1.0;
    
    /* Newton-Raphson approximation */
    double guess = x / 2.0;
    for (int i = 0; i < 10; i++) {
        guess = (guess + x / guess) / 2.0;
    }
    return guess;
}

/* log - Natural logarithm stub */
double log(double x)
{
    /* TODO: Implement proper log */
    if (x <= 0) return -1e308; /* negative infinity approximation */
    if (x == 1.0) return 0.0;
    
    /* Very rough approximation using series expansion */
    double result = 0.0;
    double y = (x - 1.0) / (x + 1.0);
    double y2 = y * y;
    double power = y;
    
    /* Use first few terms of series */
    for (int i = 0; i < 10; i++) {
        result += power / (2 * i + 1);
        power *= y2;
    }
    
    return 2.0 * result;
}