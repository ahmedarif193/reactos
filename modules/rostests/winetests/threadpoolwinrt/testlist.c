#define STANDALONE
#include <wine/test.h>

extern void func_threadpool(void);

const struct test winetest_testlist[] =
{
    { "threadpool", func_threadpool },
    { 0, 0 }
};
