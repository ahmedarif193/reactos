/*
 * PROJECT:     ReactOS VMX Hypervisor Launcher (rosl.exe)
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Shared utilities and global variable definitions
 * COPYRIGHT:   Copyright 2025-2026 Ahmed Arif
 */

#include "rosl_common.h"

/* ---- Global variable definitions ---------------------------------------- */

HANDLE g_hDev = INVALID_HANDLE_VALUE;
volatile BOOL g_Running = TRUE;
HANDLE g_hStdin = INVALID_HANDLE_VALUE;
HANDLE g_hStdout = INVALID_HANDLE_VALUE;
DWORD g_OrigConsoleMode = 0;
DWORD g_OrigStdoutMode = 0;
UINT g_OrigConsoleCP = 0;
UINT g_OrigStdoutCP = 0;
BOOL g_StdinIsConsole = FALSE;
BOOL g_StdoutIsConsole = FALSE;
BOOL g_ConsoleCpChanged = FALSE;
BOOL g_StdoutCpChanged = FALSE;
DWORD g_StdinType = FILE_TYPE_UNKNOWN;
ROSL_IO_MODE g_IoMode = RoslModePty;
ROSL_WSL_PROBE_MODE g_WslProbeMode = RoslWslProbeAuto;
ULONG g_PtyIndex = (ULONG)-1;
ULONG g_ReaderIndex = 0;
ULONG g_VconPort = 0;           /* Virtio-console port index (--vcon N) */
ULONG g_AttachSessionId = (ULONG)-1;
BOOL g_ForceService = FALSE;
BOOL g_SelfTestMode = FALSE;
USHORT g_InitialRows = 0;   /* 0 = auto-detect */
USHORT g_InitialCols = 0;   /* 0 = auto-detect */
const char *g_DiskImagePath = NULL; /* --disk <path> */
const char *g_BootCmd = NULL;       /* --cmd <command> */
ULONG g_NetBackendType = 2;         /* 0=none, 1=netd, 2=netio (default) */
BOOL g_ProbeNetTests = FALSE;       /* ping/apt disabled — virtio-net confirmed working */
BOOL g_CursorKeysApplication = FALSE;
UCHAR g_VtSeqBuf[16];
DWORD g_VtSeqLen = 0;
ULONG g_ServiceSessionId = 0;
/* g_ServiceAllocatedVconMask removed — vcon port allocation is now driver-side */
BOOL g_ServiceSerialPromptSeen = FALSE;
HANDLE g_ControlStopEvent = NULL;
HANDLE g_ControlListenerThread = NULL;
HANDLE g_ServicePromptEvent = NULL;
CRITICAL_SECTION g_ControlStateLock;
BOOL g_ControlStateLockInitialized = FALSE;
char g_ControlPipeName[128];
HANDLE g_DiscoveryMapping = NULL;
ROSL_DISCOVERY_SHARED *g_DiscoveryView = NULL;

/* ---- Helpers ------------------------------------------------------------ */

void RoslLog(const char *fmt, ...)
{
    char ts[32];
    char buf[512];
    char line[640];
    SYSTEMTIME st;
    va_list ap;
    int length;

    GetLocalTime(&st);
    _snprintf(ts, sizeof(ts), "%02u:%02u:%02u",
              st.wHour, st.wMinute, st.wSecond);

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    buf[sizeof(buf) - 1] = '\0';
    length = _snprintf(line, sizeof(line), "[%s] %s", ts, buf);
    if (length < 0 || length >= (int)sizeof(line))
        line[sizeof(line) - 1] = '\0';

    RoslWriteStyledLog(line);
    DbgPrint("rosl: [%s] %s", ts, buf);
}

ULONG
RoslDetectTscEarlyKHz(
    VOID)
{
    int CpuInfo[4];
    ULONG MaxLeaf;
    FILE *File;
    char Buf[64];

    __cpuid(CpuInfo, 0);
    MaxLeaf = (ULONG)CpuInfo[0];

    if (MaxLeaf >= 0x16)
    {
        __cpuidex(CpuInfo, 0x16, 0);
        if (CpuInfo[0] > 0)
            return (ULONG)CpuInfo[0] * 1000U;
    }

    if (MaxLeaf >= 0x15)
    {
        ULONG64 CrystalHz;
        ULONG64 TscKHz;

        __cpuidex(CpuInfo, 0x15, 0);
        if (CpuInfo[0] > 0 && CpuInfo[1] > 0 && CpuInfo[2] > 0)
        {
            CrystalHz = (ULONG64)(ULONG)CpuInfo[2];
            TscKHz = (CrystalHz * (ULONG64)(ULONG)CpuInfo[1]) /
                     ((ULONG64)(ULONG)CpuInfo[0] * 1000ULL);
            if (TscKHz > 0 && TscKHz <= 0xFFFFFFFFULL)
                return (ULONG)TscKHz;
        }
    }

    {
        LARGE_INTEGER StartQpc;
        LARGE_INTEGER EndQpc;
        LARGE_INTEGER QpcFreq;
        ULONG64 StartTsc;
        ULONG64 EndTsc;
        ULONG64 DeltaQpc;
        ULONG64 DeltaTsc;
        ULONG64 EstimatedKHz;

        if (QueryPerformanceFrequency(&QpcFreq) && QpcFreq.QuadPart > 0)
        {
            QueryPerformanceCounter(&StartQpc);
            StartTsc = __rdtsc();

            do
            {
                QueryPerformanceCounter(&EndQpc);
            } while ((ULONG64)(EndQpc.QuadPart - StartQpc.QuadPart) <
                     ((ULONG64)QpcFreq.QuadPart / 50ULL));

            EndTsc = __rdtsc();
            DeltaQpc = (ULONG64)(EndQpc.QuadPart - StartQpc.QuadPart);
            DeltaTsc = EndTsc - StartTsc;

            if (DeltaQpc != 0 && DeltaTsc != 0)
            {
                EstimatedKHz = (DeltaTsc * (ULONG64)QpcFreq.QuadPart) /
                               (DeltaQpc * 1000ULL);
                if (EstimatedKHz > 0 && EstimatedKHz <= 0xFFFFFFFFULL)
                    return (ULONG)EstimatedKHz;
            }
        }
    }

    File = fopen("Z:\\sys\\devices\\system\\cpu\\cpu0\\cpufreq\\base_frequency", "r");
    if (File != NULL)
    {
        if (fgets(Buf, sizeof(Buf), File) != NULL)
        {
            ULONG TscKHz = (ULONG)strtoul(Buf, NULL, 10);
            fclose(File);
            if (TscKHz != 0)
                return TscKHz;
        }

        fclose(File);
    }

    return 0;
}

