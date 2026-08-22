#define STANDALONE
#include <wine/test.h>

extern void func_mediacontrol(void);

const struct test winetest_testlist[] =
{
    { "mediacontrol", func_mediacontrol },
    { 0, 0 }
};
