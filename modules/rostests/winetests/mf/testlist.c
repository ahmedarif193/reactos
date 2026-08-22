#define STANDALONE
#include <wine/test.h>

extern void func_mf(void);
extern void func_topology(void);
extern void func_transform(void);

const struct test winetest_testlist[] =
{
    { "mf", func_mf },
    { "topology", func_topology },
    { "transform", func_transform },
    { 0, 0 }
};