const char *StateName(ROSV_VM_STATE s)
{
    switch (s)
    {
        case RosvVmStateCreated:      return "Created";
        case RosvVmStateMemorySet:    return "MemorySet";
        case RosvVmStateKernelLoaded: return "KernelLoaded";
        case RosvVmStateRunning:      return "Running";
        case RosvVmStateStopped:      return "Stopped";
        case RosvVmStateError:        return "Error";
        case RosvVmStateCrashed:      return "Crashed";
        default:                      return "Unknown";
    }
}

const char *CheckpointName(ROSV_CHECKPOINT cp)
{
    switch (cp)
    {
        case RosvCpDriverLoaded:  return "DriverLoaded";
        case RosvCpVmCreated:     return "VmCreated";
        case RosvCpMemorySet:     return "MemorySet";
        case RosvCpKernelLoaded:  return "KernelLoaded";
        case RosvCpInitrdLoaded:  return "InitrdLoaded";
        case RosvCpVmcsConfigured:return "VmcsConfigured";
        case RosvCpVmLaunched:    return "VmLaunched";
        case RosvCpFirstExit:     return "FirstExit";
        case RosvCpFirstIo:       return "FirstIo";
        case RosvCpFirstPrintk:   return "FirstPrintk";
        case RosvCpShellPrompt:   return "ShellPrompt";
        default:                  return "Unknown";
    }
}

void Die(const char *msg)
{
    RoslLog("[FAIL] %s (GetLastError=%lu)\n", msg, GetLastError());
    if (g_hDev != INVALID_HANDLE_VALUE)
        CloseHandle(g_hDev);
    ExitProcess(1);
}

BOOL Ioctl(DWORD code, void *in, DWORD inSz, void *out, DWORD outSz, DWORD *ret)
{
    DWORD bytes = 0;
    BOOL ok = DeviceIoControl(g_hDev, code, in, inSz, out, outSz, &bytes, NULL);
    if (ret) *ret = bytes;
    return ok;
}

void RoslInitControlState(void)
{
    if (!g_ControlStateLockInitialized)
    {
        InitializeCriticalSection(&g_ControlStateLock);
        g_ControlStateLockInitialized = TRUE;
    }
}

void RoslDeleteControlState(void)
{
    if (g_ControlStateLockInitialized)
    {
        DeleteCriticalSection(&g_ControlStateLock);
        g_ControlStateLockInitialized = FALSE;
    }
}

void
RoslBuildControlPipeName(
    _In_ ULONG SessionId,
    _Out_writes_(BufferSize) char *Buffer,
    _In_ size_t BufferSize)
{
    if (Buffer == NULL || BufferSize == 0)
        return;

    _snprintf(Buffer, BufferSize, "%s%lu", ROSL_CONTROL_PIPE_PREFIX, SessionId);
    Buffer[BufferSize - 1] = '\0';
}

BOOL
RoslConsoleSendInput(
    _In_reads_bytes_(Length) const void *Data,
    _In_ DWORD Length)
{
    DWORD Ret;

    if (Length == 0)
        return TRUE;

    return Ioctl(ROSV_IOCTL_CONSOLE_WRITE,
                 (void *)Data,
                 Length,
                 NULL,
                 0,
                 &Ret);
}

BOOL
RoslQueryVmStats(
    _Out_ ROSV_VM_STATS *Stats)
{
    DWORD Ret = 0;

    if (Stats == NULL)
        return FALSE;

    memset(Stats, 0, sizeof(*Stats));
    return Ioctl(ROSV_IOCTL_GET_VM_STATS,
                 NULL,
                 0,
                 Stats,
                 sizeof(*Stats),
                 &Ret) &&
           Ret >= sizeof(*Stats);
}

ULONG
RoslComputeBusyPercent(
    _In_ ULONGLONG TotalTicks,
    _In_ ULONGLONG HltTicks)
{
    ULONGLONG BusyTicks;

    if (TotalTicks == 0)
        return 0;

    if (HltTicks > TotalTicks)
        HltTicks = TotalTicks;

    BusyTicks = TotalTicks - HltTicks;
    return (ULONG)((BusyTicks * 100ULL + (TotalTicks / 2ULL)) / TotalTicks);
}

void
RoslLogVmCpuUsage(
    _In_z_ const char *Tag,
    _In_ const ROSV_VM_STATS *Stats,
    _Inout_ ULONGLONG *LastExitCount,
    _Inout_ ULONGLONG *LastTotalTicks,
    _Inout_ ULONGLONG *LastHltTicks)
{
    ULONGLONG DeltaExits;
    ULONGLONG DeltaTotalTicks;
    ULONGLONG DeltaHltTicks;
    ULONG BusyPercent;

    if (Tag == NULL || Stats == NULL ||
        LastExitCount == NULL ||
        LastTotalTicks == NULL ||
        LastHltTicks == NULL)
    {
        return;
    }

    DeltaExits = (Stats->ExitCount >= *LastExitCount) ?
                 (Stats->ExitCount - *LastExitCount) :
                 Stats->ExitCount;
    DeltaTotalTicks = (Stats->TotalTicks >= *LastTotalTicks) ?
                      (Stats->TotalTicks - *LastTotalTicks) :
                      Stats->TotalTicks;
    DeltaHltTicks = (Stats->HltTicks >= *LastHltTicks) ?
                    (Stats->HltTicks - *LastHltTicks) :
                    Stats->HltTicks;
    BusyPercent = RoslComputeBusyPercent(DeltaTotalTicks, DeltaHltTicks);

    RoslLog("[CPU] %s vcpu_busy=%lu%% exits=%llu (+%llu) hlt_yield=%llu spin_yield=%llu\n",
            Tag,
            (unsigned long)BusyPercent,
            (unsigned long long)Stats->ExitCount,
            (unsigned long long)DeltaExits,
            (unsigned long long)Stats->HltYield,
            (unsigned long long)Stats->SpinYield);

    *LastExitCount = Stats->ExitCount;
    *LastTotalTicks = Stats->TotalTicks;
    *LastHltTicks = Stats->HltTicks;
}

