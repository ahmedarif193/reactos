/*
 * PROJECT:         ReactOS kernel-mode tests
 * LICENSE:         LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:         Kernel-Mode Test Suite for Win11-parity Rtl exports (kmwin11new)
 *
 * These cover the first priority batch of formerly-stubbed Win11 ntoskrnl Rtl
 * exports now implemented for ARM64. The same binary is run against the Win11
 * ARM64 reference kernel (ground truth) and against ReactOS.
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

/* DL_EUI48 (mstcpip.h) is user-mode oriented; declare the MAC + the Ethernet
 * helpers here as imports from ntoskrnl so this driver TU stays self-contained. */
typedef struct _KMT_DL_EUI48 { UCHAR Byte[6]; } KMT_DL_EUI48;

NTSYSAPI LONG NTAPI
RtlCompareUnicodeStrings(PCWCH, SIZE_T, PCWCH, SIZE_T, BOOLEAN);
NTSYSAPI ULONG NTAPI
RtlNumberOfSetBitsUlongPtr(ULONG_PTR);
NTSYSAPI BOOLEAN NTAPI
RtlSuffixUnicodeString(PCUNICODE_STRING, PCUNICODE_STRING, BOOLEAN);
NTSYSAPI PWSTR NTAPI
RtlEthernetAddressToStringW(const KMT_DL_EUI48 *, PWSTR);
NTSYSAPI PSTR NTAPI
RtlEthernetAddressToStringA(const KMT_DL_EUI48 *, PSTR);
NTSYSAPI NTSTATUS NTAPI
RtlEthernetStringToAddressW(PCWSTR, LPCWSTR *, KMT_DL_EUI48 *);
NTSYSAPI NTSTATUS NTAPI
RtlEthernetStringToAddressA(PCSTR, PCSTR *, KMT_DL_EUI48 *);
NTSYSAPI NTSTATUS NTAPI
RtlStringFromGUIDEx(REFGUID, PUNICODE_STRING, BOOLEAN);
NTSYSAPI ULONG NTAPI
RtlCrc32(const void *, SIZE_T, ULONG);
NTSYSAPI ULONGLONG NTAPI
RtlCrc64(const void *, SIZE_T, ULONGLONG);

/* Secure CRT (*_s) exports (sdk/lib/crt/arm64_win11compat.c). */
int __cdecl memcpy_s(void *, SIZE_T, const void *, SIZE_T);
int __cdecl strcpy_s(char *, SIZE_T, const char *);
int __cdecl strcat_s(char *, SIZE_T, const char *);
int __cdecl sprintf_s(char *, SIZE_T, const char *, ...);
int __cdecl _snprintf_s(char *, SIZE_T, SIZE_T, const char *, ...);
int __cdecl swprintf_s(WCHAR *, SIZE_T, const WCHAR *, ...);

/* HAL processor queries (hal/halarm64/kernstubs.c, forwarded by ntoskrnl). */
NTSYSAPI ULONG NTAPI HalQueryMaximumProcessorCount(VOID);
NTSYSAPI BOOLEAN NTAPI HalIsHyperThreadingEnabled(VOID);
NTSYSAPI NTSTATUS NTAPI
RtlUnicodeToUTF8N(PCHAR, ULONG, PULONG, PCWCH, ULONG);
NTSYSAPI NTSTATUS NTAPI
RtlUTF8ToUnicodeN(PWSTR, ULONG, PULONG, PCCH, ULONG);

