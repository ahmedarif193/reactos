
#define STANDALONE
#include <wine/test.h>

extern void func_sw_extensions(void);
extern void func_sw_pixelformat(void);
extern void func_wgl_swap_control(void);
extern void func_wgl_desktop(void);
extern void func_wgl_streaming(void);
extern void func_wgl_context_rebind(void);

const struct test winetest_testlist[] =
{
    { "sw_extensions", func_sw_extensions },
    { "sw_pixelformat", func_sw_pixelformat },
    { "wgl_swap_control", func_wgl_swap_control },
    { "wgl_desktop", func_wgl_desktop },
    { "wgl_streaming", func_wgl_streaming },
    { "wgl_context_rebind", func_wgl_context_rebind },

    { 0, 0 }
};
