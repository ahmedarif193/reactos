/*
 * PROJECT:     ReactOS Task Manager v2
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     CPUID-based CPU topology detection
 * COPYRIGHT:   Copyright 2026 ReactOS Team
 */

#include "precomp.h"

#include <intrin.h>
#include <ndk/exfuncs.h>   /* SYSTEM_LOGICAL_PROCESSOR_INFORMATION via xdk path */

/* GetLogicalProcessorInformation is in kernel32 but not declared in the
 * ReactOS SDK headers at _WIN32_WINNT=0x502.  Forward-declare it here. */
BOOL WINAPI GetLogicalProcessorInformation(
    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION Buffer,
    PDWORD ReturnedLength);

/* --------------------------------------------------------------------------
 * Internal state — populated once by CpuTopo_Detect().
 * -------------------------------------------------------------------------- */

static BOOL       s_detected     = FALSE;
static CPU_VENDOR s_vendor       = CPU_VENDOR_UNKNOWN;
static WCHAR      s_brandW[64]   = {0};  /* widened CPUID brand string */
static DWORD      s_sockets      = 1;
static DWORD      s_cores        = 1;
static DWORD      s_lps          = 1;
static SIZE_T     s_l1           = 0;
static SIZE_T     s_l2           = 0;
static SIZE_T     s_l3           = 0;
static BOOL       s_virtEnabled  = FALSE;
static DWORD      s_maxMhz       = 0;

/* --------------------------------------------------------------------------
 * Helpers
 * -------------------------------------------------------------------------- */

/*
 * Widen an ASCII CPUID string (at most cch chars) into dst.
 * dst must have room for cch+1 WCHARs.
 */
static void AsciiToWide(WCHAR *dst, const char *src, int cch)
{
    int i;
    for (i = 0; i < cch && src[i] != '\0'; i++)
        dst[i] = (WCHAR)(unsigned char)src[i];
    dst[i] = L'\0';
}

/*
 * Trim leading/trailing spaces from a wide string in-place.
 */
static void TrimWide(WCHAR *s)
{
    WCHAR *start = s, *end;
    while (*start == L' ') start++;
    end = start + lstrlenW(start);
    while (end > start && *(end - 1) == L' ') end--;
    *end = L'\0';
    if (start != s)
        MoveMemory(s, start, (end - start + 1) * sizeof(WCHAR));
}

/*
 * Parse "@ X.XXGHz" or "X.XXGHz" from the brand string.
 * Returns MHz, or 0 on failure.
 * We scan for the first occurrence of a decimal number followed by "GHz" or "MHz".
 */
static DWORD ParseMhzFromBrand(const WCHAR *brand)
{
    const WCHAR *p = brand;
    while (*p)
    {
        /* Look for a digit sequence with an optional decimal point. */
        if (*p >= L'0' && *p <= L'9')
        {
            double val = 0.0;
            int decimals = 0;
            BOOL inFrac = FALSE;
            double fracMul = 1.0;
            const WCHAR *q = p;

            while (*q >= L'0' && *q <= L'9')
            {
                val = val * 10.0 + (*q - L'0');
                q++;
            }
            if (*q == L'.')
            {
                inFrac = TRUE;
                q++;
                while (*q >= L'0' && *q <= L'9')
                {
                    fracMul *= 0.1;
                    val += (*q - L'0') * fracMul;
                    q++;
                    decimals++;
                }
            }
            (void)inFrac;
            (void)decimals;

            /* Skip any spaces */
            while (*q == L' ') q++;

            if ((q[0] == L'G' || q[0] == L'g') &&
                (q[1] == L'H' || q[1] == L'h') &&
                (q[2] == L'z' || q[2] == L'Z'))
            {
                return (DWORD)(val * 1000.0 + 0.5);
            }
            if ((q[0] == L'M' || q[0] == L'm') &&
                (q[1] == L'H' || q[1] == L'h') &&
                (q[2] == L'z' || q[2] == L'Z'))
            {
                return (DWORD)(val + 0.5);
            }
        }
        p++;
    }
    return 0;
}

/* --------------------------------------------------------------------------
 * Cache detection via CPUID
 * -------------------------------------------------------------------------- */

/*
 * Intel: leaf 4 (deterministic cache parameters).
 * AMD:   leaf 0x8000001D.
 * We call whichever is appropriate for the detected vendor and accumulate
 * L1, L2, L3 sizes.
 *
 * Cache size = (ways + 1) * (partitions + 1) * (line_size + 1) * (sets + 1).
 */
