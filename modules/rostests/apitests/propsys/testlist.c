
#define STANDALONE
#include <apitest.h>

extern void func_PSPropertyBag(void);

const struct test winetest_testlist[] =
{
    { "PSPropertyBag", func_PSPropertyBag },
    { 0, 0 }
};
