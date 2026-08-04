#define STANDALONE
#include <wine/test.h>

extern void func_schtasks(void);

const struct test winetest_testlist[] =
{
    { "schtasks", func_schtasks },
    { 0, 0 }
};
