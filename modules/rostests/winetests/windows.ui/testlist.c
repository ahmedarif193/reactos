#define STANDALONE
#include <wine/test.h>

extern void func_uisettings(void);
extern void func_corewindow(void);

const struct test winetest_testlist[] =
{
    { "corewindow", func_corewindow },
    { "uisettings", func_uisettings },
    { 0, 0 }
};
