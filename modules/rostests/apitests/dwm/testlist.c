#define STANDALONE
#include <apitest.h>

extern void func_DwmSurface(void);
extern void func_DwmDirtyRegion(void);
extern void func_DwmBlit(void);
extern void func_DwmStructures(void);
extern void func_DwmWindowList(void);
extern void func_DwmCompositor(void);
extern void func_DwmCompositionModel(void);
extern void func_DwmExtEscape(void);
extern void func_DwmPublicApi(void);

const struct test winetest_testlist[] =
{
    { "DwmSurface", func_DwmSurface },
    { "DwmDirtyRegion", func_DwmDirtyRegion },
    { "DwmBlit", func_DwmBlit },
    { "DwmStructures", func_DwmStructures },
    { "DwmWindowList", func_DwmWindowList },
    { "DwmCompositor", func_DwmCompositor },
    { "DwmCompositionModel", func_DwmCompositionModel },
    { "DwmExtEscape", func_DwmExtEscape },
    { "DwmPublicApi", func_DwmPublicApi },
    { 0, 0 }
};
