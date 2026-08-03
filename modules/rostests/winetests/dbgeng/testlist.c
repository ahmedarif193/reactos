#define STANDALONE
#include <wine/test.h>

extern void func_dbgeng(void);

const struct test winetest_testlist[] =
{
    { "dbgeng", func_dbgeng },
    { 0, 0 }
};