static
VOID
TestCompareUnicodeStrings(VOID)
{
    static const WCHAR abc[] = L"abc";
    static const WCHAR abd[] = L"abd";
    static const WCHAR ABC[] = L"ABC";
    static const WCHAR ab[]  = L"ab";

    /* Equal -> 0 */
    ok(RtlCompareUnicodeStrings(abc, 3, abc, 3, FALSE) == 0, "equal cs should be 0\n");
    /* 'c' < 'd' -> negative */
    ok(RtlCompareUnicodeStrings(abc, 3, abd, 3, FALSE) < 0, "abc<abd should be <0\n");
    /* 'd' > 'c' -> positive */
    ok(RtlCompareUnicodeStrings(abd, 3, abc, 3, FALSE) > 0, "abd>abc should be >0\n");
    /* shorter is "less" when common prefix equal */
    ok(RtlCompareUnicodeStrings(ab, 2, abc, 3, FALSE) < 0, "ab<abc should be <0\n");
    /* case-sensitive: 'A'(0x41) < 'a'(0x61) -> negative */
    ok(RtlCompareUnicodeStrings(ABC, 3, abc, 3, FALSE) < 0, "ABC<abc (cs) should be <0\n");
    /* case-insensitive: equal -> 0 */
    ok(RtlCompareUnicodeStrings(ABC, 3, abc, 3, TRUE) == 0, "ABC==abc (ci) should be 0\n");
}

static
VOID
TestNumberOfSetBits(VOID)
{
    ok_eq_ulong(RtlNumberOfSetBitsUlongPtr(0), 0UL);
    ok_eq_ulong(RtlNumberOfSetBitsUlongPtr(0xF), 4UL);
    ok_eq_ulong(RtlNumberOfSetBitsUlongPtr(0x5), 2UL);
    ok_eq_ulong(RtlNumberOfSetBitsUlongPtr(0xFF), 8UL);
#ifdef _WIN64
    ok_eq_ulong(RtlNumberOfSetBitsUlongPtr((ULONG_PTR)0xFFFFFFFFFFFFFFFFULL), 64UL);
#else
    ok_eq_ulong(RtlNumberOfSetBitsUlongPtr((ULONG_PTR)0xFFFFFFFFUL), 32UL);
#endif
}

static
VOID
TestSuffixUnicodeString(VOID)
{
    UNICODE_STRING Full, Bar, Foo, BarUp, TooLong;

    RtlInitUnicodeString(&Full, L"foobar");
    RtlInitUnicodeString(&Bar, L"bar");
    RtlInitUnicodeString(&Foo, L"foo");
    RtlInitUnicodeString(&BarUp, L"BAR");
    RtlInitUnicodeString(&TooLong, L"superfoobar");

    ok(RtlSuffixUnicodeString(&Bar, &Full, FALSE) == TRUE, "'bar' is a suffix of 'foobar'\n");
    ok(RtlSuffixUnicodeString(&Foo, &Full, FALSE) == FALSE, "'foo' is NOT a suffix of 'foobar'\n");
    ok(RtlSuffixUnicodeString(&BarUp, &Full, TRUE) == TRUE, "'BAR' (ci) is a suffix of 'foobar'\n");
    ok(RtlSuffixUnicodeString(&BarUp, &Full, FALSE) == FALSE, "'BAR' (cs) is NOT a suffix of 'foobar'\n");
    ok(RtlSuffixUnicodeString(&TooLong, &Full, FALSE) == FALSE, "longer suffix should be FALSE\n");
}

static
VOID
TestEthernet(VOID)
{
    KMT_DL_EUI48 Addr = { { 0x01, 0x23, 0x45, 0x67, 0x89, 0xAB } };
    KMT_DL_EUI48 Parsed;
    WCHAR WBuf[18];
    CHAR ABuf[18];
    PWSTR WEnd;
    PSTR AEnd;
    LPCWSTR WTerm;
    PCSTR ATerm;
    NTSTATUS Status;
    UNICODE_STRING Got, Expected;

    /* AddressToStringW: canonical upper-hex, dash-separated, ptr-to-NUL return */
    WEnd = RtlEthernetAddressToStringW(&Addr, WBuf);
    ok(*WEnd == UNICODE_NULL, "W return should point at NUL\n");
    ok_eq_ulong((ULONG)(WEnd - WBuf), 17UL);
    RtlInitUnicodeString(&Got, WBuf);
    RtlInitUnicodeString(&Expected, L"01-23-45-67-89-AB");
    ok(RtlEqualUnicodeString(&Got, &Expected, FALSE), "W string '%wZ'\n", &Got);

    /* AddressToStringA */
    AEnd = RtlEthernetAddressToStringA(&Addr, ABuf);
    ok(*AEnd == ANSI_NULL, "A return should point at NUL\n");
    ok_eq_ulong((ULONG)(AEnd - ABuf), 17UL);
    ok(strcmp(ABuf, "01-23-45-67-89-AB") == 0, "A string '%s'\n", ABuf);

    /* StringToAddressW round-trip */
    RtlZeroMemory(&Parsed, sizeof(Parsed));
    Status = RtlEthernetStringToAddressW(L"01-23-45-67-89-AB", &WTerm, &Parsed);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(RtlCompareMemory(&Parsed, &Addr, 6) == 6, "W parse round-trip mismatch\n");

    /* StringToAddressA round-trip */
    RtlZeroMemory(&Parsed, sizeof(Parsed));
    Status = RtlEthernetStringToAddressA("01-23-45-67-89-AB", &ATerm, &Parsed);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(RtlCompareMemory(&Parsed, &Addr, 6) == 6, "A parse round-trip mismatch\n");
}

