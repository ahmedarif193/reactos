#define STANDALONE
#include <apitest.h>

extern void func_AppContainer(void);
extern void func_Broker(void);
extern void func_Desktop(void);
extern void func_Integrity(void);
extern void func_JobLifecycle(void);
extern void func_JobMemory(void);
extern void func_JobUi(void);
extern void func_KernelBase(void);
extern void func_Mitigations(void);
extern void func_PrivateNamespace(void);
extern void func_RestrictedToken(void);
extern void func_SecurityApi(void);
extern void func_Uipi(void);

const struct test winetest_testlist[] =
{
    { "AppContainer",    func_AppContainer },
    { "Broker",          func_Broker },
    { "Desktop",         func_Desktop },
    { "Integrity",       func_Integrity },
    { "JobLifecycle",    func_JobLifecycle },
    { "JobMemory",       func_JobMemory },
    { "JobUi",           func_JobUi },
    { "KernelBase",      func_KernelBase },
    { "Mitigations",     func_Mitigations },
    { "PrivateNamespace", func_PrivateNamespace },
    { "RestrictedToken", func_RestrictedToken },
    { "SecurityApi",     func_SecurityApi },
    { "Uipi",            func_Uipi },
    { 0, 0 }
};