static void DetectCacheIntel(void)
{
    int regs[4];
    DWORD idx = 0;

    s_l1 = 0; s_l2 = 0; s_l3 = 0;

    for (;;)
    {
        DWORD ctype, level, ways, partitions, lineSize, sets;
        SIZE_T sz;

        __cpuidex(regs, 4, (int)idx);
        ctype = (DWORD)(regs[0] & 0x1F);
        if (ctype == 0) break;  /* no more caches */

        level      = (DWORD)((regs[0] >> 5) & 0x7);
        ways       = (DWORD)((regs[1] >> 22) & 0x3FF);
        partitions = (DWORD)((regs[1] >> 12) & 0x3FF);
        lineSize   = (DWORD)(regs[1] & 0xFFF);
        sets       = (DWORD)regs[2];

        sz = ((SIZE_T)(ways + 1)) * (partitions + 1) * (lineSize + 1) * (sets + 1);

        if (level == 1)
            s_l1 += sz;
        else if (level == 2)
            s_l2 += sz;
        else if (level == 3)
            s_l3 = sz;  /* L3 is shared — just record the single instance */

        idx++;
    }
}

static void DetectCacheAMD(void)
{
    int regs[4];
    DWORD idx = 0;

    s_l1 = 0; s_l2 = 0; s_l3 = 0;

    /* Check max extended leaf */
    __cpuid(regs, (int)0x80000000);
    if ((DWORD)regs[0] < 0x8000001DU)
    {
        /* Fall back to leaf 0x80000005/6 for very old CPUs */
        __cpuid(regs, (int)0x80000005);
        /* L1D: bits 31:24 of ECX = size in KB */
        s_l1 = ((SIZE_T)((regs[2] >> 24) & 0xFF)) * 1024;
        /* L1I: bits 31:24 of EDX */
        s_l1 += ((SIZE_T)((regs[3] >> 24) & 0xFF)) * 1024;

        __cpuid(regs, (int)0x80000006);
        /* L2: bits 31:16 of ECX = size in KB */
        s_l2 = ((SIZE_T)((regs[2] >> 16) & 0xFFFF)) * 1024;
        /* L3: bits 31:18 of EDX = size in 512 KB units */
        s_l3 = ((SIZE_T)((regs[3] >> 18) & 0x3FFF)) * 512 * 1024;
        return;
    }

    for (;;)
    {
        DWORD ctype, level, ways, partitions, lineSize, sets;
        SIZE_T sz;

        __cpuidex(regs, (int)0x8000001D, (int)idx);
        ctype = (DWORD)(regs[0] & 0x1F);
        if (ctype == 0) break;

        level      = (DWORD)((regs[0] >> 5) & 0x7);
        ways       = (DWORD)((regs[1] >> 22) & 0x3FF);
        partitions = (DWORD)((regs[1] >> 12) & 0x3FF);
        lineSize   = (DWORD)(regs[1] & 0xFFF);
        sets       = (DWORD)regs[2];

        sz = ((SIZE_T)(ways + 1)) * (partitions + 1) * (lineSize + 1) * (sets + 1);

        if (level == 1)
            s_l1 += sz;
        else if (level == 2)
            s_l2 += sz;
        else if (level == 3)
            s_l3 = sz;

        idx++;
    }
}

/* --------------------------------------------------------------------------
 * Topology detection via Win32 (most reliable — avoids CPUID parsing hell)
 * -------------------------------------------------------------------------- */
static void DetectTopologyWin32(void)
{
    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION buf = NULL;
    DWORD bufLen = 0;
    BOOL ok;

    /* Get required buffer size */
    ok = GetLogicalProcessorInformation(NULL, &bufLen);
    (void)ok;
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || bufLen == 0)
    {
        /* Fallback: use GetSystemInfo */
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        s_lps = si.dwNumberOfProcessors;
        s_cores = s_lps;
        s_sockets = 1;
        return;
    }

    buf = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION)HeapAlloc(GetProcessHeap(), 0, bufLen);
    if (!buf)
    {
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        s_lps = si.dwNumberOfProcessors;
        s_cores = s_lps;
        s_sockets = 1;
        return;
    }

    if (!GetLogicalProcessorInformation(buf, &bufLen))
    {
        HeapFree(GetProcessHeap(), 0, buf);
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        s_lps = si.dwNumberOfProcessors;
        s_cores = s_lps;
        s_sockets = 1;
        return;
    }

    {
        DWORD numEntries = bufLen / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
        DWORD i;
        DWORD cores = 0, sockets = 0, lps = 0;

        for (i = 0; i < numEntries; i++)
        {
            switch (buf[i].Relationship)
            {
                case RelationProcessorCore:
                {
                    DWORD_PTR mask = buf[i].ProcessorMask;
                    DWORD lpBits = 0;
                    while (mask)
                    {
                        lpBits += (DWORD)(mask & 1);
                        mask >>= 1;
                    }
                    cores++;
                    lps += lpBits;
                    break;
                }
                case RelationProcessorPackage:
                    sockets++;
                    break;
                default:
                    break;
            }
        }

        s_sockets = (sockets > 0) ? sockets : 1;
        s_cores   = (cores > 0)   ? cores   : lps;
        s_lps     = (lps > 0)     ? lps     : 1;
    }

    HeapFree(GetProcessHeap(), 0, buf);
}

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

