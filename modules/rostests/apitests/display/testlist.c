#define STANDALONE
#include <apitest.h>

extern void func_DisplayStackClassifier(void);

const struct test winetest_testlist[] =
{
    { "DisplayStackClassifier", func_DisplayStackClassifier },
    { 0, 0 }
};