BOOL
RoslWriteVconPort(
    _In_ ULONG PortIndex,
    _In_reads_bytes_(Length) const void *Data,
    _In_ DWORD Length)
{
    ROSV_VCON_PORT_IO Req;
    DWORD Ret;

    if (Length == 0 || Length > sizeof(Req.Data))
        return FALSE;

    memset(&Req, 0, sizeof(Req));
    Req.PortIndex = PortIndex;
    Req.Length = Length;
    memcpy(Req.Data, Data, Length);

    return Ioctl(ROSV_IOCTL_VCON_PORT_WRITE, &Req,
                 (DWORD)(8 + Length),
                 NULL, 0, &Ret);
}

void RoslResetTerminalInputMode(void)
{
    g_CursorKeysApplication = FALSE;
    g_VtSeqLen = 0;
}

BOOL RoslIsVtFinalByte(UCHAR Byte)
{
    return (Byte >= 0x40 && Byte <= 0x7E);
}

void RoslApplyVtSequence(const UCHAR *Seq, DWORD Length)
{
    if (Length == 5 && memcmp(Seq, "\x1b[?1h", 5) == 0)
    {
        g_CursorKeysApplication = TRUE;
    }
    else if (Length == 5 && memcmp(Seq, "\x1b[?1l", 5) == 0)
    {
        g_CursorKeysApplication = FALSE;
    }
}

static BOOL RoslShouldSuppressVtSequence(const UCHAR *Seq, DWORD Length)
{
    if (Length < 4 ||
        Seq == NULL ||
        Seq[0] != '\x1b' ||
        Seq[1] != '[' ||
        Seq[2] != '?')
    {
        return FALSE;
    }

    return RoslIsVtFinalByte(Seq[Length - 1]);
}

static DWORD RoslFlushPendingVtSequence(UCHAR *Data, DWORD OutLen, DWORD OutSize)
{
    if (g_VtSeqLen == 0)
        return OutLen;

    if (OutLen + g_VtSeqLen <= OutSize)
    {
        memcpy(Data + OutLen, g_VtSeqBuf, g_VtSeqLen);
        OutLen += g_VtSeqLen;
    }

    g_VtSeqLen = 0;
    return OutLen;
}

/*
 * Track VT input mode state while suppressing DEC private mode toggles that
 * the ReactOS console still renders literally (for example ESC[?7l).
 */
DWORD RoslFeedTerminalOutput(UCHAR *Data, DWORD Length)
{
    DWORD i;
    DWORD outLen = 0;

    if (Data == NULL || Length == 0)
        return 0;

    for (i = 0; i < Length; i++)
    {
        UCHAR Byte = Data[i];

        if (g_VtSeqLen == 0)
        {
            if (Byte == '\x1b')
            {
                g_VtSeqBuf[0] = Byte;
                g_VtSeqLen = 1;
            }
            else
            {
                Data[outLen++] = Byte;
            }
            continue;
        }

        if (g_VtSeqLen >= sizeof(g_VtSeqBuf))
        {
            outLen = RoslFlushPendingVtSequence(Data, outLen, Length);
            if (Byte == '\x1b')
            {
                g_VtSeqBuf[0] = Byte;
                g_VtSeqLen = 1;
            }
            else
            {
                Data[outLen++] = Byte;
            }
            continue;
        }

        g_VtSeqBuf[g_VtSeqLen++] = Byte;

        if (g_VtSeqLen == 2)
        {
            if (Byte == '[' || Byte == 'O')
                continue;

            outLen = RoslFlushPendingVtSequence(Data, outLen, Length);
            continue;
        }

        if (g_VtSeqBuf[1] == '[')
        {
            if (RoslIsVtFinalByte(Byte))
            {
                RoslApplyVtSequence(g_VtSeqBuf, g_VtSeqLen);
                if (!RoslShouldSuppressVtSequence(g_VtSeqBuf, g_VtSeqLen))
                    outLen = RoslFlushPendingVtSequence(Data, outLen, Length);
                else
                    g_VtSeqLen = 0;
            }
        }
        else if (g_VtSeqBuf[1] == 'O')
        {
            if (RoslIsVtFinalByte(Byte))
                outLen = RoslFlushPendingVtSequence(Data, outLen, Length);
        }
        else
        {
            outLen = RoslFlushPendingVtSequence(Data, outLen, Length);
        }
    }

    return outLen;
}

