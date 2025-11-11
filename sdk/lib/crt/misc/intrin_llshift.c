unsigned long long __ll_lshift(unsigned long long Mask, int Bit)
{
    unsigned char shift = Bit & 0x3F;
    return Mask << shift;
}

long long __ll_rshift(long long Mask, int Bit)
{
    unsigned char shift = Bit & 0x3F;
    return Mask >> shift;
}

unsigned long long __ull_rshift(unsigned long long Mask, int Bit)
{
    unsigned char shift = Bit & 0x3F;
    return Mask >> shift;
}
