/*
 * ARM64 Math Function Stubs
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
    return result;
}

/* pow - Stub implementation */
double pow(double base, double exponent)
{
    /* TODO: Implement proper pow */
    if (exponent == 0.0) return 1.0;
    if (exponent == 1.0) return base;
    if (exponent == 2.0) return base * base;
    
    /* Very simple integer exponent handling */
    int exp_int = (int)exponent;
    if (exponent == (double)exp_int && exp_int > 0) {
        double result = 1.0;
        for (int i = 0; i < exp_int; i++) {
            result *= base;
        }
        return result;
    }
    
    /* Return approximation for other cases */
    return base;
}

/* ceil - Round up to nearest integer */
double ceil(double x)
{
    long long int_part = (long long)x;
    if (x == (double)int_part) {
        return x;
    }
    if (x > 0) {
        return (double)(int_part + 1);
    }
    return (double)int_part;
}

/* floor - Round down to nearest integer */
double floor(double x)
{
    long long int_part = (long long)x;
    if (x == (double)int_part) {
        return x;
    }
    if (x > 0) {
        return (double)int_part;
    }
    return (double)(int_part - 1);
}