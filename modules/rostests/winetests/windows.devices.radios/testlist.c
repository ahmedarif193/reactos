#define STANDALONE
#include <wine/test.h>

extern void func_radios(void);

const struct test winetest_testlist[] =
{
    { "radios", func_radios },
    { 0, 0 }
};