DWORD RoslTranslateKeyEvent(
    _In_ const KEY_EVENT_RECORD *KeyEvent,
    _Out_writes_bytes_(SeqSize) UCHAR *Seq,
    _In_ DWORD SeqSize)
{
    DWORD ctrlState;
    BOOL alt;
    WORD virtualKey;
    WCHAR ch;
    int utf8Len;

    if (KeyEvent == NULL || Seq == NULL || SeqSize == 0)
        return 0;

    ctrlState = KeyEvent->dwControlKeyState;
    alt = (ctrlState & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) != 0;
    virtualKey = KeyEvent->wVirtualKeyCode;
    ch = KeyEvent->uChar.UnicodeChar;

    if (ch != 0)
    {
        if (ch <= 0x1F || ch == 0x7F)
        {
            if (alt && SeqSize >= 2)
            {
                Seq[0] = '\x1b';
                Seq[1] = (UCHAR)ch;
                return 2;
            }

            Seq[0] = (UCHAR)ch;
            return 1;
        }

        utf8Len = WideCharToMultiByte(CP_UTF8,
                                      0,
                                      &ch,
                                      1,
                                      (LPSTR)(alt ? Seq + 1 : Seq),
                                      (int)(alt ? SeqSize - 1 : SeqSize),
                                      NULL,
                                      NULL);
        if (utf8Len <= 0)
            return 0;

        if (alt)
        {
            if (SeqSize < (DWORD)(utf8Len + 1))
                return 0;

            Seq[0] = '\x1b';
            return (DWORD)utf8Len + 1;
        }

        return (DWORD)utf8Len;
    }

    switch (virtualKey)
    {
        case VK_UP:
            if (SeqSize >= 3) { Seq[0]='\x1b'; Seq[1]=g_CursorKeysApplication ? 'O' : '['; Seq[2]='A'; return 3; }
            break;
        case VK_DOWN:
            if (SeqSize >= 3) { Seq[0]='\x1b'; Seq[1]=g_CursorKeysApplication ? 'O' : '['; Seq[2]='B'; return 3; }
            break;
        case VK_RIGHT:
            if (SeqSize >= 3) { Seq[0]='\x1b'; Seq[1]=g_CursorKeysApplication ? 'O' : '['; Seq[2]='C'; return 3; }
            break;
        case VK_LEFT:
            if (SeqSize >= 3) { Seq[0]='\x1b'; Seq[1]=g_CursorKeysApplication ? 'O' : '['; Seq[2]='D'; return 3; }
            break;
        case VK_HOME:
            if (SeqSize >= 3) { Seq[0]='\x1b'; Seq[1]=g_CursorKeysApplication ? 'O' : '['; Seq[2]='H'; return 3; }
            break;
        case VK_END:
            if (SeqSize >= 3) { Seq[0]='\x1b'; Seq[1]=g_CursorKeysApplication ? 'O' : '['; Seq[2]='F'; return 3; }
            break;
        case VK_INSERT:
            if (SeqSize >= 4)
            {
                Seq[0]='\x1b'; Seq[1]='['; Seq[2]='2'; Seq[3]='~';
                return 4;
            }
            break;
        case VK_DELETE:
            if (SeqSize >= 4)
            {
                Seq[0]='\x1b'; Seq[1]='['; Seq[2]='3'; Seq[3]='~';
                return 4;
            }
            break;
        case VK_PRIOR:
            if (SeqSize >= 4)
            {
                Seq[0]='\x1b'; Seq[1]='['; Seq[2]='5'; Seq[3]='~';
                return 4;
            }
            break;
        case VK_NEXT:
            if (SeqSize >= 4)
            {
                Seq[0]='\x1b'; Seq[1]='['; Seq[2]='6'; Seq[3]='~';
                return 4;
            }
            break;
        case VK_F1:  if (SeqSize >= 3) { Seq[0]='\x1b'; Seq[1]='O'; Seq[2]='P'; return 3; } break;
        case VK_F2:  if (SeqSize >= 3) { Seq[0]='\x1b'; Seq[1]='O'; Seq[2]='Q'; return 3; } break;
        case VK_F3:  if (SeqSize >= 3) { Seq[0]='\x1b'; Seq[1]='O'; Seq[2]='R'; return 3; } break;
        case VK_F4:  if (SeqSize >= 3) { Seq[0]='\x1b'; Seq[1]='O'; Seq[2]='S'; return 3; } break;
        case VK_F5:  if (SeqSize >= 5) { Seq[0]='\x1b'; Seq[1]='['; Seq[2]='1'; Seq[3]='5'; Seq[4]='~'; return 5; } break;
        case VK_F6:  if (SeqSize >= 5) { Seq[0]='\x1b'; Seq[1]='['; Seq[2]='1'; Seq[3]='7'; Seq[4]='~'; return 5; } break;
        case VK_F7:  if (SeqSize >= 5) { Seq[0]='\x1b'; Seq[1]='['; Seq[2]='1'; Seq[3]='8'; Seq[4]='~'; return 5; } break;
        case VK_F8:  if (SeqSize >= 5) { Seq[0]='\x1b'; Seq[1]='['; Seq[2]='1'; Seq[3]='9'; Seq[4]='~'; return 5; } break;
        case VK_F9:  if (SeqSize >= 5) { Seq[0]='\x1b'; Seq[1]='['; Seq[2]='2'; Seq[3]='0'; Seq[4]='~'; return 5; } break;
        case VK_F10: if (SeqSize >= 5) { Seq[0]='\x1b'; Seq[1]='['; Seq[2]='2'; Seq[3]='1'; Seq[4]='~'; return 5; } break;
        case VK_F11: if (SeqSize >= 5) { Seq[0]='\x1b'; Seq[1]='['; Seq[2]='2'; Seq[3]='3'; Seq[4]='~'; return 5; } break;
        case VK_F12: if (SeqSize >= 5) { Seq[0]='\x1b'; Seq[1]='['; Seq[2]='2'; Seq[3]='4'; Seq[4]='~'; return 5; } break;
    }

    return 0;
}

/* ---- Driver loading ----------------------------------------------------- */

BOOL
WaitForServiceState(
    _In_ SC_HANDLE hSvc,
    _In_ DWORD DesiredState,
    _In_ DWORD TimeoutMs)
{
    SERVICE_STATUS_PROCESS ssp;
    DWORD bytesNeeded = 0;
    DWORD elapsed = 0;

    while (elapsed <= TimeoutMs)
    {
        if (!QueryServiceStatusEx(hSvc,
                                  SC_STATUS_PROCESS_INFO,
                                  (LPBYTE)&ssp,
                                  sizeof(ssp),
                                  &bytesNeeded))
        {
            return FALSE;
        }

        if (ssp.dwCurrentState == DesiredState)
            return TRUE;

        Sleep(100);
        elapsed += 100;
    }

    return FALSE;
}

/**
 * Ensure rosv.sys is registered as a demand-start service and started.
 * Creates the service entry if it doesn't exist yet, then starts it.
 * Returns TRUE if the driver is (now) running, FALSE on hard failure.
 */
