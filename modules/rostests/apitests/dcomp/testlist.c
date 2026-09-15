#define STANDALONE
#include <apitest.h>

extern void func_visual_reset(void);

const struct test winetest_testlist[] =
{
    { "visual_reset", func_visual_reset },
    { 0, 0 }
};
