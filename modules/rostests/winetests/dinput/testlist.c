/* Automatically generated file; DO NOT EDIT!! */

#define STANDALONE
#include <wine/test.h>

extern void func_device8(void);
extern void func_dinput(void);
extern void func_force_feedback(void);
extern void func_hid(void);
extern void func_hotplug(void);
extern void func_joystick8(void);

const struct test winetest_testlist[] =
{
    { "device8", func_device8 },
    { "dinput", func_dinput },
    { "force_feedback", func_force_feedback },
    { "hid", func_hid },
    { "hotplug", func_hotplug },
    { "joystick8", func_joystick8 },
    { 0, 0 }
};