BOOL EnsureDriverLoaded(void)
{
    SC_HANDLE hScm, hSvc;
    DWORD err;
    SERVICE_STATUS_PROCESS ssp;
    DWORD bytesNeeded = 0;

    RoslLog("[INFO] Ensuring rosv.sys is loaded\n");

    hScm = OpenSCManagerA(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!hScm)
    {
        err = GetLastError();
        RoslLog("[WARN] OpenSCManager failed (err=%lu), trying device directly\n", err);
        return TRUE; /* Let CreateFile attempt proceed */
    }

    /* Try to open existing service */
    hSvc = OpenServiceA(hScm, ROSV_SERVICE_NAME, SERVICE_ALL_ACCESS);
    if (!hSvc)
    {
        err = GetLastError();
        if (err != ERROR_SERVICE_DOES_NOT_EXIST)
        {
            RoslLog("[WARN] OpenService failed (err=%lu)\n", err);
            CloseServiceHandle(hScm);
            return TRUE;
        }

        /* Service doesn't exist yet - create it as demand-start kernel driver */
        hSvc = CreateServiceA(hScm,
                              ROSV_SERVICE_NAME,
                              "ROSV Hypervisor",
                              SERVICE_ALL_ACCESS,
                              SERVICE_KERNEL_DRIVER,
                              SERVICE_DEMAND_START,
                              SERVICE_ERROR_NORMAL,
                              ROSV_DRIVER_PATH,
                              NULL, NULL, NULL, NULL, NULL);
        if (!hSvc)
        {
            err = GetLastError();
            RoslLog("[FAIL] CreateService failed (err=%lu)\n", err);
            CloseServiceHandle(hScm);
            return FALSE;
        }
        RoslLog("[OK]   Service 'rosv' created\n");
    }

    if (QueryServiceStatusEx(hSvc,
                             SC_STATUS_PROCESS_INFO,
                             (LPBYTE)&ssp,
                             sizeof(ssp),
                             &bytesNeeded))
    {
        if (ssp.dwCurrentState == SERVICE_RUNNING)
        {
            RoslLog("[OK]   Driver already running\n");
            CloseServiceHandle(hSvc);
            CloseServiceHandle(hScm);
            return TRUE;
        }

        if (ssp.dwCurrentState == SERVICE_START_PENDING)
        {
            RoslLog("[INFO] Driver start pending; waiting for RUNNING\n");
            if (WaitForServiceState(hSvc, SERVICE_RUNNING, ROSV_SERVICE_STATE_TIMEOUT_MS))
            {
                RoslLog("[OK]   Driver started\n");
                CloseServiceHandle(hSvc);
                CloseServiceHandle(hScm);
                return TRUE;
            }
            RoslLog("[WARN] Driver start pending timeout; trying StartService\n");
        }
        else if (ssp.dwCurrentState == SERVICE_STOP_PENDING)
        {
            RoslLog("[INFO] Driver stop pending; waiting for STOPPED before start\n");
            if (!WaitForServiceState(hSvc, SERVICE_STOPPED, ROSV_SERVICE_STATE_TIMEOUT_MS))
            {
                RoslLog("[WARN] Driver stop pending timeout; proceeding with start attempt\n");
            }
        }
    }
    else
    {
        RoslLog("[WARN] QueryServiceStatusEx failed (err=%lu)\n", GetLastError());
    }

    /* Start the driver */
    if (!StartServiceA(hSvc, 0, NULL))
    {
        err = GetLastError();
        if (err == ERROR_SERVICE_ALREADY_RUNNING)
        {
            RoslLog("[OK]   Driver already running\n");
        }
        else if (err == ERROR_SERVICE_DATABASE_LOCKED || err == ERROR_SERVICE_REQUEST_TIMEOUT)
        {
            RoslLog("[WARN] StartService transient failure (err=%lu)\n", err);
            if (!WaitForServiceState(hSvc, SERVICE_RUNNING, ROSV_SERVICE_STATE_TIMEOUT_MS))
            {
                CloseServiceHandle(hSvc);
                CloseServiceHandle(hScm);
                return FALSE;
            }
            RoslLog("[OK]   Driver started\n");
        }
        else
        {
            RoslLog("[FAIL] StartService failed (err=%lu)\n", err);
            CloseServiceHandle(hSvc);
            CloseServiceHandle(hScm);
            return FALSE;
        }
    }
    else
    {
        RoslLog("[OK]   Driver started\n");
    }

    CloseServiceHandle(hSvc);
    CloseServiceHandle(hScm);
    return TRUE;
}

HANDLE
OpenRosvDeviceWithRetry(
    _In_ ULONG Attempts,
    _In_ DWORD DelayMs)
{
    HANDLE hDev;
    ULONG i;
    DWORD err = ERROR_SUCCESS;

    for (i = 0; i < Attempts; i++)
    {
        hDev = CreateFileA("\\\\.\\RosvHypervisor", GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, NULL);
        if (hDev != INVALID_HANDLE_VALUE)
            return hDev;

        err = GetLastError();
        if (err != ERROR_FILE_NOT_FOUND &&
            err != ERROR_PATH_NOT_FOUND &&
            err != ERROR_SHARING_VIOLATION)
        {
            break;
        }

        Sleep(DelayMs);
    }

    SetLastError(err);
    return INVALID_HANDLE_VALUE;
}

/* ---- Initrd info detection ---------------------------------------------- */

BOOL GetInitrdInfo(const char *path, ROSL_INITRD_INFO *info)
{
    HANDLE hFile;
    DWORD bytesRead;
    UCHAR header[10];
    UCHAR tail[4];
    LARGE_INTEGER fileSize;
    LARGE_INTEGER seekPos;

    memset(info, 0, sizeof(*info));

    hFile = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        return FALSE;

    if (!GetFileSizeEx(hFile, &fileSize) || fileSize.QuadPart < 18)
    {
        CloseHandle(hFile);
        return FALSE;
    }

    info->CompressedSize = (DWORD)fileSize.QuadPart;

    /* Read gzip header to check for magic 0x1F 0x8B */
    if (!ReadFile(hFile, header, 10, &bytesRead, NULL) || bytesRead < 10)
    {
        CloseHandle(hFile);
        return TRUE; /* Got size but not gzip info */
    }

    if (header[0] == 0x1F && header[1] == 0x8B)
    {
        info->IsGzip = TRUE;

        /* Last 4 bytes of gzip = uncompressed size mod 2^32 */
        seekPos.QuadPart = fileSize.QuadPart - 4;
        SetFilePointerEx(hFile, seekPos, NULL, FILE_BEGIN);

        if (ReadFile(hFile, tail, 4, &bytesRead, NULL) && bytesRead == 4)
        {
            info->UncompressedSize = (DWORD)tail[0] |
                                     ((DWORD)tail[1] << 8) |
                                     ((DWORD)tail[2] << 16) |
                                     ((DWORD)tail[3] << 24);
        }
    }

    CloseHandle(hFile);
    return TRUE;
}

BYTE *ReadWholeFile(const char *path, DWORD *outSize)
{
    HANDLE hFile;
    DWORD sz, bytesRead;
    BYTE *buf;

    hFile = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        RoslLog("[FAIL] Cannot open '%s' (err=%lu)\n", path, GetLastError());
        return NULL;
    }

    sz = GetFileSize(hFile, NULL);
    if (sz == INVALID_FILE_SIZE || sz == 0)
    {
        RoslLog("[FAIL] Bad file size for '%s' (err=%lu)\n", path, GetLastError());
        CloseHandle(hFile);
        return NULL;
    }

    buf = (BYTE *)HeapAlloc(GetProcessHeap(), 0, sz);
    if (!buf)
    {
        RoslLog("[FAIL] HeapAlloc(%lu) failed for '%s'\n", sz, path);
        CloseHandle(hFile);
        return NULL;
    }

    if (!ReadFile(hFile, buf, sz, &bytesRead, NULL) || bytesRead != sz)
    {
        RoslLog("[FAIL] ReadFile '%s' incomplete (%lu/%lu, err=%lu)\n",
                path, bytesRead, sz, GetLastError());
        HeapFree(GetProcessHeap(), 0, buf);
        CloseHandle(hFile);
        return NULL;
    }

    CloseHandle(hFile);
    *outSize = sz;
    RoslLog("[OK]   Loaded '%s' (%lu bytes)\n", path, sz);
    return buf;
}

