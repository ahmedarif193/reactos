#define STANDALONE
#include <apitest.h>

extern void func_D3dkmtAdapter(void);
extern void func_D3dkmtDevice(void);
extern void func_D3dkmtAlloc(void);
extern void func_D3dkmtSync(void);
extern void func_D3dkmtDisplay(void);
extern void func_D3dkmtPresent(void);
extern void func_vidmm(void);
extern void func_present(void);
extern void func_dwm(void);
extern void func_display(void);

const struct test winetest_testlist[] =
{
    { "D3dkmtAdapter", func_D3dkmtAdapter },
    { "D3dkmtDevice",  func_D3dkmtDevice },
    { "D3dkmtAlloc",   func_D3dkmtAlloc },
    { "D3dkmtSync",    func_D3dkmtSync },
    { "D3dkmtDisplay", func_D3dkmtDisplay },
    { "D3dkmtPresent", func_D3dkmtPresent },
    { "vidmm",         func_vidmm },
    { "present",       func_present },
    { "dwm",           func_dwm },
    { "display",       func_display },
    { 0, 0 }
};
