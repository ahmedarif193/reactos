#define STANDALONE
#include <wine/test.h>

extern void func_textinput(void);

const struct test winetest_testlist[] =
{
    { "textinput", func_textinput },
    { 0, 0 }
};
