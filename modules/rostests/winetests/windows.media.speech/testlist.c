#define STANDALONE
#include <wine/test.h>

extern void func_speech(void);

const struct test winetest_testlist[] =
{
    { "speech", func_speech },
    { 0, 0 }
};
