/* Automatically generated file; DO NOT EDIT!! */

#define STANDALONE
#include <wine/test.h>

extern void func_device(void);
extern void func_gatt(void);
extern void func_radio(void);
extern void func_sdp(void);

const struct test winetest_testlist[] =
{
    { "device", func_device },
    { "gatt", func_gatt },
    { "radio", func_radio },
    { "sdp", func_sdp },
    { 0, 0 }
};
