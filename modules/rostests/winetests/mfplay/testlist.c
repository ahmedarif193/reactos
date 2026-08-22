#define STANDALONE
#include <wine/test.h>

extern void func_mfplay(void);

const struct test winetest_testlist[] =
{
    { "mfplay", func_mfplay },
    { 0, 0 }
};