static
VOID
TestStringFromGUIDEx(VOID)
{
    static const GUID g = { 0x12345678, 0x9ABC, 0xDEF0, { 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0 } };
    WCHAR buf[40];
    UNICODE_STRING us, expected;
    NTSTATUS status;

    /* Allocate=FALSE into caller buffer */
    us.Buffer = buf;
    us.Length = 0;
    us.MaximumLength = sizeof(buf);
    status = RtlStringFromGUIDEx(&g, &us, FALSE);
    ok_eq_hex(status, STATUS_SUCCESS);
    ok_eq_ulong((ULONG)us.Length, 38UL * sizeof(WCHAR));
    RtlInitUnicodeString(&expected, L"{12345678-9abc-def0-1234-56789abcdef0}");
    ok(RtlEqualUnicodeString(&us, &expected, FALSE), "GUIDEx(no-alloc) = %wZ\n", &us);

    /* Allocate=FALSE with too-small buffer -> STATUS_BUFFER_TOO_SMALL */
    us.Buffer = buf;
    us.Length = 0;
    us.MaximumLength = 10;
    status = RtlStringFromGUIDEx(&g, &us, FALSE);
    ok_eq_hex(status, STATUS_BUFFER_TOO_SMALL);

    /* Allocate=TRUE -> allocates a freeable buffer */
    RtlInitUnicodeString(&us, NULL);
    status = RtlStringFromGUIDEx(&g, &us, TRUE);
    ok_eq_hex(status, STATUS_SUCCESS);
    if (NT_SUCCESS(status))
    {
        ok(us.Buffer != NULL, "alloc buffer\n");
        ok(RtlEqualUnicodeString(&us, &expected, FALSE), "GUIDEx(alloc) = %wZ\n", &us);
        RtlFreeUnicodeString(&us);
    }
}

static
VOID
TestCrc32(VOID)
{
    /* Win11's RtlCrc32 is CRC-32C (Castagnoli), verified on the reference kernel:
     * RtlCrc32("123456789", 9, 0) == 0xE3069283 (NOT zlib CRC-32 0xCBF43926). */
    ok_eq_hex(RtlCrc32("123456789", 9, 0), 0xE3069283UL);
    ok_eq_hex(RtlCrc32("", 0, 0), 0UL);
    /* Win11's RtlCrc64 uses reflected poly 0x9A6C9329AC4BC9B5 (verified). */
    {
        ULONGLONG c64 = RtlCrc64("123456789", 9, 0);
        ok(c64 == 0xAE8B14860A799888ULL, "RtlCrc64 = 0x%I64x, expected 0x%I64x\n",
           c64, (ULONGLONG)0xAE8B14860A799888ULL);
    }
}

