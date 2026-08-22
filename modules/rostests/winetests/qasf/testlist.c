#define STANDALONE
#include <wine/test.h>

extern void func_asfreader(void);
extern void func_dmowrapper(void);

const struct test winetest_testlist[] =
{
    { "asfreader", func_asfreader },
    { "dmowrapper", func_dmowrapper },
    { 0, 0 }
};
