
#define STANDALONE
#include <apitest.h>

extern void func_FeatureStaging(void);
extern void func_ForwardedOrdinals(void);

extern void func_RegistryHelpers(void);

extern void func_Win81ExportSurface(void);

const struct test winetest_testlist[] =
{
    { "FeatureStaging", func_FeatureStaging },
    { "ForwardedOrdinals", func_ForwardedOrdinals },
    { "RegistryHelpers", func_RegistryHelpers },
    { "Win81ExportSurface", func_Win81ExportSurface },
    { 0, 0 }
};
