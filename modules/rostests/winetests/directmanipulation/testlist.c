#define STANDALONE
#include <wine/test.h>
extern void func_manipulation(void);
const struct test winetest_testlist[] = {{"manipulation", func_manipulation}, {0, 0}};
