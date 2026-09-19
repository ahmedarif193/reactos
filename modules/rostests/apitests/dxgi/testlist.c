#define STANDALONE
#include <apitest.h>

extern void func_stub_surface(void);
extern void func_d3d11_render_probe(void);
extern void func_dwm_render(void);
extern void func_completion_event(void);
extern void func_composition(void);
extern void func_texture_dimensions(void);

const struct test winetest_testlist[] =
{
    { "stub_surface", func_stub_surface },
    { "d3d11_render_probe", func_d3d11_render_probe },
    { "dwm_render", func_dwm_render },
    { "completion_event", func_completion_event },
    { "composition", func_composition },
    { "texture_dimensions", func_texture_dimensions },
    { 0, 0 }
};
