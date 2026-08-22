#define STANDALONE
#include <wine/test.h>

extern void func_input(void);

const struct test winetest_testlist[] =
{
    { "input", func_input },
    { 0, 0 }
};
