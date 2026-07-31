#define STANDALONE
#include <apitest.h>

extern void func_D3dkmtAdapter(void);
extern void func_D3dkmtDevice(void);
extern void func_D3dkmtAlloc(void);
extern void func_D3dkmtSync(void);
extern void func_D3dkmtDisplay(void);
extern void func_D3dkmtPresent(void);
extern void func_pagingqueue(void);
extern void func_residency(void);
extern void func_gpuva(void);
extern void func_hwqueue(void);
extern void func_sync2(void);
extern void func_videomem(void);
extern void func_enum2(void);
extern void func_context2(void);
extern void func_alloc2(void);
extern void func_keyedmutex(void);
extern void func_overlay(void);
extern void func_power(void);
extern void func_D3dkmtAdapterInfo(void);
extern void func_adapterclass(void);
extern void func_wddmcaps(void);
extern void func_sharing(void);
extern void func_syncext(void);
extern void func_keyedmutexlife(void);
extern void func_allocstandard(void);
extern void func_allocflags(void);
extern void func_allocpriority(void);
extern void func_allocresidency(void);
extern void func_allocreserve(void);
extern void func_allocdestroy2(void);
extern void func_displayext(void);
extern void func_presentext(void);
extern void func_misc(void);
extern void func_teardown(void);
extern void func_renderpath(void);
extern void func_capsaudit(void);
extern void func_umdabi(void);
extern void func_adapterid(void);
extern void func_gpusync(void);
extern void func_umdcallbacks(void);
extern void func_abifreeze(void);
extern void func_procstress(void);
extern void func_umdload(void);
extern void func_umd2d(void);
extern void func_renderadapter(void);
extern void func_luidident(void);
extern void func_handletype(void);
extern void func_stalehandle(void);
extern void func_modelistsize(void);
extern void func_vidmm(void);
extern void func_present(void);
extern void func_dwm(void);
extern void func_cursor(void);
extern void func_display(void);
extern void func_pipeline(void);
extern void func_capture(void);
extern void func_privateioctl(void);
extern void func_cle(void);
extern void func_v3dsmoke(void);
#ifdef REACTOS_D3DKMT_VC4KMT_TEST
extern void func_vc4kmt_smoke(void);
#endif

const struct test winetest_testlist[] =
{
    { "D3dkmtAdapter", func_D3dkmtAdapter },
    { "D3dkmtDevice",  func_D3dkmtDevice },
    { "D3dkmtAlloc",   func_D3dkmtAlloc },
    { "D3dkmtSync",    func_D3dkmtSync },
    { "D3dkmtDisplay", func_D3dkmtDisplay },
    { "D3dkmtPresent", func_D3dkmtPresent },
    { "pagingqueue",   func_pagingqueue },
    { "residency",     func_residency },
    { "gpuva",         func_gpuva },
    { "hwqueue",       func_hwqueue },
    { "sync2",         func_sync2 },
    { "videomem",      func_videomem },
    { "enum2",         func_enum2 },
    { "context2",      func_context2 },
    { "alloc2",        func_alloc2 },
    { "keyedmutex",    func_keyedmutex },
    { "overlay",       func_overlay },
    { "power",         func_power },
    { "D3dkmtAdapterInfo", func_D3dkmtAdapterInfo },
    { "adapterclass",  func_adapterclass },
    { "wddmcaps",      func_wddmcaps },
    { "sharing",       func_sharing },
    { "syncext",       func_syncext },
    { "keyedmutexlife", func_keyedmutexlife },
    { "allocstandard", func_allocstandard },
    { "allocflags",    func_allocflags },
    { "allocpriority", func_allocpriority },
    { "allocresidency", func_allocresidency },
    { "allocreserve",  func_allocreserve },
    { "allocdestroy2", func_allocdestroy2 },
    { "displayext",    func_displayext },
    { "presentext",    func_presentext },
    { "misc",          func_misc },
    { "teardown",      func_teardown },
    { "renderpath",    func_renderpath },
    { "capsaudit",     func_capsaudit },
    { "umdabi",        func_umdabi },
    { "adapterid",     func_adapterid },
    { "gpusync",       func_gpusync },
    { "umdcallbacks",  func_umdcallbacks },
    { "abifreeze",     func_abifreeze },
    { "procstress",    func_procstress },
    { "umdload",       func_umdload },
    { "umd2d",         func_umd2d },
    { "renderadapter", func_renderadapter },
    { "luidident",     func_luidident },
    { "handletype",    func_handletype },
    { "stalehandle",   func_stalehandle },
    { "modelistsize",  func_modelistsize },
    { "vidmm",         func_vidmm },
    { "present",       func_present },
    { "dwm",           func_dwm },
    { "cursor",        func_cursor },
    { "display",       func_display },
    { "pipeline",      func_pipeline },
    { "capture",       func_capture },
    { "privateioctl",  func_privateioctl },
    { "cle",           func_cle },
    { "v3dsmoke",      func_v3dsmoke },
#ifdef REACTOS_D3DKMT_VC4KMT_TEST
    { "vc4kmt_smoke",  func_vc4kmt_smoke },
#endif
    { 0, 0 }
};
