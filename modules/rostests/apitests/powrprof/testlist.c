
#define STANDALONE
#include <apitest.h>

extern void func_power(void);
extern void func_power_profile(void);
extern void func_SystemPowerInformation(void);

const struct test winetest_testlist[] =
{
    { "power", func_power },
    { "power_profile", func_power_profile },
    { "SystemPowerInformation", func_SystemPowerInformation },
    { 0, 0 }
};
