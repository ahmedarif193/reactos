#define STANDALONE
#include <wine/test.h>

extern void func_data(void);

const struct test winetest_testlist[] =
{
    { "data", func_data },
    { 0, 0 }
};
