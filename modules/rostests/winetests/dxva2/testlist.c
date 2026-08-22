#define STANDALONE
#include <wine/test.h>

extern void func_dxva2(void);

const struct test winetest_testlist[] =
{
    { "dxva2", func_dxva2 },
    { 0, 0 }
};
