#define STANDALONE
#include <wine/test.h>

extern void func_verifier(void);

const struct test winetest_testlist[] =
{
    { "verifier", func_verifier },
    { 0, 0 }
};
