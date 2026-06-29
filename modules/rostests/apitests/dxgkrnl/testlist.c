#define STANDALONE
#include <apitest.h>

extern void func_D3dkmtIoctl(void);
extern void func_D3dkmtStructures(void);
extern void func_D3dkmtAdapter(void);
extern void func_D3dkmtDevice(void);
extern void func_D3dkmtSync(void);
extern void func_D3dkmtDisplay(void);
extern void func_D3dkmtPresent(void);
extern void func_HandleEncoding(void);
extern void func_VidPnConstants(void);
extern void func_VidPnTopology(void);
extern void func_VidPnModeSet(void);
extern void func_WddmConstants(void);
extern void func_Scheduler(void);
extern void func_VidMm(void);
extern void func_PresentQueue(void);
extern void func_TdrPreemption(void);
extern void func_CddDisplay(void);
extern void func_WddmPipeline(void);
extern void func_D3dkmtCallbackExchange(void);
extern void func_GpuVirtualAddress(void);
extern void func_ProcessLifecycle(void);
extern void func_VidSch(void);
extern void func_D3dkmtPresentPath(void);
extern void func_D3dkmtDisplayAdv(void);
extern void func_D3dkmtTdr(void);
extern void func_D3dkmtVidMm(void);
extern void func_D3dkmtChain(void);

const struct test winetest_testlist[] =
{
    { "D3dkmtCallbackExchange", func_D3dkmtCallbackExchange },
    { "D3dkmtIoctl", func_D3dkmtIoctl },
    { "D3dkmtStructures", func_D3dkmtStructures },
    { "D3dkmtAdapter", func_D3dkmtAdapter },
    { "D3dkmtDevice", func_D3dkmtDevice },
    { "D3dkmtSync", func_D3dkmtSync },
    { "D3dkmtDisplay", func_D3dkmtDisplay },
    { "D3dkmtPresent", func_D3dkmtPresent },
    { "HandleEncoding", func_HandleEncoding },
    { "VidPnConstants", func_VidPnConstants },
    { "VidPnTopology", func_VidPnTopology },
    { "VidPnModeSet", func_VidPnModeSet },
    { "WddmConstants", func_WddmConstants },
    { "Scheduler", func_Scheduler },
    { "VidMm", func_VidMm },
    { "PresentQueue", func_PresentQueue },
    { "TdrPreemption", func_TdrPreemption },
    { "CddDisplay", func_CddDisplay },
    { "WddmPipeline", func_WddmPipeline },
    { "GpuVirtualAddress", func_GpuVirtualAddress },
    { "ProcessLifecycle", func_ProcessLifecycle },
    { "VidSch", func_VidSch },
    { "D3dkmtPresentPath", func_D3dkmtPresentPath },
    { "D3dkmtDisplayAdv", func_D3dkmtDisplayAdv },
    { "D3dkmtTdr", func_D3dkmtTdr },
    { "D3dkmtVidMm", func_D3dkmtVidMm },
    { "D3dkmtChain", func_D3dkmtChain },
    { 0, 0 }
};
