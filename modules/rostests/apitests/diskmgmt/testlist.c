#define STANDALONE
#include <apitest.h>

extern void func_SnapshotMatch(void);
extern void func_SnapshotEnum(void);

const struct test winetest_testlist[] =
{
    { "SnapshotEnum", func_SnapshotEnum },
    { "SnapshotMatch", func_SnapshotMatch },
    { 0, 0 }
};