BOOL StreamInitrdFile(const char *path, DWORD *outSize)
{
    HANDLE hFile = INVALID_HANDLE_VALUE;
    DWORD ret;
    DWORD bytesRead;
    DWORD totalSize;
    DWORD sent = 0;
    DWORD requestSize;
    DWORD headerSize = (DWORD)FIELD_OFFSET(ROSV_INITRD_CHUNK_REQUEST, Data);
    ROSV_INITRD_BEGIN_REQUEST beginReq;
    ROSV_INITRD_CHUNK_REQUEST *chunkReq = NULL;
    BOOL readOk;
    BOOL ok = FALSE;

    hFile = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        RoslLog("[FAIL] Cannot open '%s' (err=%lu)\n", path, GetLastError());
        return FALSE;
    }

    totalSize = GetFileSize(hFile, NULL);
    if (totalSize == INVALID_FILE_SIZE || totalSize == 0)
    {
        RoslLog("[FAIL] Bad file size for '%s' (err=%lu)\n", path, GetLastError());
        goto Cleanup;
    }

    RoslLog("[OK]   Loaded '%s' (%lu bytes)\n", path, totalSize);

    beginReq.TotalSize = totalSize;
    beginReq.Flags = 0;
    RoslLog("[INFO] ROSV_IOCTL_INITRD_BEGIN (%lu bytes)\n", totalSize);
    if (!Ioctl(ROSV_IOCTL_INITRD_BEGIN, &beginReq, sizeof(beginReq), NULL, 0, &ret))
        Die("IOCTL_INITRD_BEGIN failed");

    requestSize = headerSize + INITRD_STREAM_CHUNK_SIZE;
    chunkReq = (ROSV_INITRD_CHUNK_REQUEST *)HeapAlloc(GetProcessHeap(), 0, requestSize);
    if (!chunkReq)
    {
        RoslLog("[FAIL] HeapAlloc(%lu) failed for initrd chunk buffer\n", requestSize);
        goto Cleanup;
    }

    RoslLog("[INFO] ROSV_IOCTL_INITRD_CHUNK streaming (%u KB chunks)\n",
            INITRD_STREAM_CHUNK_SIZE / 1024);
    for (;;)
    {
        readOk = ReadFile(hFile, chunkReq->Data, INITRD_STREAM_CHUNK_SIZE, &bytesRead, NULL);
        if (!readOk)
            break;

        if (bytesRead == 0)
            break;

        chunkReq->Offset = sent;
        chunkReq->DataLength = bytesRead;
        chunkReq->Flags = 0;

        if (!Ioctl(ROSV_IOCTL_INITRD_CHUNK,
                   chunkReq,
                   headerSize + bytesRead,
                   NULL,
                   0,
                   &ret))
        {
            Die("IOCTL_INITRD_CHUNK failed");
        }

        sent += bytesRead;
        if ((sent % (32 * 1024 * 1024)) == 0 || sent == totalSize)
        {
            RoslLog("[INFO] Initrd stream progress: %lu/%lu bytes\n", sent, totalSize);
        }
    }

    if (!readOk)
    {
        RoslLog("[FAIL] ReadFile '%s' failed at %lu/%lu bytes (err=%lu)\n",
                path, sent, totalSize, GetLastError());
        goto Cleanup;
    }

    if (sent != totalSize)
    {
        RoslLog("[FAIL] Initrd stream incomplete (%lu/%lu bytes)\n", sent, totalSize);
        goto Cleanup;
    }

    RoslLog("[INFO] ROSV_IOCTL_INITRD_COMMIT\n");
    if (!Ioctl(ROSV_IOCTL_INITRD_COMMIT, NULL, 0, NULL, 0, &ret))
        Die("IOCTL_INITRD_COMMIT failed");

    RoslLog("[OK]   Initrd loaded (streamed)\n");
    *outSize = totalSize;
    ok = TRUE;

Cleanup:
    if (chunkReq)
        HeapFree(GetProcessHeap(), 0, chunkReq);
    if (hFile != INVALID_HANDLE_VALUE)
        CloseHandle(hFile);
    return ok;
}

/* ---- Console mode management -------------------------------------------- */

void ConsoleSetRawMode(void)
{
    DWORD rawMode;
    DWORD fileType;

    g_StdinIsConsole = FALSE;
    g_StdoutIsConsole = FALSE;
    g_ConsoleCpChanged = FALSE;
    g_StdoutCpChanged = FALSE;

    g_hStdin = GetStdHandle(STD_INPUT_HANDLE);
    if (g_hStdin == INVALID_HANDLE_VALUE)
    {
        RoslLog("[WARN] Cannot get stdin handle (err=%lu)\n", GetLastError());
    }

    g_StdinType = FILE_TYPE_UNKNOWN;
    if (g_hStdin != INVALID_HANDLE_VALUE)
    {
        fileType = GetFileType(g_hStdin);
        if (fileType != FILE_TYPE_UNKNOWN)
        {
            g_StdinType = fileType;
        }
    }

    if (g_hStdin != INVALID_HANDLE_VALUE && GetConsoleMode(g_hStdin, &g_OrigConsoleMode))
    {
        g_StdinIsConsole = TRUE;
        g_OrigConsoleCP = GetConsoleCP();
        if (g_OrigConsoleCP != 0)
        {
            if (!SetConsoleCP(CP_UTF8))
            {
                RoslLog("[WARN] SetConsoleCP(CP_UTF8) failed (err=%lu)\n", GetLastError());
            }
            else
            {
                g_ConsoleCpChanged = TRUE;
            }
        }

        rawMode = ENABLE_WINDOW_INPUT | ENABLE_EXTENDED_FLAGS;
#ifdef ENABLE_QUICK_EDIT_MODE
        rawMode &= ~ENABLE_QUICK_EDIT_MODE;
#endif
#ifdef ENABLE_INSERT_MODE
        rawMode &= ~ENABLE_INSERT_MODE;
#endif
        rawMode |= ENABLE_VIRTUAL_TERMINAL_INPUT;

        if (!SetConsoleMode(g_hStdin, rawMode))
        {
            rawMode &= ~ENABLE_VIRTUAL_TERMINAL_INPUT;
            if (!SetConsoleMode(g_hStdin, rawMode))
            {
                /*
                 * Fall back to the legacy raw mask if this host console does not
                 * support the full extended mode set above.
                 */
                rawMode = g_OrigConsoleMode &
                          ~(ENABLE_LINE_INPUT |
                            ENABLE_ECHO_INPUT |
                            ENABLE_PROCESSED_INPUT);
                if (!SetConsoleMode(g_hStdin, rawMode))
                {
                    RoslLog("[WARN] SetConsoleMode(raw) failed (err=%lu)\n", GetLastError());
                }
            }
        }

        /* Drop any stale events captured before raw mode was applied. */
        FlushConsoleInputBuffer(g_hStdin);
    }
    else if (g_hStdin != INVALID_HANDLE_VALUE)
    {
        RoslLog("[WARN] GetConsoleMode failed (err=%lu), stdin may be redirected\n",
                GetLastError());
    }

    ConsoleEnableVtOutput();
}

