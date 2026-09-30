
#define STANDALONE
#include <wine/test.h>

extern void func_GetDeviceDriverFileName(void);
extern void func_GetDeviceDriverBaseName(void);
extern void func_QueryWorkingSetEx(void);

const struct test winetest_testlist[] =
{
    { "GetDeviceDriverFileName",                    func_GetDeviceDriverFileName },
    { "GetDeviceDriverBaseName",                    func_GetDeviceDriverBaseName },
    { "QueryWorkingSetEx",                          func_QueryWorkingSetEx },

    { 0, 0 }
};

