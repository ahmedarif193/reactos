#define STANDALONE
#include <wine/test.h>

extern void func_mfmediaengine(void);

const struct test winetest_testlist[] =
{
    { "mfmediaengine", func_mfmediaengine },
    { 0, 0 }
};
