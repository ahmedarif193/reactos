#define STANDALONE
#include <apitest.h>

extern void func_stub_surface(void);
extern void func_d3d11_render_probe(void);

const struct test winetest_testlist[] =
{
    { "stub_surface", func_stub_surface },
    { "d3d11_render_probe", func_d3d11_render_probe },
    { 0, 0 }
};