void ConsoleEnableVtOutput(void)
{
    DWORD outMode;

    g_StdoutIsConsole = FALSE;
    g_StdoutCpChanged = FALSE;

    /*
     * Enable VT output processing so guest ANSI/VT escape sequences
     * (e.g. bracketed paste mode and erase-line) render correctly.
     *
     * Do not enable DISABLE_NEWLINE_AUTO_RETURN here: PTY output is normal
     * terminal text, not a host-managed manual wrap stream. Leaving the
     * default CR-on-LF behavior intact avoids column drift after long lines.
     */
    g_hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
    if (g_hStdout != INVALID_HANDLE_VALUE && GetConsoleMode(g_hStdout, &g_OrigStdoutMode))
    {
        g_StdoutIsConsole = TRUE;
        g_OrigStdoutCP = GetConsoleOutputCP();
        if (g_OrigStdoutCP != 0)
        {
            if (!SetConsoleOutputCP(CP_UTF8))
            {
                RoslLog("[WARN] SetConsoleOutputCP(CP_UTF8) failed (err=%lu)\n",
                        GetLastError());
            }
            else
            {
                g_StdoutCpChanged = TRUE;
            }
        }

        outMode = ENABLE_PROCESSED_OUTPUT |
                  ENABLE_WRAP_AT_EOL_OUTPUT |
                  ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        if (!SetConsoleMode(g_hStdout, outMode))
        {
            RoslLog("[WARN] SetConsoleMode(stdout VT) failed (err=%lu)\n",
                    GetLastError());
        }
    }
}

void ConsoleRestoreMode(void)
{
    if (g_ConsoleCpChanged)
    {
        SetConsoleCP(g_OrigConsoleCP);
        g_ConsoleCpChanged = FALSE;
    }

    if (g_StdoutCpChanged)
    {
        SetConsoleOutputCP(g_OrigStdoutCP);
        g_StdoutCpChanged = FALSE;
    }

    if (g_hStdin != INVALID_HANDLE_VALUE && g_StdinIsConsole)
    {
        SetConsoleMode(g_hStdin, g_OrigConsoleMode);
    }
    if (g_hStdout != INVALID_HANDLE_VALUE && g_StdoutIsConsole)
    {
        SetConsoleMode(g_hStdout, g_OrigStdoutMode);
    }
}

DWORD
ReadRedirectedStdin(
    _Out_writes_bytes_(BufferSize) UCHAR *Buffer,
    _In_ DWORD BufferSize)
{
    DWORD bytesAvailable = 0;
    DWORD bytesRead = 0;
    DWORD toRead = BufferSize;

    if (Buffer == NULL || BufferSize == 0)
        return 0;

    if (g_hStdin == INVALID_HANDLE_VALUE || g_StdinIsConsole)
        return 0;

    if (g_StdinType != FILE_TYPE_PIPE)
    {
        /*
         * Headless ROSL sessions must keep the PTY/probe loop non-blocking.
         * Treat any non-pipe stdin as non-interactive and ignore it.
         */
        return 0;
    }

    /* g_StdinType is FILE_TYPE_PIPE from here on. */
    if (!PeekNamedPipe(g_hStdin, NULL, 0, NULL, &bytesAvailable, NULL))
    {
        DWORD err = GetLastError();
        if (err == ERROR_BROKEN_PIPE || err == ERROR_PIPE_NOT_CONNECTED)
            g_hStdin = INVALID_HANDLE_VALUE;
        return 0;
    }

    if (bytesAvailable == 0)
        return 0;

    if (toRead > bytesAvailable)
        toRead = bytesAvailable;

    if (!ReadFile(g_hStdin, Buffer, toRead, &bytesRead, NULL))
    {
        DWORD err = GetLastError();
        if (err == ERROR_BROKEN_PIPE || err == ERROR_HANDLE_EOF || err == ERROR_PIPE_NOT_CONNECTED)
            g_hStdin = INVALID_HANDLE_VALUE;
        return 0;
    }

    return bytesRead;
}

/* ---- Ctrl+C handler ----------------------------------------------------- */

BOOL WINAPI CtrlHandler(DWORD type)
{
    if (type == CTRL_C_EVENT &&
        g_AttachSessionId != (ULONG)-1)
    {
        UCHAR CtrlC = 0x03;

        if (g_IoMode == RoslModePty)
        {
            PtySendInput(&CtrlC, 1);
            return TRUE;
        }

        if (g_IoMode == RoslModeVcon)
        {
            VconSendInput(&CtrlC, 1);
            return TRUE;
        }
    }

    RoslLog("\n[INFO] Ctrl+C caught, shutting down VM...\n");
    g_Running = FALSE;

    if (g_AttachSessionId != (ULONG)-1 &&
        (g_IoMode == RoslModePty || g_IoMode == RoslModeVcon))
        TerminalStop();

    return TRUE;
}

/* ---- PTY/Vcon I/O helpers ----------------------------------------------- */

/*
 * Helper: send data to guest via PTY_WRITE IOCTL.
 * The data goes through termios input processing, then drains
 * from PTY InputBuf into UART RxFifo in the kernel driver.
 */