static
VOID
TestUtf8(VOID)
{
    /* "A" + U+00E9 (e-acute) + U+20AC (euro) -> 1 + 2 + 3 = 6 UTF-8 bytes. */
    static const WCHAR uni[] = { 0x0041, 0x00E9, 0x20AC };
    CHAR utf8[16];
    WCHAR back[8];
    ULONG produced = 0;
    NTSTATUS status;

    status = RtlUnicodeToUTF8N(utf8, sizeof(utf8), &produced, uni, sizeof(uni));
    ok_eq_hex(status, STATUS_SUCCESS);
    ok_eq_ulong(produced, 6UL);
    ok((UCHAR)utf8[0] == 0x41 && (UCHAR)utf8[1] == 0xC3 && (UCHAR)utf8[2] == 0xA9 &&
       (UCHAR)utf8[3] == 0xE2 && (UCHAR)utf8[4] == 0x82 && (UCHAR)utf8[5] == 0xAC,
       "UTF-8 bytes mismatch\n");

    /* Round-trip back to UTF-16. */
    produced = 0;
    status = RtlUTF8ToUnicodeN(back, sizeof(back), &produced, utf8, 6);
    ok_eq_hex(status, STATUS_SUCCESS);
    ok_eq_ulong(produced, 3UL * sizeof(WCHAR));
    ok(back[0] == uni[0] && back[1] == uni[1] && back[2] == uni[2], "UTF-16 round-trip mismatch\n");
}

static
VOID
TestSecureCrt(VOID)
{
    char buf[32];
    WCHAR wbuf[32];
    int rc;

    /* memcpy_s: success + overflow */
    RtlZeroMemory(buf, sizeof(buf));
    rc = memcpy_s(buf, sizeof(buf), "hello", 6);
    ok_eq_int(rc, 0);
    ok(strcmp(buf, "hello") == 0, "memcpy_s buf '%s'\n", buf);
    rc = memcpy_s(buf, 4, "toolong", 8);
    ok(rc != 0, "memcpy_s overflow should fail, rc=%d\n", rc);

    /* strcpy_s: success + overflow */
    rc = strcpy_s(buf, sizeof(buf), "world");
    ok_eq_int(rc, 0);
    ok(strcmp(buf, "world") == 0, "strcpy_s buf '%s'\n", buf);
    rc = strcpy_s(buf, 3, "toolong");
    ok(rc != 0, "strcpy_s overflow should fail, rc=%d\n", rc);

    /* strcat_s */
    rc = strcpy_s(buf, sizeof(buf), "ab");
    ok_eq_int(rc, 0);
    rc = strcat_s(buf, sizeof(buf), "cd");
    ok_eq_int(rc, 0);
    ok(strcmp(buf, "abcd") == 0, "strcat_s buf '%s'\n", buf);

    /* sprintf_s: returns count, formats correctly */
    rc = sprintf_s(buf, sizeof(buf), "%d-%s", 42, "x");
    ok_eq_int(rc, 4);
    ok(strcmp(buf, "42-x") == 0, "sprintf_s buf '%s'\n", buf);

    /* _snprintf_s with _TRUNCATE */
    rc = _snprintf_s(buf, sizeof(buf), (SIZE_T)-1, "%s", "hi");
    ok_eq_int(rc, 2);
    ok(strcmp(buf, "hi") == 0, "_snprintf_s buf '%s'\n", buf);

    /* swprintf_s */
    rc = swprintf_s(wbuf, 32, L"%d", 7);
    ok_eq_int(rc, 1);
    ok(wbuf[0] == L'7' && wbuf[1] == 0, "swprintf_s wbuf[0]=0x%x\n", wbuf[0]);
}

static
VOID
TestHalProc(VOID)
{
    ULONG halMax = HalQueryMaximumProcessorCount();
    ULONG active = KeQueryActiveProcessorCount(NULL);

    /* Contract: max >= active >= 1 (exact values are VM-config dependent). */
    ok(halMax >= 1, "HalQueryMaximumProcessorCount=%lu should be >=1\n", halMax);
    ok(halMax >= active, "HalMax=%lu should be >= active=%lu\n", halMax, active);
    ok(HalIsHyperThreadingEnabled() == FALSE, "ARM64 HalIsHyperThreadingEnabled should be FALSE\n");
}

START_TEST(Win11NewKM)
{
    TestCompareUnicodeStrings();
    TestNumberOfSetBits();
    TestSuffixUnicodeString();
    TestEthernet();
    TestStringFromGUIDEx();
    TestCrc32();
    TestUtf8();
    TestSecureCrt();
    TestHalProc();
}
