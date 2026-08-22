#define STANDALONE
#include <wine/test.h>

extern void func_usb(void);

const struct test winetest_testlist[] =
{
    { "usb", func_usb },
    { 0, 0 }
};
