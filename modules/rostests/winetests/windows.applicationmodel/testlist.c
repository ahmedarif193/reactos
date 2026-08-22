#define STANDALONE
#include <wine/test.h>

extern void func_model(void);

const struct test winetest_testlist[] =
{
    { "model", func_model },
    { 0, 0 }
};
