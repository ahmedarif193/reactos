#define STANDALONE
#include <wine/test.h>

extern void func_perception(void);

const struct test winetest_testlist[] =
{
    { "perception", func_perception },
    { 0, 0 }
};
