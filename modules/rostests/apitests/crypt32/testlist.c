
#define STANDALONE
#include <apitest.h>

extern void func_CertEnumSystemStoreLocation(void);
extern void func_CertUpdateStore(void);

const struct test winetest_testlist[] =
{
    { "CertEnumSystemStoreLocation", func_CertEnumSystemStoreLocation },
    { "CertUpdateStore", func_CertUpdateStore },

    { 0, 0 }
};