BOOL PtySendInput(const void *data, DWORD len)
{
    BYTE reqBuf[512];
    ROSV_PTY_IO_REQUEST *req = (ROSV_PTY_IO_REQUEST *)reqBuf;
    BYTE resBuf[64];
    DWORD ret;
    DWORD reqSize;

    if (len == 0 || len > sizeof(reqBuf) - 8)
        return FALSE;

    req->PtyIndex = g_PtyIndex;
    req->DataLength = len;
    memcpy(req->Data, data, len);
    reqSize = 8 + len;  /* PtyIndex(4) + DataLength(4) + data */

    return Ioctl(ROSV_IOCTL_PTY_WRITE, reqBuf, reqSize, resBuf, sizeof(resBuf), &ret);
}

/* Send input data to a virtio-console port (host -> guest) */
BOOL VconSendInput(const void *data, DWORD len)
{
    return RoslWriteVconPort(g_VconPort, data, len);
}

/* Read output data from a virtio-console port (guest -> host) */
BOOL VconReadOutput(void *buf, DWORD bufSize, DWORD *bytesRead)
{
    ROSV_VCON_PORT_IO req;
    ROSV_VCON_PORT_IO res;
    DWORD ret;

    *bytesRead = 0;

    req.PortIndex = g_VconPort;
    req.Length = bufSize < sizeof(res.Data) ? bufSize : sizeof(res.Data);

    if (!Ioctl(ROSV_IOCTL_VCON_PORT_READ, &req, 8, /* just header */
               &res, sizeof(res), &ret))
        return FALSE;

    if (res.Length > 0 && res.Length <= bufSize)
    {
        memcpy(buf, res.Data, res.Length);
        *bytesRead = res.Length;
    }
    return TRUE;
}

BOOL PtyResize(USHORT rows, USHORT cols)
{
    ROSV_PTY_RESIZE_REQUEST req;
    DWORD ret;

    if (g_PtyIndex == (ULONG)-1)
        return FALSE;

    req.PtyIndex = g_PtyIndex;
    req.Winsize.ws_row = rows;
    req.Winsize.ws_col = cols;
    req.Winsize.ws_xpixel = 0;
    req.Winsize.ws_ypixel = 0;
    return Ioctl(ROSV_IOCTL_PTY_RESIZE, &req, sizeof(req), NULL, 0, &ret);
}

/*
 * Switch host-facing PTY behavior to non-canonical/no-echo mode so each
 * keystroke is forwarded to the guest immediately instead of line-buffered.
 */
void ConfigurePtyLowLatencyMode(void)
{
    ROSV_TERMIOS termios;
    ROSV_PTY_TERMIOS_REQUEST req;
    DWORD ret;

    if (g_PtyIndex == (ULONG)-1)
        return;

    memset(&termios, 0, sizeof(termios));
    if (!Ioctl(ROSV_IOCTL_PTY_GET_TERMIOS,
               &g_PtyIndex,
               sizeof(g_PtyIndex),
               &termios,
               sizeof(termios),
               &ret))
    {
        RoslLog("[WARN] ROSV_IOCTL_PTY_GET_TERMIOS failed (err=%lu); using driver defaults\n",
                GetLastError());
        return;
    }

    /*
     * Make the host PTY fully transparent (raw passthrough).
     * All terminal processing is done by the guest-side pty (via script).
     * The host PTY should not eat control chars, echo, or convert newlines.
     */
    termios.c_lflag &= ~(ROSV_ICANON |
                         ROSV_ECHO |
                         ROSV_ECHOE |
                         ROSV_ECHOK |
                         ROSV_ECHONL |
                         ROSV_ISIG |
                         ROSV_IEXTEN);
    termios.c_iflag &= ~(ROSV_IXON | ROSV_IXOFF | ROSV_ICRNL | ROSV_INLCR | ROSV_IGNCR);
    termios.c_oflag &= ~(ROSV_OPOST | ROSV_ONLCR | ROSV_OCRNL);
    termios.c_cc[ROSV_VMIN] = 1;
    termios.c_cc[ROSV_VTIME] = 0;

    memset(&req, 0, sizeof(req));
    req.PtyIndex = g_PtyIndex;
    req.Action = ROSV_TCSANOW;
    req.Termios = termios;

    if (!Ioctl(ROSV_IOCTL_PTY_SET_TERMIOS, &req, sizeof(req), NULL, 0, &ret))
    {
        RoslLog("[WARN] ROSV_IOCTL_PTY_SET_TERMIOS failed (err=%lu); using driver defaults\n",
                GetLastError());
        return;
    }

    RoslLog("[INFO] PTY host mode: non-canonical/no-echo (low-latency input)\n");
}

/* ---- Pipe I/O helpers --------------------------------------------------- */

BOOL
RoslPipeReadExact(
    _In_ HANDLE Pipe,
    _Out_writes_bytes_(Size) void *Buffer,
    _In_ DWORD Size)
{
    DWORD TotalRead = 0;

    while (TotalRead < Size)
    {
        DWORD BytesRead = 0;

        if (!ReadFile(Pipe,
                      (PUCHAR)Buffer + TotalRead,
                      Size - TotalRead,
                      &BytesRead,
                      NULL))
        {
            return FALSE;
        }

        if (BytesRead == 0)
            return FALSE;

        TotalRead += BytesRead;
    }

    return TRUE;
}

BOOL
RoslPipeWriteExact(
    _In_ HANDLE Pipe,
    _In_reads_bytes_(Size) const void *Buffer,
    _In_ DWORD Size)
{
    DWORD TotalWritten = 0;

    while (TotalWritten < Size)
    {
        DWORD BytesWritten = 0;

        if (!WriteFile(Pipe,
                       (const UCHAR *)Buffer + TotalWritten,
                       Size - TotalWritten,
                       &BytesWritten,
                       NULL))
        {
            return FALSE;
        }

        if (BytesWritten == 0)
            return FALSE;

        TotalWritten += BytesWritten;
    }

    return TRUE;
}

/* ---- Bundled image path resolution -------------------------------------- */

BOOL GetExeDir(char *buf, DWORD bufSize)
{
    DWORD len;
    char *lastSlash;

    len = GetModuleFileNameA(NULL, buf, bufSize);
    if (len == 0 || len >= bufSize)
    {
        RoslLog("[FAIL] GetModuleFileNameA failed (err=%lu)\n", GetLastError());
        return FALSE;
    }

    lastSlash = strrchr(buf, '\\');
    if (lastSlash)
        *(lastSlash + 1) = '\0';
    else
        buf[0] = '\0';

    return TRUE;
}
