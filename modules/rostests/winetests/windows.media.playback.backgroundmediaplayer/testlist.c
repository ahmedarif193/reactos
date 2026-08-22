#define STANDALONE
#include <wine/test.h>

extern void func_playback(void);

const struct test winetest_testlist[] =
{
    { "playback", func_playback },
    { 0, 0 }
};
