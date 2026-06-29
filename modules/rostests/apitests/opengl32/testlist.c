
#define STANDALONE
#include <wine/test.h>

extern void func_sw_extensions(void);
extern void func_sw_pixelformat(void);
extern void func_wgl_smoke(void);

const struct test winetest_testlist[] =
{
    { "sw_extensions", func_sw_extensions },
    { "sw_pixelformat", func_sw_pixelformat },
    { "wgl_smoke", func_wgl_smoke },

    { 0, 0 }
};
