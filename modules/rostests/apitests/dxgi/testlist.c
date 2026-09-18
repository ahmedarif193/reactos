#define STANDALONE
#include <apitest.h>

extern void func_stub_surface(void);
extern void func_d3d11_render_probe(void);
extern void func_dwm_render(void);

const struct test winetest_testlist[] =
{
    { "stub_surface", func_stub_surface },
    { "d3d11_render_probe", func_d3d11_render_probe },
    { "dwm_render", func_dwm_render },
    { 0, 0 }
};
