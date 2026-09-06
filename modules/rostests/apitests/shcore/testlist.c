
#define STANDALONE
#include <apitest.h>

extern void func_ForwardedOrdinals(void);

const struct test winetest_testlist[] =
{
    { "ForwardedOrdinals", func_ForwardedOrdinals },
    { 0, 0 }
};
