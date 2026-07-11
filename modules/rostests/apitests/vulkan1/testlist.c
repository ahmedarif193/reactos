#define STANDALONE
#include <apitest.h>

extern void func_loader(void);
extern void func_swiftshader_icd(void);

const struct test winetest_testlist[] =
{
    { "loader", func_loader },
    { "swiftshader_icd", func_swiftshader_icd },
    { 0, 0 }
};
