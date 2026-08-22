#define STANDALONE
#include <wine/test.h>

extern void func_devices(void);

const struct test winetest_testlist[] =
{
    { "devices", func_devices },
    { 0, 0 }
};
