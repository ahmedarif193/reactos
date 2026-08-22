#define STANDALONE
#include <wine/test.h>

extern void func_storage(void);

const struct test winetest_testlist[] =
{
    { "storage", func_storage },
    { 0, 0 }
};
