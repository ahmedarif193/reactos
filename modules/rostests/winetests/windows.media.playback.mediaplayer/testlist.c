#define STANDALONE
#include <wine/test.h>

extern void func_mediaplayer(void);

const struct test winetest_testlist[] =
{
    { "mediaplayer", func_mediaplayer },
    { 0, 0 }
};
