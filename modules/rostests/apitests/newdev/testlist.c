#define STANDALONE
#include <apitest.h>

extern void func_protocol(void);

const struct test winetest_testlist[] =
{
    { "protocol", func_protocol },
    { 0, 0 }
};