void CpuTopo_Detect(void)
{
    int regs[4];
    char vendorStr[13];
    char brandBuf[48];

    if (s_detected) return;
    s_detected = TRUE;

    /* --- Vendor string (leaf 0) --- */
    __cpuid(regs, 0);
    /* EBX:EDX:ECX form the vendor string */
    ((int*)vendorStr)[0] = regs[1];
    ((int*)vendorStr)[1] = regs[3];
    ((int*)vendorStr)[2] = regs[2];
    vendorStr[12] = '\0';

    if (memcmp(vendorStr, "GenuineIntel", 12) == 0)
        s_vendor = CPU_VENDOR_INTEL;
    else if (memcmp(vendorStr, "AuthenticAMD", 12) == 0)
        s_vendor = CPU_VENDOR_AMD;
    else
        s_vendor = CPU_VENDOR_UNKNOWN;

    /* --- Brand string (leaves 0x80000002..4) --- */
    {
        int extMax;
        __cpuid(regs, (int)0x80000000);
        extMax = regs[0];

        if (extMax >= (int)0x80000004)
        {
            int b[12];
            __cpuid(b + 0, (int)0x80000002);
            __cpuid(b + 4, (int)0x80000003);
            __cpuid(b + 8, (int)0x80000004);
            memcpy(brandBuf, b, 48);
            brandBuf[47] = '\0';
            AsciiToWide(s_brandW, brandBuf, 47);
            TrimWide(s_brandW);
        }
        else
        {
            lstrcpyW(s_brandW, L"Unknown CPU");
        }
    }

    /* --- Virtualization --- */
    /* Intel VMX: CPUID.1:ECX bit 5 */
    /* AMD SVM:   CPUID.80000001:ECX bit 2 */
    __cpuid(regs, 1);
    if (s_vendor == CPU_VENDOR_INTEL)
    {
        s_virtEnabled = ((regs[2] >> 5) & 1) ? TRUE : FALSE;
    }
    else if (s_vendor == CPU_VENDOR_AMD)
    {
        int eregs[4];
        __cpuid(eregs, (int)0x80000001);
        s_virtEnabled = ((eregs[2] >> 2) & 1) ? TRUE : FALSE;
    }
    else
    {
        s_virtEnabled = FALSE;
    }

    /* --- Topology via Win32 (GLPI) --- */
    DetectTopologyWin32();

    /* --- Cache sizes --- */
    if (s_vendor == CPU_VENDOR_INTEL)
        DetectCacheIntel();
    else
        DetectCacheAMD();  /* also works as fallback for unknown vendors */

    /* --- Max MHz from brand string --- */
    s_maxMhz = ParseMhzFromBrand(s_brandW);
}

CPU_VENDOR CpuTopo_GetVendor(void)
{
    return s_vendor;
}

LPCWSTR CpuTopo_GetBrandString(void)
{
    return s_brandW;
}

void CpuTopo_GetCounts(DWORD *out_sockets, DWORD *out_cores, DWORD *out_lps)
{
    if (out_sockets) *out_sockets = s_sockets;
    if (out_cores)   *out_cores   = s_cores;
    if (out_lps)     *out_lps     = s_lps;
}

void CpuTopo_GetCacheSizes(SIZE_T *out_l1, SIZE_T *out_l2, SIZE_T *out_l3)
{
    if (out_l1) *out_l1 = s_l1;
    if (out_l2) *out_l2 = s_l2;
    if (out_l3) *out_l3 = s_l3;
}

BOOL CpuTopo_HasVirtualization(void)
{
    return s_virtEnabled;
}

DWORD CpuTopo_GetMaxMhzHint(void)
{
    return s_maxMhz;
}
