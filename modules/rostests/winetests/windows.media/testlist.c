#define STANDALONE
#include <wine/test.h>

extern void func_media(void);

const struct test winetest_testlist[] =
{
    { "media", func_media },
    { 0, 0 }
};
