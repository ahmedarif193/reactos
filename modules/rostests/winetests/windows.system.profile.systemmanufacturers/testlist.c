#define STANDALONE
#include <wine/test.h>

extern void func_smbios(void);

const struct test winetest_testlist[] =
{
    { "smbios", func_smbios },
    { 0, 0 }
};
