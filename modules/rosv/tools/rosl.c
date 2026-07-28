/*
 * PROJECT:     ReactOS VMX Hypervisor Launcher (rosl.exe)
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     User-mode console tool to drive rosv.sys via IOCTLs
 * COPYRIGHT:   Copyright 2025-2026 Ahmed Arif
 *
 * Usage: rosl.exe [options] [bzImage] [initrd] [ram_mb] [cmdline]
 *        rosl.exe --attach <session-id>
 *        With no args: starts the ROSL supervisor service using bundled
 *        images from <exedir>\rosv\ subdirectory.
 *
 * Options:
 *   --service         Start the supervisor service (default)
 *   --attach <id>     Connect as an interactive PTY client
 *   --pty             Use PTY client mode
 *   --uart            Keep legacy UART supervision mode
 *   --disk <path>     Use disk image as virtio-blk root (raw .img or .vhdx)
 *   --rows <N>        Initial terminal rows (default: auto-detect)
 *   --cols <N>        Initial terminal columns (default: auto-detect)
 *
 */

#include <windows.h>
#include <winioctl.h>
#include <intrin.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

/* DbgPrint from ntdll - writes to kernel debug output (serial port) */
ULONG __cdecl DbgPrint(PCSTR Format, ...);

static void RoslLog(const char *fmt, ...)
{
    char ts[32];
    char buf[512];
    SYSTEMTIME st;
    va_list ap;

    GetLocalTime(&st);
    _snprintf(ts, sizeof(ts), "%02u:%02u:%02u",
              st.wHour, st.wMinute, st.wSecond);

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    buf[sizeof(buf) - 1] = '\0';
    printf("[%s] %s", ts, buf);
    DbgPrint("rosl: [%s] %s", ts, buf);
}

static ULONG
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

/* ---- IOCTL codes (mirrored from rosv/rosv.h for user mode) -------------- */

#define ROSV_IOCTL_TYPE     0x8000

#define ROSV_IOCTL_CREATE_VM    CTL_CODE(ROSV_IOCTL_TYPE, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define ROSV_IOCTL_SET_MEMORY   CTL_CODE(ROSV_IOCTL_TYPE, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define ROSV_IOCTL_LOAD_KERNEL  CTL_CODE(ROSV_IOCTL_TYPE, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define ROSV_IOCTL_LOAD_INITRD  CTL_CODE(ROSV_IOCTL_TYPE, 0x803, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define ROSV_IOCTL_SET_CMDLINE  CTL_CODE(ROSV_IOCTL_TYPE, 0x804, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define ROSV_IOCTL_START_VM     CTL_CODE(ROSV_IOCTL_TYPE, 0x805, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define ROSV_IOCTL_STOP_VM      CTL_CODE(ROSV_IOCTL_TYPE, 0x806, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define ROSV_IOCTL_DESTROY_VM   CTL_CODE(ROSV_IOCTL_TYPE, 0x807, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define ROSV_IOCTL_GET_STATE    CTL_CODE(ROSV_IOCTL_TYPE, 0x808, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define ROSV_IOCTL_GET_LOG      CTL_CODE(ROSV_IOCTL_TYPE, 0x809, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define ROSV_IOCTL_CONSOLE_READ CTL_CODE(ROSV_IOCTL_TYPE, 0x80A, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define ROSV_IOCTL_CONSOLE_WRITE CTL_CODE(ROSV_IOCTL_TYPE, 0x80B, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define ROSV_IOCTL_INITRD_BEGIN CTL_CODE(ROSV_IOCTL_TYPE, 0x811, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define ROSV_IOCTL_INITRD_CHUNK CTL_CODE(ROSV_IOCTL_TYPE, 0x812, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define ROSV_IOCTL_INITRD_COMMIT CTL_CODE(ROSV_IOCTL_TYPE, 0x813, METHOD_BUFFERED, FILE_ANY_ACCESS)

/* PTY IOCTLs */
#define ROSV_IOCTL_PTY_CREATE   CTL_CODE(ROSV_IOCTL_TYPE, 0x820, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define ROSV_IOCTL_PTY_DESTROY  CTL_CODE(ROSV_IOCTL_TYPE, 0x821, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define ROSV_IOCTL_PTY_READ     CTL_CODE(ROSV_IOCTL_TYPE, 0x822, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define ROSV_IOCTL_PTY_WRITE    CTL_CODE(ROSV_IOCTL_TYPE, 0x823, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define ROSV_IOCTL_PTY_RESIZE   CTL_CODE(ROSV_IOCTL_TYPE, 0x824, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define ROSV_IOCTL_PTY_GET_TERMIOS CTL_CODE(ROSV_IOCTL_TYPE, 0x825, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define ROSV_IOCTL_PTY_SET_TERMIOS CTL_CODE(ROSV_IOCTL_TYPE, 0x826, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define ROSV_IOCTL_PTY_SIGNAL   CTL_CODE(ROSV_IOCTL_TYPE, 0x827, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define ROSV_IOCTL_PTY_ATTACH   CTL_CODE(ROSV_IOCTL_TYPE, 0x82A, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define ROSV_IOCTL_PTY_DETACH   CTL_CODE(ROSV_IOCTL_TYPE, 0x82B, METHOD_BUFFERED, FILE_ANY_ACCESS)

/* Session IOCTLs */
#define ROSV_IOCTL_SESSION_ATTACH CTL_CODE(ROSV_IOCTL_TYPE, 0x835, METHOD_BUFFERED, FILE_ANY_ACCESS)

/* VM stats IOCTL */
#define ROSV_IOCTL_GET_VM_STATS   CTL_CODE(ROSV_IOCTL_TYPE, 0x870, METHOD_BUFFERED, FILE_ANY_ACCESS)

/* Disk image IOCTLs (virtio-blk backend) */
#define ROSV_IOCTL_ATTACH_DISK    CTL_CODE(ROSV_IOCTL_TYPE, 0x860, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define ROSV_IOCTL_DETACH_DISK    CTL_CODE(ROSV_IOCTL_TYPE, 0x861, METHOD_BUFFERED, FILE_ANY_ACCESS)

/* Virtio-console port IOCTLs */
#define ROSV_IOCTL_VCON_PORT_WRITE CTL_CODE(ROSV_IOCTL_TYPE, 0x880, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define ROSV_IOCTL_VCON_PORT_READ  CTL_CODE(ROSV_IOCTL_TYPE, 0x881, METHOD_BUFFERED, FILE_ANY_ACCESS)

/* ---- Structures (user-mode compatible copies) --------------------------- */

typedef struct {
    ULONG PortIndex;
    ULONG Length;
    UCHAR Data[4096];
} ROSV_VCON_PORT_IO;

typedef struct {
    ULONG RamSizeMB;
    ULONG NetBackendType;  /* 0=none, 1=netd/slirp, 2=netio */
} ROSV_VM_CONFIG;

typedef struct {
    ULONG VmId;
    LONG  Status;   /* NTSTATUS */
} ROSV_VM_CREATE_RESULT;

typedef enum {
    RosvVmStateCreated = 0,
    RosvVmStateMemorySet,
    RosvVmStateKernelLoaded,
    RosvVmStateRunning,
    RosvVmStateStopped,
    RosvVmStateError,
    RosvVmStateCrashed
} ROSV_VM_STATE;

typedef enum {
    RosvCpDriverLoaded = 0,
    RosvCpVmCreated,
    RosvCpMemorySet,
    RosvCpKernelLoaded,
    RosvCpInitrdLoaded,
    RosvCpVmcsConfigured,
    RosvCpVmLaunched,
    RosvCpFirstExit,
    RosvCpFirstIo,
    RosvCpFirstPrintk,
    RosvCpShellPrompt
} ROSV_CHECKPOINT;

typedef struct {
    ROSV_VM_STATE  State;
    ULONGLONG      ExitCount;
    ULONG          LastExitReason;
    ROSV_CHECKPOINT LastCheckpoint;
} ROSV_VM_STATE_INFO;

typedef struct {
    ULONGLONG ExitCount;
    ULONGLONG ExitHlt;
    ULONGLONG ExitPreempt;
    ULONGLONG ExitEpt;
    ULONGLONG ExitIo;
    ULONGLONG ExitMsr;
    ULONGLONG ExitExtInt;
    ULONGLONG ExitIntWin;
    ULONGLONG ExitOther;
    ULONGLONG TimerInjected;
    ULONGLONG HltYield;
    ULONGLONG SpinYield;
    ULONGLONG HltTicks;
    ULONGLONG TotalTicks;
} ROSV_VM_STATS;

typedef struct {
    ULONGLONG TotalSize;
    ULONG Flags;
} ROSV_INITRD_BEGIN_REQUEST;

typedef struct {
    ULONGLONG Offset;
    ULONG DataLength;
    ULONG Flags;
    UCHAR Data[1];
} ROSV_INITRD_CHUNK_REQUEST;

/* ---- PTY structures (user-mode copies) ---------------------------------- */

typedef struct {
    USHORT InitialRows;
    USHORT InitialCols;
    ULONG Flags;
} ROSV_PTY_CREATE_REQUEST;

typedef struct {
    ULONG PtyIndex;
    ULONG ReaderIndex;
    ULONG VconPort;  /* Bound vcon port for guest shell launch */
    LONG  Status;   /* NTSTATUS */
} ROSV_PTY_CREATE_RESULT;

typedef struct {
    ULONG PtyIndex;
} ROSV_PTY_ATTACH_REQUEST;

typedef struct {
    ULONG PtyIndex;
    ULONG ReaderIndex;
    LONG  Status;   /* NTSTATUS */
} ROSV_PTY_ATTACH_RESULT;

typedef struct {
    ULONG PtyIndex;
    ULONG ReaderIndex;
} ROSV_PTY_DETACH_REQUEST;

/* PTY_READ input: PtyIndex + ReaderIndex */
typedef struct {
    ULONG PtyIndex;
    ULONG ReaderIndex;
} ROSV_PTY_READ_REQUEST;

typedef struct {
    ULONG PtyIndex;
} ROSV_PTY_DESTROY_REQUEST;

/* PTY I/O request: header + data */
typedef struct {
    ULONG PtyIndex;
    ULONG DataLength;
    UCHAR Data[1];  /* Variable-length */
} ROSV_PTY_IO_REQUEST;

/* PTY I/O result: header + data */
typedef struct {
    ULONG BytesTransferred;
    LONG  Status;
    UCHAR Data[1];  /* Variable-length */
} ROSV_PTY_IO_RESULT;

typedef struct {
    USHORT ws_row;
    USHORT ws_col;
    USHORT ws_xpixel;
    USHORT ws_ypixel;
} ROSV_WINSIZE;

typedef struct {
    ULONG PtyIndex;
    ROSV_WINSIZE Winsize;
} ROSV_PTY_RESIZE_REQUEST;

typedef struct {
    ULONG PtyIndex;
    ULONG Signal;
} ROSV_PTY_SIGNAL_REQUEST;

#define ROSV_SIGINT     2
#define ROSV_SIGQUIT    3
#define ROSV_SIGTSTP    20

/* PTY termios structures */
#define ROSV_NCCS               32
/* Input flags (c_iflag) */
#define ROSV_IGNCR              0x00080
#define ROSV_ICRNL              0x00100
#define ROSV_INLCR              0x00040
#define ROSV_IXON               0x00400
#define ROSV_IXOFF              0x01000
/* Output flags (c_oflag) */
#define ROSV_OPOST              0x00001
#define ROSV_ONLCR              0x00004
#define ROSV_OCRNL              0x00008
/* Local flags (c_lflag) */
#define ROSV_ISIG               0x00001
#define ROSV_ICANON             0x00002
#define ROSV_ECHO               0x00008
#define ROSV_ECHOE              0x00010
#define ROSV_ECHOK              0x00020
#define ROSV_ECHONL             0x00040
#define ROSV_IEXTEN             0x08000
/* Control char indices */
#define ROSV_VTIME              5
#define ROSV_VMIN               6
#define ROSV_TCSANOW            0

typedef struct {
    ULONG c_iflag;
    ULONG c_oflag;
    ULONG c_cflag;
    ULONG c_lflag;
    UCHAR c_cc[ROSV_NCCS];
    ULONG c_ispeed;
    ULONG c_ospeed;
} ROSV_TERMIOS;

typedef struct {
    ULONG PtyIndex;
    ULONG Action;
    ROSV_TERMIOS Termios;
} ROSV_PTY_TERMIOS_REQUEST;

typedef struct {
    ULONG SessionId;
} ROSV_SESSION_ATTACH_REQUEST;

typedef struct {
    ULONG SessionId;
    ULONG PtyIndex;
    LONG  Status;   /* NTSTATUS */
} ROSV_SESSION_ATTACH_RESULT;

/* ---- Disk attach structures --------------------------------------------- */

#define ROSV_DISK_PATH_MAX  260

typedef struct {
    ULONGLONG DiskSizeBytes;    /* 0 = auto-detect from file */
    ULONG Flags;                /* Reserved, must be 0 */
    ULONG PathLength;           /* Characters in Path[], excluding NUL */
    WCHAR Path[ROSV_DISK_PATH_MAX];
} ROSV_DISK_ATTACH_REQUEST;

typedef struct {
    ULONGLONG DiskSizeBytes;    /* Actual disk size */
    ULONG     DiskIndex;        /* 0 = /dev/vda */
    LONG      Status;           /* NTSTATUS */
    ULONG     DiskMode;         /* 0=ramdisk, 1=demand-paged */
    ULONG     BackendType;      /* 0=raw, 1=vhdx */
} ROSV_DISK_ATTACH_RESULT;

/* ---- Terminal bridge (terminal.c) --------------------------------------- */

extern BOOL TerminalInitialize(HANDLE hDevice, ULONG ptyIndex);
extern void TerminalDestroy(void);
extern BOOL TerminalRun(void);
extern void TerminalStop(void);
extern BOOL TerminalIsRunning(void);
extern DWORD TerminalWaitForExit(DWORD timeout);
extern BOOL TerminalGetSize(USHORT *rows, USHORT *cols);

static BOOL RoslPublishServiceDiscovery(_In_ ULONG SessionId);
static void RoslUnpublishServiceDiscovery(void);
static BOOL RoslDiscoverServiceSession(_Out_ PULONG SessionId);
static BOOL PtySendInput(const void *data, DWORD len);
static BOOL VconSendInput(const void *data, DWORD len);

/* ---- Globals ------------------------------------------------------------ */

typedef enum {
    RoslModePty = 0,    /* Default: PTY-based terminal */
    RoslModeUart,       /* Legacy: UART polling loop */
    RoslModeVcon        /* Virtio-console port */
} ROSL_IO_MODE;

typedef enum {
    RoslWslProbeAuto = 0,   /* Enable only for headless/autostart sessions */
    RoslWslProbeEnabled,    /* Force enable */
    RoslWslProbeDisabled    /* Force disable */
} ROSL_WSL_PROBE_MODE;

typedef BOOL (*ROSL_SEND_INPUT_ROUTINE)(
    _In_reads_bytes_(Length) const void *Data,
    _In_ DWORD Length);

typedef enum {
    RoslCtlOpAllocateInteractive = 1,
    RoslCtlOpReleaseInteractive = 2
} ROSL_CONTROL_OP;

typedef struct {
    ULONG Version;
    ULONG Op;
    ULONG SessionId;
    ULONG PtyIndex;
    ULONG ReaderIndex;
    USHORT Rows;
    USHORT Cols;
    ULONG Flags;
} ROSL_CONTROL_REQUEST;

typedef struct {
    ULONG Version;
    ULONG SessionId;
    ULONG PtyIndex;
    ULONG ReaderIndex;
    ULONG Flags;
    LONG Status;
} ROSL_CONTROL_RESPONSE;

typedef struct {
    ULONG Version;
    ULONG SessionId;
    ULONG ServicePid;
    ULONG Flags;
} ROSL_DISCOVERY_SHARED;

static HANDLE g_hDev = INVALID_HANDLE_VALUE;
static volatile BOOL g_Running = TRUE;
static HANDLE g_hStdin = INVALID_HANDLE_VALUE;
static HANDLE g_hStdout = INVALID_HANDLE_VALUE;
static DWORD g_OrigConsoleMode = 0;
static DWORD g_OrigStdoutMode = 0;
static UINT g_OrigConsoleCP = 0;
static UINT g_OrigStdoutCP = 0;
static BOOL g_StdinIsConsole = FALSE;
static BOOL g_StdoutIsConsole = FALSE;
static BOOL g_ConsoleCpChanged = FALSE;
static BOOL g_StdoutCpChanged = FALSE;
static DWORD g_StdinType = FILE_TYPE_UNKNOWN;
static ROSL_IO_MODE g_IoMode = RoslModePty;
static ROSL_WSL_PROBE_MODE g_WslProbeMode = RoslWslProbeAuto;
static ULONG g_PtyIndex = (ULONG)-1;
static ULONG g_ReaderIndex = 0;
static ULONG g_VconPort = 0;           /* Virtio-console port index (--vcon N) */
static ULONG g_AttachSessionId = (ULONG)-1;
static BOOL g_ForceService = FALSE;
static USHORT g_InitialRows = 0;   /* 0 = auto-detect */
static USHORT g_InitialCols = 0;   /* 0 = auto-detect */
static const char *g_DiskImagePath = NULL; /* --disk <path> */
static const char *g_BootCmd = NULL;       /* --cmd <command> */
static ULONG g_NetBackendType = 2;         /* 0=none, 1=netd, 2=netio (default) */
static BOOL g_ProbeNetTests = FALSE;       /* ping/apt disabled — virtio-net confirmed working */
static BOOL g_CursorKeysApplication = FALSE;
static UCHAR g_VtSeqBuf[16];
static DWORD g_VtSeqLen = 0;
static ULONG g_ServiceSessionId = 0;
/* g_ServiceAllocatedVconMask removed — vcon port allocation is now driver-side */
static BOOL g_ServiceSerialPromptSeen = FALSE;
static HANDLE g_ControlStopEvent = NULL;
static HANDLE g_ControlListenerThread = NULL;
static HANDLE g_ServicePromptEvent = NULL;
static CRITICAL_SECTION g_ControlStateLock;
static BOOL g_ControlStateLockInitialized = FALSE;
static char g_ControlPipeName[128];
static HANDLE g_DiscoveryMapping = NULL;
static ROSL_DISCOVERY_SHARED *g_DiscoveryView = NULL;

#define DEFAULT_RAM_MB  1024
#define DEFAULT_DISK_RAM_MB  1024  /* Need ~1GB for kernels with KASAN/debug features */
#define DEFAULT_CMDLINE_COMMON "console=ttyS0,115200n8 earlyprintk=serial,ttyS0,115200 nokaslr kasan=off nosoftlockup i8042.noaux i8042.nopnp nomodeset video=vesafb:off systemd.getty_auto=no systemd.mask=console-getty.service systemd.mask=getty@tty1.service sysctl.net.ipv4.ping_group_range=\"0 2147483647\""
#define DEFAULT_INITRD_ROOT_ARGS "rdinit=/sbin/init"
#define DEFAULT_DISK_ROOT_ARGS "root=/dev/vda rootfstype=ext4 rw rootwait init=/sbin/init"

/* Virtio MMIO device parameters appended to kernel cmdline.
 * Format: virtio_mmio.device=<size>@<baseaddr>:<irq>
 * The base address and IRQ must match what the driver configures in EPT.
 * virtio-blk: 0xFEB00000, IRQ 5 (disk, only with --disk)
 * virtio-net: 0xFEB01000, IRQ 6 (network, always present) */
#define VIRTIO_MMIO_BLK_PARAM "virtio_mmio.device=0x1000@0xFEB00000:5"
#define VIRTIO_MMIO_NET_PARAM "virtio_mmio.device=0x1000@0xFEB01000:6"
#define VIRTIO_MMIO_CON_PARAM "virtio_mmio.device=0x1000@0xFEB02000:7"

#define CONSOLE_BUF_SIZE 4096
#define POLL_INTERVAL_MS 50
#define INITRD_STREAM_CHUNK_SIZE (512 * 1024)
#define ROSL_CONTROL_VERSION 1
#define ROSL_CONTROL_PIPE_PREFIX "\\\\.\\pipe\\RoslControl-"
#define ROSL_DISCOVERY_MAPPING_NAME "RoslControlDiscovery"
#define ROSL_CONTROL_FLAG_USE_VCON 0x00000001UL
#define ROSL_VCON_FIRST_PORT 1
#define ROSL_VCON_MAX_PORTS 4
#define ROSL_VCON_PROMPT_WAIT_MS 60000
#define ROSL_WSL_PROBE_PROMPT_TIMEOUT_MS 60000
#define ROSL_WSL_PROBE_COMMAND_INTERVAL_MS 500
#define ROSL_WSL_PROBE_DONE_TIMEOUT_MS 120000
#define ROSL_WSL_PROBE_TAIL_MAX 2048

static const char *g_WslProbeCommandsBase[] = {
    "echo __ROSL_WSL2_PROBE_BEGIN__",
    "ip addr show",
    "ip route show",
    "cat /etc/os-release | head -5 || true",
    "ls /dev/vport* 2>&1 || true",
    "sudo bash -c 'setsid bash --login -i <>/dev/vport2p1 >&0 2>&1 &'",
    "echo VCON_SHELL_STARTED_ON_PORT1",
};

static const char *g_WslProbeCommandsNet[] = {
    "ping -c 4 -W 2 8.8.8.8",
    "ping -c 4 -W 2 google.com",
    "sudo -n apt-get update -qq -o APT::Color=0 -o Dpkg::Use-Pty=0 -o Acquire::Languages=none 2>&1 || true",
};

static const char *g_WslProbeCommandsTail[] = {
    "printf '\\137\\137ROSL\\137WSL2\\137PROBE\\137DONE\\137\\137\\n'"
};

/* Built at startup based on g_ProbeNetTests */
#define ROSL_WSL_PROBE_MAX_CMDS 16
static const char *g_WslProbeCommands[ROSL_WSL_PROBE_MAX_CMDS];
static DWORD g_WslProbeCmdCount = 0;

static void RoslBuildProbeCommandList(void)
{
    DWORD i;
    g_WslProbeCmdCount = 0;

    for (i = 0; i < (DWORD)(sizeof(g_WslProbeCommandsBase) / sizeof(g_WslProbeCommandsBase[0])); i++)
        g_WslProbeCommands[g_WslProbeCmdCount++] = g_WslProbeCommandsBase[i];

    if (g_ProbeNetTests)
    {
        for (i = 0; i < (DWORD)(sizeof(g_WslProbeCommandsNet) / sizeof(g_WslProbeCommandsNet[0])); i++)
            g_WslProbeCommands[g_WslProbeCmdCount++] = g_WslProbeCommandsNet[i];
    }

    for (i = 0; i < (DWORD)(sizeof(g_WslProbeCommandsTail) / sizeof(g_WslProbeCommandsTail[0])); i++)
        g_WslProbeCommands[g_WslProbeCmdCount++] = g_WslProbeCommandsTail[i];
}

#define ROSL_WSL_PROBE_CMD_COUNT g_WslProbeCmdCount

/* ---- Helpers ------------------------------------------------------------ */

static const char *StateName(ROSV_VM_STATE s)
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

static const char *CheckpointName(ROSV_CHECKPOINT cp)
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

static void Die(const char *msg)
{
    RoslLog("[FAIL] %s (GetLastError=%lu)\n", msg, GetLastError());
    if (g_hDev != INVALID_HANDLE_VALUE)
        CloseHandle(g_hDev);
    ExitProcess(1);
}

static BOOL Ioctl(DWORD code, void *in, DWORD inSz, void *out, DWORD outSz, DWORD *ret)
{
    DWORD bytes = 0;
    BOOL ok = DeviceIoControl(g_hDev, code, in, inSz, out, outSz, &bytes, NULL);
    if (ret) *ret = bytes;
    return ok;
}

static void RoslInitControlState(void)
{
    if (!g_ControlStateLockInitialized)
    {
        InitializeCriticalSection(&g_ControlStateLock);
        g_ControlStateLockInitialized = TRUE;
    }
}

static void RoslDeleteControlState(void)
{
    if (g_ControlStateLockInitialized)
    {
        DeleteCriticalSection(&g_ControlStateLock);
        g_ControlStateLockInitialized = FALSE;
    }
}

static void
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

static BOOL
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

static BOOL
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

static ULONG
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

static void
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

static BOOL
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

static BOOL
RoslServiceStartVconShell(
    _In_ ULONG PortIndex)
{
    char Command[512];
    int Length;

    /*
     * Launch a shell on /dev/vport2pN with a guest-side PTY.
     * `script -qc bash /dev/null` allocates a real pseudo-terminal inside
     * the guest, giving bash isatty()=true. This enables:
     *   - Ctrl+C (0x03) → SIGINT via the guest tty layer
     *   - Ctrl+Z (0x1A) → SIGTSTP
     *   - Ctrl+D (0x04) → EOF
     *   - readline line editing, tab completion
     *   - proper job control
     * Without this, /dev/vportNpM is a raw char device (not a tty)
     * and bash runs in degraded mode with no signal handling.
     */
    Length = _snprintf(
        Command,
        sizeof(Command),
        "(sudo -n bash -c 'setsid script -qc \"bash --login\" /dev/null <>/dev/vport2p%lu >&0 2>&1 &' "
        "|| setsid script -qc 'bash --login' /dev/null <>/dev/vport2p%lu >&0 2>&1 &) "
        "&& echo __ROSL_VCON_PORT_%lu__\n",
        PortIndex,
        PortIndex,
        PortIndex);
    if (Length <= 0 || (DWORD)Length >= sizeof(Command))
    {
        SetLastError(ERROR_BUFFER_OVERFLOW);
        return FALSE;
    }

    RoslLog("[INFO] Starting guest shell (script+pty) on /dev/vport2p%lu via serial bootstrap\n",
            PortIndex);
    return RoslConsoleSendInput(Command, (DWORD)Length);
}

/* Vcon port allocation is now handled by the driver via PTY_CREATE.
 * The driver allocates both a PTY and a bound vcon port atomically.
 * PTY_DESTROY releases the vcon port. */

static BOOL
RoslServiceWaitForSerialPrompt(void)
{
    if (g_ServiceSerialPromptSeen)
        return TRUE;

    if (g_ServicePromptEvent == NULL)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }

    if (WaitForSingleObject(g_ServicePromptEvent, ROSL_VCON_PROMPT_WAIT_MS) == WAIT_OBJECT_0)
        return TRUE;

    SetLastError(ERROR_TIMEOUT);
    return FALSE;
}

static void
RoslServiceNoteSerialPrompt(void)
{
    if (g_ServiceSerialPromptSeen)
        return;

    g_ServiceSerialPromptSeen = TRUE;
    RoslLog("[INFO] Service supervisor: serial prompt detected\n");
    if (g_ServicePromptEvent != NULL)
    {
        SetEvent(g_ServicePromptEvent);
    }
}

static void RoslResetTerminalInputMode(void)
{
    g_CursorKeysApplication = FALSE;
    g_VtSeqLen = 0;
}

static BOOL RoslIsVtFinalByte(UCHAR Byte)
{
    return (Byte >= 0x40 && Byte <= 0x7E);
}

static void RoslApplyVtSequence(const UCHAR *Seq, DWORD Length)
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

static DWORD RoslFeedTerminalOutput(UCHAR *Data, DWORD Length)
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

static DWORD RoslTranslateKeyEvent(
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

#define ROSV_SERVICE_NAME   "rosv"
#define ROSV_DRIVER_PATH    "system32\\drivers\\rosv.sys"
#define ROSV_SERVICE_STATE_TIMEOUT_MS 5000

static BOOL
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
static BOOL EnsureDriverLoaded(void)
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

static HANDLE
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

typedef struct {
    DWORD CompressedSize;       /* File size on disk */
    DWORD UncompressedSize;     /* From gzip trailer (0 if not gzip) */
    BOOL  IsGzip;
} ROSL_INITRD_INFO;

static BOOL GetInitrdInfo(const char *path, ROSL_INITRD_INFO *info)
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

/* ---- VM creation with retry --------------------------------------------- */

#define RAM_RETRY_STEP_MB   64
#define RAM_MIN_FLOOR_MB    256

static BOOL
CreateAndConfigureVm(
    _In_ ULONG ramMb,
    _In_ ULONG minRamMb,
    _Out_ ROSV_VM_CREATE_RESULT *createRes,
    _Out_ ULONG *actualRamMb,
    _Out_opt_ DWORD *lastErrorOut)
{
    ROSV_VM_CONFIG cfg;
    DWORD ret;
    ROSV_VM_CREATE_RESULT localCreateRes;
    DWORD err;
    ULONG tryMb = ramMb;
    BOOL vmCreated = FALSE;

    if (minRamMb < RAM_MIN_FLOOR_MB)
        minRamMb = RAM_MIN_FLOOR_MB;

    /* Create VM once with initial (max) RAM config */
    cfg.RamSizeMB = tryMb;
    cfg.NetBackendType = g_NetBackendType;
    RoslLog("[INFO] ROSV_IOCTL_CREATE_VM (RAM=%lu MB, net=%s)\n", tryMb,
            g_NetBackendType == 2 ? "netio" : g_NetBackendType == 1 ? "netd" : "none");
    memset(&localCreateRes, 0, sizeof(localCreateRes));
    if (!Ioctl(ROSV_IOCTL_CREATE_VM,
               &cfg, sizeof(cfg),
               &localCreateRes, sizeof(localCreateRes),
               &ret))
    {
        err = GetLastError();
        RoslLog("[FAIL] IOCTL_CREATE_VM failed (err=%lu)\n", err);
        if (lastErrorOut) *lastErrorOut = err;
        return FALSE;
    }
    vmCreated = TRUE;
    RoslLog("[OK]   VM created: VmId=%lu Status=0x%08lX\n",
            localCreateRes.VmId, (ULONG)localCreateRes.Status);

    /* Try SET_MEMORY, retrying with less RAM on failure */
    while (tryMb >= minRamMb)
    {
        cfg.RamSizeMB = tryMb;
        RoslLog("[INFO] ROSV_IOCTL_SET_MEMORY (RAM=%lu MB)\n", tryMb);
        if (Ioctl(ROSV_IOCTL_SET_MEMORY, &cfg, sizeof(cfg), NULL, 0, &ret))
        {
            RoslLog("[OK]   Memory configured: %lu MB\n", tryMb);
            if (createRes != NULL)
                *createRes = localCreateRes;
            if (actualRamMb != NULL)
                *actualRamMb = tryMb;
            if (lastErrorOut) *lastErrorOut = ERROR_SUCCESS;
            return TRUE;
        }

        err = GetLastError();
        RoslLog("[WARN] IOCTL_SET_MEMORY failed for %lu MB (err=%lu)\n", tryMb, err);

        /* Reduce and retry */
        if (tryMb <= minRamMb)
            break;

        tryMb -= RAM_RETRY_STEP_MB;
        if (tryMb < minRamMb)
            tryMb = minRamMb;

        RoslLog("[INFO] Retrying with %lu MB (min=%lu MB)\n", tryMb, minRamMb);
    }

    /* All retries exhausted */
    err = GetLastError();
    RoslLog("[FAIL] Cannot allocate VM memory (tried %lu-%lu MB)\n", ramMb, tryMb);
    if (lastErrorOut) *lastErrorOut = err;

    /* Destroy the VM we created */
    if (vmCreated)
    {
        RoslLog("[INFO] Destroying VM after memory allocation failure\n");
        Ioctl(ROSV_IOCTL_DESTROY_VM, NULL, 0, NULL, 0, &ret);
    }

    SetLastError(err);
    return FALSE;
}

static BYTE *ReadWholeFile(const char *path, DWORD *outSize)
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

static BOOL StreamInitrdFile(const char *path, DWORD *outSize)
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

static void ConsoleSetRawMode(void)
{
    DWORD rawMode;
    DWORD outMode;
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

static void ConsoleRestoreMode(void)
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

static DWORD
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

static BOOL WINAPI CtrlHandler(DWORD type)
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

/* ---- PTY interactive loop (simple polling for debug) -------------------- */

/*
 * Helper: send data to guest via PTY_WRITE IOCTL.
 * The data goes through termios input processing, then drains
 * from PTY InputBuf into UART RxFifo in the kernel driver.
 */
static BOOL PtySendInput(const void *data, DWORD len)
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
static BOOL VconSendInput(const void *data, DWORD len)
{
    return RoslWriteVconPort(g_VconPort, data, len);
}

/* Read output data from a virtio-console port (guest -> host) */
static BOOL VconReadOutput(void *buf, DWORD bufSize, DWORD *bytesRead)
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

static BOOL PtyResize(USHORT rows, USHORT cols)
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
static void ConfigurePtyLowLatencyMode(void)
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

typedef struct {
    BOOL Enabled;
    BOOL PromptSeen;
    BOOL Started;
    BOOL Done;
    BOOL WaitingForCommandPrompt;
    DWORD WaitStartTick;
    DWORD LoopStartTick;
    DWORD ProbeStartTick;
    DWORD LastSendTick;
    DWORD NextCommand;
    DWORD TailLength;
    ROSL_SEND_INPUT_ROUTINE SendInput;
    const char *ChannelName;
    char Tail[ROSL_WSL_PROBE_TAIL_MAX];
} ROSL_WSL_PROBE_STATE;

static void
RoslWslProbeInit(
    _Out_ ROSL_WSL_PROBE_STATE *Probe,
    _In_ BOOL Enable,
    _In_opt_ ROSL_SEND_INPUT_ROUTINE SendInput,
    _In_opt_z_ const char *ChannelName)
{
    memset(Probe, 0, sizeof(*Probe));
    Probe->Enabled = Enable;
    Probe->LoopStartTick = GetTickCount();
    Probe->LastSendTick = Probe->LoopStartTick;
    Probe->SendInput = SendInput;
    Probe->ChannelName = (ChannelName != NULL) ? ChannelName : "control";
}

static void RoslWslProbeAppendTail(
    _Inout_ ROSL_WSL_PROBE_STATE *Probe,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ DWORD Length)
{
    DWORD keep;

    if (Length == 0)
        return;

    if (Length >= (ROSL_WSL_PROBE_TAIL_MAX - 1))
    {
        keep = ROSL_WSL_PROBE_TAIL_MAX - 1;
        memcpy(Probe->Tail, Data + (Length - keep), keep);
        Probe->TailLength = keep;
        Probe->Tail[Probe->TailLength] = '\0';
        return;
    }

    if (Probe->TailLength + Length >= (ROSL_WSL_PROBE_TAIL_MAX - 1))
    {
        DWORD overflow = (Probe->TailLength + Length) - (ROSL_WSL_PROBE_TAIL_MAX - 1);
        memmove(Probe->Tail, Probe->Tail + overflow, Probe->TailLength - overflow);
        Probe->TailLength -= overflow;
    }

    memcpy(Probe->Tail + Probe->TailLength, Data, Length);
    Probe->TailLength += Length;
    Probe->Tail[Probe->TailLength] = '\0';
}

static BOOL RoslWslProbePromptDetected(_In_ const ROSL_WSL_PROBE_STATE *Probe)
{
    return strstr(Probe->Tail, ":~$ ") != NULL ||
           strstr(Probe->Tail, ":/$ ") != NULL ||
           strstr(Probe->Tail, ":~# ") != NULL ||
           strstr(Probe->Tail, ":/# ") != NULL ||
           strstr(Probe->Tail, "wsluser@") != NULL;
}

static BOOL RoslWslProbePromptAtTail(_In_ const ROSL_WSL_PROBE_STATE *Probe)
{
    static const char *const prompts[] = { ":~$ ", ":/$ ", ":~# ", ":/# " };
    size_t i;

    if (Probe->TailLength < 4)
        return FALSE;

    /*
     * Search the last 256 bytes of the tail for a prompt pattern.
     * The prompt may not be at the exact end due to trailing ANSI
     * escape sequences (e.g., bracket paste mode \x1b[?2004h) that
     * bash emits after the prompt.
     */
    for (i = 0; i < (sizeof(prompts) / sizeof(prompts[0])); i++)
    {
        const char *found;
        size_t searchStart = (Probe->TailLength > 256) ? (Probe->TailLength - 256) : 0;

        found = strstr(Probe->Tail + searchStart, prompts[i]);
        if (found != NULL)
        {
            return TRUE;
        }
    }

    return FALSE;
}

static void RoslWslProbeFeedOutput(
    _Inout_ ROSL_WSL_PROBE_STATE *Probe,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ DWORD Length)
{
    if (!Probe->Enabled || Probe->Done || Length == 0)
        return;

    RoslWslProbeAppendTail(Probe, Data, Length);

    if (!Probe->PromptSeen && RoslWslProbePromptDetected(Probe))
    {
        Probe->PromptSeen = TRUE;
        RoslLog("[INFO] WSL2 probe: login/prompt detected\n");
    }

    /* Check for DONE marker BEFORE clearing tail on prompt detection,
     * otherwise the prompt-detected tail clear races with DONE detection. */
    if (strstr(Probe->Tail, "__ROSL_WSL2_PROBE_DONE__") != NULL)
    {
        Probe->Done = TRUE;
        RoslLog("[OK]   WSL2 probe: command batch completed\n");
        return;
    }

    if (Probe->WaitingForCommandPrompt)
    {
        if (RoslWslProbePromptAtTail(Probe))
        {
            RoslLog("[INFO] WSL2 probe: prompt detected in tail, proceeding\n");
            Probe->WaitingForCommandPrompt = FALSE;
            Probe->TailLength = 0;
            Probe->Tail[0] = '\0';
        }
    }
}

static void RoslWslProbeTick(_Inout_ ROSL_WSL_PROBE_STATE *Probe)
{
    DWORD now;
    char line[256];
    int lineLen;

    if (!Probe->Enabled)
        return;

    now = GetTickCount();

    /*
     * After the probe batch has completed, only run the periodic heartbeat.
     * Skip the initial probe logic.
     */
    if (Probe->Done)
        goto heartbeat;

    if (!Probe->Started)
    {
        if (!Probe->PromptSeen &&
            (now - Probe->LoopStartTick) < ROSL_WSL_PROBE_PROMPT_TIMEOUT_MS)
        {
            return;
        }

        Probe->Started = TRUE;
        Probe->ProbeStartTick = now;
        Probe->LastSendTick = 0;
        Probe->NextCommand = 0;

        if (!Probe->PromptSeen)
        {
            RoslLog("[WARN] WSL2 probe: prompt not detected within %lu ms, sending commands anyway\n",
                    (unsigned long)ROSL_WSL_PROBE_PROMPT_TIMEOUT_MS);
        }

        RoslLog("[INFO] WSL2 probe: sending %lu commands via %s\n",
                (unsigned long)ROSL_WSL_PROBE_CMD_COUNT,
                Probe->ChannelName);
    }

    if (Probe->NextCommand < ROSL_WSL_PROBE_CMD_COUNT)
    {
        BOOL sent;
        BOOL waitForPrompt;

        if (Probe->WaitingForCommandPrompt)
        {
            /* Safety valve: if we've waited >5s for a prompt, force-proceed */
            if ((now - Probe->WaitStartTick) > 5000)
            {
                RoslLog("[WARN] WSL2 probe: prompt wait timeout after 5s, forcing next cmd\n");
                Probe->WaitingForCommandPrompt = FALSE;
            }
            else
            {
                return;
            }
        }

        if ((now - Probe->LastSendTick) < ROSL_WSL_PROBE_COMMAND_INTERVAL_MS)
            return;

        sent = FALSE;
        lineLen = _snprintf(line, sizeof(line), "%s\n", g_WslProbeCommands[Probe->NextCommand]);
        if (lineLen > 0 && (DWORD)lineLen < sizeof(line))
        {
            if (Probe->SendInput == NULL ||
                !Probe->SendInput(line, (DWORD)lineLen))
            {
                RoslLog("[WARN] WSL2 probe: failed to send cmd[%lu]\n",
                        (unsigned long)(Probe->NextCommand + 1));
            }
            else
            {
                sent = TRUE;
                RoslLog("[INFO] WSL2 probe cmd[%lu/%lu]: %s\n",
                        (unsigned long)(Probe->NextCommand + 1),
                        (unsigned long)ROSL_WSL_PROBE_CMD_COUNT,
                        g_WslProbeCommands[Probe->NextCommand]);
            }
        }
        else
        {
            RoslLog("[WARN] WSL2 probe: command[%lu] too long, skipping\n",
                    (unsigned long)(Probe->NextCommand + 1));
        }

        waitForPrompt = (sent &&
                         (Probe->NextCommand + 1) < ROSL_WSL_PROBE_CMD_COUNT);
        Probe->NextCommand++;
        Probe->LastSendTick = now;
        Probe->WaitingForCommandPrompt = waitForPrompt;
        if (waitForPrompt)
        {
            Probe->WaitStartTick = now;
            /*
             * Do NOT clear the tail here — the prompt from the command we just
             * sent may arrive in the same data chunk and already be in the tail
             * by the time FeedOutput runs next.  Clearing would lose it, causing
             * a deadlock where WaitingForCommandPrompt is never cleared.
             *
             * Also check if the tail already ends with a prompt (from the
             * previous command's output that FeedOutput saw before Tick ran).
             */
            if (RoslWslProbePromptAtTail(Probe))
            {
                RoslLog("[INFO] WSL2 probe: prompt already in tail, proceeding immediately\n");
                Probe->WaitingForCommandPrompt = FALSE;
            }
        }
        return;
    }

heartbeat:
    (void)0;
}

/*
 * VconInteractiveLoop - Interactive terminal loop using virtio-console port.
 *
 * This is a simplified interactive loop that polls the virtio-console port
 * for guest output and forwards host keyboard input to the guest.
 * No PTY, no probe, no WSL automation — just raw terminal I/O.
 */
static void VconInteractiveLoop(void)
{
    HANDLE hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD stdoutMode = 0;
    DWORD ret;

    RoslLog("[INFO] Entering virtio-console mode (port %lu, Ctrl+C to exit)...\n",
            g_VconPort);
    RoslResetTerminalInputMode();

    if (hStdout != INVALID_HANDLE_VALUE &&
        GetConsoleMode(hStdout, &stdoutMode))
    {
        (void)stdoutMode;
    }

    ConsoleSetRawMode();

    while (g_Running)
    {
        BOOL ioActivity = FALSE;
        BYTE readBuf[4096];
        DWORD bytesRead = 0;

        /* 1. Poll guest output via VCON_PORT_READ */
        if (VconReadOutput(readBuf, sizeof(readBuf), &bytesRead) && bytesRead > 0)
        {
            ioActivity = TRUE;
            bytesRead = RoslFeedTerminalOutput(readBuf, bytesRead);
            if (hStdout != INVALID_HANDLE_VALUE)
            {
                DWORD written;
                if (bytesRead > 0)
                    WriteFile(hStdout, readBuf, bytesRead, &written, NULL);
            }
        }

        /* 2. Check stdin for user key input (non-blocking) */
        if (g_StdinIsConsole && g_hStdin != INVALID_HANDLE_VALUE)
        {
            DWORD numEvents = 0;

            if (GetNumberOfConsoleInputEvents(g_hStdin, &numEvents) && numEvents > 0)
            {
                INPUT_RECORD records[64];
                DWORD numRead = 0;
                DWORD i;

                if (ReadConsoleInputW(g_hStdin, records, 64, &numRead))
                {
                    for (i = 0; i < numRead; i++)
                    {
                        if (records[i].EventType == KEY_EVENT &&
                            records[i].Event.KeyEvent.bKeyDown)
                        {
                            KEY_EVENT_RECORD *keyEvent = &records[i].Event.KeyEvent;
                            UCHAR seq[8];
                            DWORD seqLen;

                            seqLen = RoslTranslateKeyEvent(keyEvent, seq, sizeof(seq));
                            if (seqLen > 0 && VconSendInput(seq, seqLen))
                            {
                                ioActivity = TRUE;
                            }
                        }
                    }
                }
            }
        }
        else if (g_hStdin != INVALID_HANDLE_VALUE)
        {
            UCHAR rawInput[256];
            DWORD rawLen = ReadRedirectedStdin(rawInput, sizeof(rawInput));
            if (rawLen > 0)
            {
                VconSendInput(rawInput, rawLen);
                ioActivity = TRUE;
            }
        }

        /* 3. State polling — check VM is still alive */
        {
            ROSV_VM_STATE_INFO stateInfo;
            if (Ioctl(ROSV_IOCTL_GET_STATE, NULL, 0,
                      &stateInfo, sizeof(stateInfo), &ret))
            {
                if (stateInfo.State == RosvVmStateStopped ||
                    stateInfo.State == RosvVmStateError ||
                    stateInfo.State == RosvVmStateCrashed)
                {
                    RoslLog("[INFO] VM stopped/crashed (state=%u), exiting vcon loop\n",
                            stateInfo.State);
                    break;
                }
            }
        }

        if (!ioActivity)
            Sleep(5);
    }

    ConsoleRestoreMode();
    RoslLog("[INFO] Exited virtio-console loop\n");
}

static void PtyInteractiveLoop(void)
{
    HANDLE hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD stdoutMode = 0;
    DWORD ret;
    ROSV_VM_STATE_INFO stateInfo;
    ROSV_VM_STATE lastState = (ROSV_VM_STATE)-1;
    DWORD stateCounter = 0;
    /* PTY_READ uses: input = PTY_READ_REQUEST, output = IO_RESULT + data */
    ROSV_PTY_READ_REQUEST ptyReadReq;
    BYTE readResBuf[4096 + 8];
    ROSV_PTY_IO_RESULT *readRes = (ROSV_PTY_IO_RESULT *)readResBuf;
    DWORD stateHeartbeatCounter = 0;

    RoslLog("[INFO] Entering PTY client mode (session=%lu pty=%lu reader=%lu)\n",
            g_AttachSessionId,
            g_PtyIndex,
            g_ReaderIndex);
    RoslResetTerminalInputMode();

    if (hStdout != INVALID_HANDLE_VALUE &&
        GetConsoleMode(hStdout, &stdoutMode))
    {
        (void)stdoutMode;
    }

    ConsoleSetRawMode();

    while (g_Running)
    {
        BOOL ioActivity = FALSE;

        /* 1. Poll PTY output only. */
        memset(readResBuf, 0, sizeof(readResBuf));
        ptyReadReq.PtyIndex = g_PtyIndex;
        ptyReadReq.ReaderIndex = g_ReaderIndex;
        if (Ioctl(ROSV_IOCTL_PTY_READ, &ptyReadReq, sizeof(ptyReadReq),
                  readResBuf, sizeof(readResBuf), &ret))
        {
            if (readRes->BytesTransferred > 0)
            {
                DWORD bytesRead = readRes->BytesTransferred;

                ioActivity = TRUE;
                bytesRead = RoslFeedTerminalOutput(readRes->Data, bytesRead);
                if (hStdout != INVALID_HANDLE_VALUE)
                {
                    DWORD written;
                    if (bytesRead > 0)
                        WriteFile(hStdout, readRes->Data, bytesRead, &written, NULL);
                }
            }
        }

        /* 2. Check stdin for user key input (non-blocking) */
        if (g_StdinIsConsole && g_hStdin != INVALID_HANDLE_VALUE)
        {
            DWORD numEvents = 0;

            if (GetNumberOfConsoleInputEvents(g_hStdin, &numEvents) && numEvents > 0)
            {
                INPUT_RECORD records[64];
                DWORD numRead = 0;
                DWORD i;

                if (ReadConsoleInputW(g_hStdin, records, 64, &numRead))
                {
                    for (i = 0; i < numRead; i++)
                    {
                        if (records[i].EventType == KEY_EVENT &&
                            records[i].Event.KeyEvent.bKeyDown)
                        {
                            KEY_EVENT_RECORD *keyEvent = &records[i].Event.KeyEvent;
                            UCHAR seq[8];
                            DWORD seqLen;

                            seqLen = RoslTranslateKeyEvent(keyEvent, seq, sizeof(seq));
                            if (seqLen > 0 &&
                                PtySendInput(seq, seqLen))
                            {
                                ioActivity = TRUE;
                            }
                        }
                        else if (records[i].EventType == WINDOW_BUFFER_SIZE_EVENT)
                        {
                            COORD newSize = records[i].Event.WindowBufferSizeEvent.dwSize;

                            if (PtyResize((USHORT)newSize.Y, (USHORT)newSize.X))
                                ioActivity = TRUE;
                        }
                    }
                }
            }
        }
        else if (g_hStdin != INVALID_HANDLE_VALUE)
        {
            UCHAR rawInput[256];
            DWORD rawLen = ReadRedirectedStdin(rawInput, sizeof(rawInput));
            if (rawLen > 0)
            {
                PtySendInput(rawInput, rawLen);
                ioActivity = TRUE;
            }
        }

        /* 3. Periodically poll VM state (~every 500ms = 10 iterations at 50ms) */
        stateCounter++;
        if (stateCounter >= 10)
        {
            stateCounter = 0;
            memset(&stateInfo, 0, sizeof(stateInfo));
            if (Ioctl(ROSV_IOCTL_GET_STATE, NULL, 0, &stateInfo, sizeof(stateInfo), &ret))
            {
                if (stateInfo.State != lastState)
                {
                    lastState = stateInfo.State;
                    stateHeartbeatCounter = 0;
                }
                else
                {
                    stateHeartbeatCounter++;
                    if (stateInfo.State == RosvVmStateRunning &&
                        stateHeartbeatCounter >= 10)
                    {
                            stateHeartbeatCounter = 0;
                    }
                }

                if (stateInfo.State == RosvVmStateStopped ||
                    stateInfo.State == RosvVmStateError ||
                    stateInfo.State == RosvVmStateCrashed)
                {
                    break;
                }
            }
        }

        if (!ioActivity)
        {
            if (g_StdinIsConsole && g_hStdin != INVALID_HANDLE_VALUE)
            {
                DWORD wait = WaitForSingleObject(g_hStdin, POLL_INTERVAL_MS);
                if (wait == WAIT_FAILED)
                    Sleep(POLL_INTERVAL_MS);
            }
            else
            {
                Sleep(POLL_INTERVAL_MS);
            }
        }
        else
        {
            Sleep(0);
        }
    }

    ConsoleRestoreMode();
    RoslLog("[INFO] PTY client loop exited\n");
}

/* ---- UART interactive console loop (legacy) ----------------------------- */

static void UartInteractiveLoop(void)
{
    HANDLE hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
    BYTE readBuf[CONSOLE_BUF_SIZE];
    ROSV_VM_STATE_INFO stateInfo;
    ROSV_VM_STATE lastState = (ROSV_VM_STATE)-1;
    DWORD ret;
    DWORD stateCounter = 0;
    DWORD stateHeartbeatCounter = 0;

    RoslLog("[INFO] Entering interactive console (Ctrl+C to exit)...\n");
    RoslResetTerminalInputMode();

    ConsoleSetRawMode();

    while (g_Running)
    {
        BOOL ioActivity = FALSE;

        /* 1. Poll CONSOLE_READ for VM serial output */
        memset(readBuf, 0, sizeof(readBuf));
        if (Ioctl(ROSV_IOCTL_CONSOLE_READ, NULL, 0, readBuf, sizeof(readBuf) - 1, &ret))
        {
            if (ret > 0)
            {
                DWORD written;
                ioActivity = TRUE;
                ret = RoslFeedTerminalOutput(readBuf, ret);
                readBuf[ret] = '\0';
                if (ret > 0)
                    WriteFile(hStdout, readBuf, ret, &written, NULL);
            }
        }

        /* 2. Check stdin for user key input (non-blocking) */
        if (g_hStdin != INVALID_HANDLE_VALUE)
        {
            if (g_StdinIsConsole)
            {
                DWORD numEvents = 0;

                if (GetNumberOfConsoleInputEvents(g_hStdin, &numEvents) && numEvents > 0)
                {
                    INPUT_RECORD records[64];
                    DWORD numRead = 0;
                    DWORD i;

                    if (ReadConsoleInputW(g_hStdin, records, 64, &numRead))
                    {
                        for (i = 0; i < numRead; i++)
                        {
                            if (records[i].EventType == KEY_EVENT &&
                                records[i].Event.KeyEvent.bKeyDown)
                            {
                                UCHAR seq[8];
                                DWORD seqLen = RoslTranslateKeyEvent(&records[i].Event.KeyEvent,
                                                                     seq,
                                                                     sizeof(seq));

                                if (seqLen > 0 &&
                                    Ioctl(ROSV_IOCTL_CONSOLE_WRITE,
                                          seq,
                                          seqLen,
                                          NULL,
                                          0,
                                          &ret))
                                {
                                    ioActivity = TRUE;
                                }
                            }
                        }
                    }
                }
            }
            else
            {
                UCHAR rawInput[256];
                DWORD rawLen = ReadRedirectedStdin(rawInput, sizeof(rawInput));
                if (rawLen > 0)
                {
                    Ioctl(ROSV_IOCTL_CONSOLE_WRITE, rawInput, rawLen, NULL, 0, &ret);
                    ioActivity = TRUE;
                }
            }
        }

        /* 3. Periodically poll VM state (~every 500ms = 10 iterations at 50ms) */
        stateCounter++;
        if (stateCounter >= 10)
        {
            stateCounter = 0;
            memset(&stateInfo, 0, sizeof(stateInfo));
            if (Ioctl(ROSV_IOCTL_GET_STATE, NULL, 0, &stateInfo, sizeof(stateInfo), &ret))
            {
                if (stateInfo.State != lastState)
                {
                    /* State transition: debug serial only */
                    DbgPrint("rosl: [STATE] %s | exits=%llu last_exit=0x%lX checkpoint=%s\n",
                           StateName(stateInfo.State),
                           (unsigned long long)stateInfo.ExitCount,
                           (unsigned long)stateInfo.LastExitReason,
                           CheckpointName(stateInfo.LastCheckpoint));
                    lastState = stateInfo.State;
                    stateHeartbeatCounter = 0;
                }
                else
                {
                    stateHeartbeatCounter++;
                    if (stateInfo.State == RosvVmStateRunning &&
                        stateHeartbeatCounter >= 10)
                    {
                        stateHeartbeatCounter = 0;
                    }
                }

                if (stateInfo.State == RosvVmStateStopped ||
                    stateInfo.State == RosvVmStateError ||
                    stateInfo.State == RosvVmStateCrashed)
                {
                    RoslLog("\r\n[INFO] VM is no longer running (state=%s)\r\n",
                           StateName(stateInfo.State));
                    break;
                }
            }
        }

        /*
         * Sleep strategy:
         *  - Idle: wait on console input handle so a keypress wakes immediately.
         *  - Active: Sleep(0) yields without timer-granularity latency.
         */
        if (!ioActivity)
        {
            if (g_hStdin != INVALID_HANDLE_VALUE && g_StdinIsConsole)
            {
                DWORD wait = WaitForSingleObject(g_hStdin, POLL_INTERVAL_MS);
                if (wait == WAIT_FAILED)
                    Sleep(POLL_INTERVAL_MS);
            }
            else
            {
                Sleep(POLL_INTERVAL_MS);
            }
        }
        else
        {
            Sleep(0);
        }
    }

    ConsoleRestoreMode();
}

static void ServiceSupervisorLoop(void)
{
    HANDLE hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
    BYTE readBuf[CONSOLE_BUF_SIZE];
    ROSV_VM_STATE_INFO stateInfo;
    ROSV_VM_STATE lastState = (ROSV_VM_STATE)-1;
    ROSL_WSL_PROBE_STATE wslProbe;
    ROSV_VM_STATS vmStats;
    DWORD ret;
    DWORD stateCounter = 0;
    DWORD stateHeartbeatCounter = 0;
    DWORD lastStatsTick;
    BOOL bootCmdSent = FALSE;
    char bootCmdTail[512];
    DWORD bootCmdTailLen = 0;
    BOOL probeEnabled;
    ULONGLONG lastStatsExitCount = 0;
    ULONGLONG lastStatsTotalTicks = 0;
    ULONGLONG lastStatsHltTicks = 0;

    memset(bootCmdTail, 0, sizeof(bootCmdTail));
    probeEnabled = (g_WslProbeMode == RoslWslProbeEnabled);
    lastStatsTick = GetTickCount();

    RoslBuildProbeCommandList();
    RoslWslProbeInit(&wslProbe, probeEnabled, RoslConsoleSendInput, "serial");

    RoslLog("[INFO] Service supervisor active on serial console\n");
    if (probeEnabled)
    {
        RoslLog("[INFO] WSL2 probe: enabled (serial)\n");
    }

    while (g_Running)
    {
        BOOL ioActivity = FALSE;
        DWORD nowTick;

        memset(readBuf, 0, sizeof(readBuf));
        if (Ioctl(ROSV_IOCTL_CONSOLE_READ, NULL, 0, readBuf, sizeof(readBuf) - 1, &ret))
        {
            if (ret > 0)
            {
                DWORD copyLen;

                ioActivity = TRUE;
                copyLen = RoslFeedTerminalOutput(readBuf, ret);
                readBuf[copyLen] = '\0';
                if (hStdout != INVALID_HANDLE_VALUE)
                {
                    DWORD written;
                    if (copyLen > 0)
                        WriteFile(hStdout, readBuf, copyLen, &written, NULL);
                }

                if (copyLen > 0)
                    RoslWslProbeFeedOutput(&wslProbe, readBuf, copyLen);

                /* Keep a sliding window of the last sizeof(bootCmdTail)-1 bytes
                 * of serial output so we can detect shell prompts. */
                {
                    const BYTE *src = readBuf;
                    DWORD srcLen = copyLen;
                    DWORD maxTail = sizeof(bootCmdTail) - 1;

                    if (srcLen >= maxTail)
                    {
                        /* Incoming chunk alone fills the window — just take the tail */
                        memcpy(bootCmdTail, src + srcLen - maxTail, maxTail);
                        bootCmdTailLen = maxTail;
                    }
                    else if (bootCmdTailLen + srcLen > maxTail)
                    {
                        /* Shift existing data to make room */
                        DWORD keep = maxTail - srcLen;
                        memmove(bootCmdTail, bootCmdTail + bootCmdTailLen - keep, keep);
                        memcpy(bootCmdTail + keep, src, srcLen);
                        bootCmdTailLen = maxTail;
                    }
                    else
                    {
                        memcpy(bootCmdTail + bootCmdTailLen, src, srcLen);
                        bootCmdTailLen += srcLen;
                    }
                    bootCmdTail[bootCmdTailLen] = '\0';
                }

                if (!g_ServiceSerialPromptSeen &&
                    (strstr(bootCmdTail, ":~$ ") != NULL ||
                     strstr(bootCmdTail, ":/$ ") != NULL ||
                     strstr(bootCmdTail, ":~# ") != NULL ||
                     strstr(bootCmdTail, ":/# ") != NULL ||
                     strstr(bootCmdTail, "# ") != NULL))
                {
                    RoslServiceNoteSerialPrompt();
                }

                if (g_BootCmd != NULL && !bootCmdSent && g_ServiceSerialPromptSeen)
                {
                    char cmdBuf[512];
                    int cmdLen = _snprintf(cmdBuf, sizeof(cmdBuf), "%s\n", g_BootCmd);
                    if (cmdLen > 0 && (DWORD)cmdLen < sizeof(cmdBuf))
                    {
                        RoslLog("[INFO] Boot command: injecting '%s' via serial\n", g_BootCmd);
                        RoslConsoleSendInput(cmdBuf, (DWORD)cmdLen);
                    }
                    bootCmdSent = TRUE;
                }
            }
        }

        RoslWslProbeTick(&wslProbe);

        nowTick = GetTickCount();
        if ((nowTick - lastStatsTick) >= 1000)
        {
            lastStatsTick = nowTick;
            if (RoslQueryVmStats(&vmStats))
            {
                RoslLogVmCpuUsage("service",
                                  &vmStats,
                                  &lastStatsExitCount,
                                  &lastStatsTotalTicks,
                                  &lastStatsHltTicks);
            }
        }

        stateCounter++;
        if (stateCounter >= 10)
        {
            stateCounter = 0;
            memset(&stateInfo, 0, sizeof(stateInfo));
            if (Ioctl(ROSV_IOCTL_GET_STATE, NULL, 0, &stateInfo, sizeof(stateInfo), &ret))
            {
                if (stateInfo.State != lastState)
                {
                    RoslLog("[STATE] %s | exits=%llu last_exit=0x%lX checkpoint=%s\n",
                            StateName(stateInfo.State),
                            (unsigned long long)stateInfo.ExitCount,
                            (unsigned long)stateInfo.LastExitReason,
                            CheckpointName(stateInfo.LastCheckpoint));
                    lastState = stateInfo.State;
                    stateHeartbeatCounter = 0;
                }
                else
                {
                    stateHeartbeatCounter++;
                    if (stateInfo.State == RosvVmStateRunning &&
                        stateHeartbeatCounter >= 10)
                    {
                        stateHeartbeatCounter = 0;
                    }
                }

                if (stateInfo.State == RosvVmStateStopped ||
                    stateInfo.State == RosvVmStateError ||
                    stateInfo.State == RosvVmStateCrashed)
                {
                    RoslLog("[INFO] Service supervisor exiting because VM state=%s\n",
                            StateName(stateInfo.State));
                    break;
                }
            }
        }

        if (!ioActivity)
            Sleep(POLL_INTERVAL_MS);
        else
            Sleep(0);
    }
}

static BOOL
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

static BOOL
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

static DWORD WINAPI
RoslControlClientThread(
    _In_ LPVOID Parameter)
{
    HANDLE Pipe = (HANDLE)Parameter;
    ROSL_CONTROL_REQUEST Request;
    ROSL_CONTROL_RESPONSE Response;
    ULONG AllocatedPtyIndex = (ULONG)-1;
    ULONG AllocatedVconPort = (ULONG)-1;
    BOOL ClientAttached = FALSE;

    memset(&Response, 0, sizeof(Response));
    Response.Version = ROSL_CONTROL_VERSION;
    Response.SessionId = g_ServiceSessionId;

    if (!RoslPipeReadExact(Pipe, &Request, sizeof(Request)))
        goto cleanup;

    if (Request.Version != ROSL_CONTROL_VERSION ||
        Request.SessionId != g_ServiceSessionId ||
        Request.Op != RoslCtlOpAllocateInteractive)
    {
        RoslLog("[FAIL] Control client: bad request (ver=%lu sess=%lu op=%lu)\n",
                Request.Version, Request.SessionId, Request.Op);
        Response.Status = ERROR_INVALID_PARAMETER;
        RoslPipeWriteExact(Pipe, &Response, sizeof(Response));
        goto cleanup;
    }

    /* Create a PTY via IOCTL — driver allocates PTY + binds a vcon port */
    {
        ROSV_PTY_CREATE_REQUEST PtyReq;
        ROSV_PTY_CREATE_RESULT PtyRes;
        DWORD Ret;

        memset(&PtyReq, 0, sizeof(PtyReq));
        PtyReq.InitialRows = Request.Rows;
        PtyReq.InitialCols = Request.Cols;
        PtyReq.Flags = 0;

        memset(&PtyRes, 0, sizeof(PtyRes));

        if (!Ioctl(ROSV_IOCTL_PTY_CREATE, &PtyReq, sizeof(PtyReq),
                   &PtyRes, sizeof(PtyRes), &Ret) ||
            PtyRes.Status != 0)
        {
            RoslLog("[FAIL] Control client: PTY_CREATE failed (Status=0x%08lX)\n",
                    (ULONG)PtyRes.Status);
            Response.Status = ERROR_GEN_FAILURE;
            RoslPipeWriteExact(Pipe, &Response, sizeof(Response));
            goto cleanup;
        }

        AllocatedPtyIndex = PtyRes.PtyIndex;
        AllocatedVconPort = PtyRes.VconPort;
        ClientAttached = TRUE;

        RoslLog("[OK]   PTY_CREATE: pty=%lu reader=%lu vcon_port=%lu\n",
                PtyRes.PtyIndex, PtyRes.ReaderIndex, PtyRes.VconPort);
    }

    /* Wait for guest serial prompt before launching shell */
    if (!RoslServiceWaitForSerialPrompt())
    {
        RoslLog("[FAIL] Control client: timed out waiting for serial prompt\n");
        Response.Status = GetLastError();
        RoslPipeWriteExact(Pipe, &Response, sizeof(Response));
        goto cleanup;
    }

    /* Start a guest shell on the vcon port bound to this PTY */
    EnterCriticalSection(&g_ControlStateLock);
    if (!RoslServiceStartVconShell(AllocatedVconPort))
    {
        RoslLog("[FAIL] Control client: failed to start shell on vcon port %lu\n",
                AllocatedVconPort);
        Response.Status = GetLastError();
        if (Response.Status == ERROR_SUCCESS)
            Response.Status = ERROR_GEN_FAILURE;
        LeaveCriticalSection(&g_ControlStateLock);
        RoslPipeWriteExact(Pipe, &Response, sizeof(Response));
        goto cleanup;
    }
    LeaveCriticalSection(&g_ControlStateLock);

    /* Return PTY credentials to client (NOT vcon — client uses PTY IOCTLs) */
    Response.Status = ERROR_SUCCESS;
    Response.PtyIndex = AllocatedPtyIndex;
    Response.ReaderIndex = 0;
    Response.Flags = 0;  /* NO ROSL_CONTROL_FLAG_USE_VCON — client uses PTY path */
    if (!RoslPipeWriteExact(Pipe, &Response, sizeof(Response)))
        goto cleanup;

    RoslLog("[INFO] Control plane: client attached (pty=%lu vcon=%lu session=%lu)\n",
            AllocatedPtyIndex, AllocatedVconPort, g_ServiceSessionId);

    /* Hold connection open until client releases or disconnects */
    while (g_Running && RoslPipeReadExact(Pipe, &Request, sizeof(Request)))
    {
        memset(&Response, 0, sizeof(Response));
        Response.Version = ROSL_CONTROL_VERSION;
        Response.SessionId = g_ServiceSessionId;
        Response.PtyIndex = AllocatedPtyIndex;
        Response.Flags = 0;

        if (Request.Version != ROSL_CONTROL_VERSION ||
            Request.SessionId != g_ServiceSessionId ||
            Request.PtyIndex != AllocatedPtyIndex)
        {
            Response.Status = ERROR_INVALID_PARAMETER;
            RoslPipeWriteExact(Pipe, &Response, sizeof(Response));
            continue;
        }

        if (Request.Op == RoslCtlOpReleaseInteractive)
        {
            Response.Status = ERROR_SUCCESS;
            RoslPipeWriteExact(Pipe, &Response, sizeof(Response));
            goto cleanup;
        }

        Response.Status = ERROR_INVALID_FUNCTION;
        RoslPipeWriteExact(Pipe, &Response, sizeof(Response));
    }

cleanup:
    /* Destroy the PTY (driver releases the bound vcon port internally) */
    if (ClientAttached && AllocatedPtyIndex != (ULONG)-1)
    {
        DWORD Ret;
        RoslLog("[INFO] Control plane: destroying PTY %lu (vcon=%lu)\n",
                AllocatedPtyIndex, AllocatedVconPort);
        Ioctl(ROSV_IOCTL_PTY_DESTROY, &AllocatedPtyIndex,
              sizeof(AllocatedPtyIndex), NULL, 0, &Ret);
    }

    FlushFileBuffers(Pipe);
    DisconnectNamedPipe(Pipe);
    CloseHandle(Pipe);
    return 0;
}

static DWORD WINAPI
RoslControlListenerThread(
    _In_ LPVOID Parameter)
{
    UNREFERENCED_PARAMETER(Parameter);

    while (WaitForSingleObject(g_ControlStopEvent, 0) == WAIT_TIMEOUT)
    {
        HANDLE Pipe;
        HANDLE ThreadHandle;
        BOOL Connected;
        DWORD Error;

        Pipe = CreateNamedPipeA(g_ControlPipeName,
                                PIPE_ACCESS_DUPLEX,
                                PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
                                PIPE_UNLIMITED_INSTANCES,
                                sizeof(ROSL_CONTROL_RESPONSE),
                                sizeof(ROSL_CONTROL_REQUEST),
                                1000,
                                NULL);
        if (Pipe == INVALID_HANDLE_VALUE)
        {
            RoslLog("[FAIL] CreateNamedPipe failed (err=%lu)\n", GetLastError());
            return 1;
        }

        Connected = ConnectNamedPipe(Pipe, NULL);
        if (!Connected)
        {
            Error = GetLastError();
            if (Error != ERROR_PIPE_CONNECTED)
            {
                CloseHandle(Pipe);
                if (WaitForSingleObject(g_ControlStopEvent, 0) == WAIT_OBJECT_0)
                    break;
                continue;
            }
        }

        ThreadHandle = CreateThread(NULL,
                                    0,
                                    RoslControlClientThread,
                                    Pipe,
                                    0,
                                    NULL);
        if (ThreadHandle == NULL)
        {
            RoslLog("[WARN] CreateThread(control client) failed (err=%lu)\n", GetLastError());
            CloseHandle(Pipe);
            continue;
        }

        CloseHandle(ThreadHandle);
    }

    return 0;
}

static void
RoslWakeControlListener(void)
{
    HANDLE Pipe;

    if (g_ControlPipeName[0] == '\0')
        return;

    Pipe = CreateFileA(g_ControlPipeName,
                       GENERIC_READ | GENERIC_WRITE,
                       0,
                       NULL,
                       OPEN_EXISTING,
                       0,
                       NULL);
    if (Pipe != INVALID_HANDLE_VALUE)
        CloseHandle(Pipe);
}

static BOOL
RoslStartControlServer(
    _In_ ULONG SessionId)
{
    RoslInitControlState();
    g_ServiceSessionId = SessionId;

    g_ServiceSerialPromptSeen = FALSE;
    RoslBuildControlPipeName(SessionId, g_ControlPipeName, sizeof(g_ControlPipeName));

    g_ControlStopEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (g_ControlStopEvent == NULL)
    {
        RoslLog("[FAIL] CreateEvent(control stop) failed (err=%lu)\n", GetLastError());
        return FALSE;
    }

    g_ControlListenerThread = CreateThread(NULL,
                                           0,
                                           RoslControlListenerThread,
                                           NULL,
                                           0,
                                           NULL);
    if (g_ControlListenerThread == NULL)
    {
        RoslLog("[FAIL] CreateThread(control listener) failed (err=%lu)\n", GetLastError());
        CloseHandle(g_ControlStopEvent);
        g_ControlStopEvent = NULL;
        return FALSE;
    }

    g_ServicePromptEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (g_ServicePromptEvent == NULL)
    {
        RoslLog("[FAIL] CreateEvent(service prompt) failed (err=%lu)\n", GetLastError());
        CloseHandle(g_ControlListenerThread);
        g_ControlListenerThread = NULL;
        CloseHandle(g_ControlStopEvent);
        g_ControlStopEvent = NULL;
        return FALSE;
    }

    if (!RoslPublishServiceDiscovery(SessionId))
    {
        RoslLog("[FAIL] Failed to publish service discovery (err=%lu)\n", GetLastError());
        CloseHandle(g_ControlListenerThread);
        g_ControlListenerThread = NULL;
        CloseHandle(g_ServicePromptEvent);
        g_ServicePromptEvent = NULL;
        CloseHandle(g_ControlStopEvent);
        g_ControlStopEvent = NULL;
        return FALSE;
    }

    RoslLog("[INFO] Control plane listening on %s\n", g_ControlPipeName);
    return TRUE;
}

static void
RoslStopControlServer(void)
{
    if (g_ControlStopEvent != NULL)
        SetEvent(g_ControlStopEvent);

    RoslWakeControlListener();

    if (g_ControlListenerThread != NULL)
    {
        WaitForSingleObject(g_ControlListenerThread, 5000);
        CloseHandle(g_ControlListenerThread);
        g_ControlListenerThread = NULL;
    }

    if (g_ControlStopEvent != NULL)
    {
        CloseHandle(g_ControlStopEvent);
        g_ControlStopEvent = NULL;
    }

    if (g_ServicePromptEvent != NULL)
    {
        CloseHandle(g_ServicePromptEvent);
        g_ServicePromptEvent = NULL;
    }


    g_ServiceSerialPromptSeen = FALSE;

    RoslUnpublishServiceDiscovery();
    g_ControlPipeName[0] = '\0';
}

static HANDLE
RoslClientConnectControl(
    _In_ ULONG SessionId)
{
    HANDLE Pipe;
    DWORD Mode = PIPE_READMODE_MESSAGE;
    char PipeName[128];

    RoslBuildControlPipeName(SessionId, PipeName, sizeof(PipeName));

    if (!WaitNamedPipeA(PipeName, 5000))
        return INVALID_HANDLE_VALUE;

    Pipe = CreateFileA(PipeName,
                       GENERIC_READ | GENERIC_WRITE,
                       0,
                       NULL,
                       OPEN_EXISTING,
                       0,
                       NULL);
    if (Pipe == INVALID_HANDLE_VALUE)
        return INVALID_HANDLE_VALUE;

    SetNamedPipeHandleState(Pipe, &Mode, NULL, NULL);
    return Pipe;
}

static BOOL
RoslClientAllocatePty(
    _In_ HANDLE Pipe,
    _In_ ULONG SessionId,
    _In_ USHORT Rows,
    _In_ USHORT Cols,
    _Out_ PULONG PtyIndex,
    _Out_ PULONG ReaderIndex,
    _Out_opt_ PULONG Flags)
{
    ROSL_CONTROL_REQUEST Request;
    ROSL_CONTROL_RESPONSE Response;

    if (PtyIndex == NULL || ReaderIndex == NULL)
        return FALSE;

    memset(&Request, 0, sizeof(Request));
    memset(&Response, 0, sizeof(Response));
    Request.Version = ROSL_CONTROL_VERSION;
    Request.Op = RoslCtlOpAllocateInteractive;
    Request.SessionId = SessionId;
    Request.Rows = Rows;
    Request.Cols = Cols;

    if (!RoslPipeWriteExact(Pipe, &Request, sizeof(Request)) ||
        !RoslPipeReadExact(Pipe, &Response, sizeof(Response)))
    {
        return FALSE;
    }

    if (Response.Status != ERROR_SUCCESS)
    {
        SetLastError((DWORD)Response.Status);
        return FALSE;
    }

    *PtyIndex = Response.PtyIndex;
    *ReaderIndex = Response.ReaderIndex;
    if (Flags != NULL)
        *Flags = Response.Flags;
    return TRUE;
}

static BOOL
RoslPublishServiceDiscovery(
    _In_ ULONG SessionId)
{
    g_DiscoveryMapping = CreateFileMappingA(INVALID_HANDLE_VALUE,
                                            NULL,
                                            PAGE_READWRITE,
                                            0,
                                            sizeof(ROSL_DISCOVERY_SHARED),
                                            ROSL_DISCOVERY_MAPPING_NAME);
    if (g_DiscoveryMapping == NULL)
        return FALSE;

    g_DiscoveryView = (ROSL_DISCOVERY_SHARED *)MapViewOfFile(g_DiscoveryMapping,
                                                             FILE_MAP_ALL_ACCESS,
                                                             0,
                                                             0,
                                                             sizeof(ROSL_DISCOVERY_SHARED));
    if (g_DiscoveryView == NULL)
    {
        CloseHandle(g_DiscoveryMapping);
        g_DiscoveryMapping = NULL;
        return FALSE;
    }

    memset(g_DiscoveryView, 0, sizeof(*g_DiscoveryView));
    g_DiscoveryView->Version = ROSL_CONTROL_VERSION;
    g_DiscoveryView->SessionId = SessionId;
    g_DiscoveryView->ServicePid = GetCurrentProcessId();
    g_DiscoveryView->Flags = 1;
    FlushViewOfFile(g_DiscoveryView, sizeof(*g_DiscoveryView));
    return TRUE;
}

static void
RoslUnpublishServiceDiscovery(void)
{
    if (g_DiscoveryView != NULL)
    {
        memset(g_DiscoveryView, 0, sizeof(*g_DiscoveryView));
        FlushViewOfFile(g_DiscoveryView, sizeof(*g_DiscoveryView));
        UnmapViewOfFile(g_DiscoveryView);
        g_DiscoveryView = NULL;
    }

    if (g_DiscoveryMapping != NULL)
    {
        CloseHandle(g_DiscoveryMapping);
        g_DiscoveryMapping = NULL;
    }
}

static BOOL
RoslDiscoverServiceSession(
    _Out_ PULONG SessionId)
{
    HANDLE Mapping;
    ROSL_DISCOVERY_SHARED *View;
    BOOL Found = FALSE;

    if (SessionId == NULL)
        return FALSE;

    *SessionId = 0;

    Mapping = OpenFileMappingA(FILE_MAP_READ, FALSE, ROSL_DISCOVERY_MAPPING_NAME);
    if (Mapping == NULL)
        return FALSE;

    View = (ROSL_DISCOVERY_SHARED *)MapViewOfFile(Mapping,
                                                  FILE_MAP_READ,
                                                  0,
                                                  0,
                                                  sizeof(ROSL_DISCOVERY_SHARED));
    if (View != NULL)
    {
        if (View->Version == ROSL_CONTROL_VERSION &&
            View->SessionId != 0 &&
            (View->Flags & 1) != 0)
        {
            *SessionId = View->SessionId;
            Found = TRUE;
        }

        UnmapViewOfFile(View);
    }

    CloseHandle(Mapping);
    return Found;
}

static void
RoslClientReleasePty(
    _In_ HANDLE Pipe,
    _In_ ULONG SessionId,
    _In_ ULONG PtyIndex,
    _In_ ULONG ReaderIndex)
{
    ROSL_CONTROL_REQUEST Request;
    ROSL_CONTROL_RESPONSE Response;

    memset(&Request, 0, sizeof(Request));
    memset(&Response, 0, sizeof(Response));
    Request.Version = ROSL_CONTROL_VERSION;
    Request.Op = RoslCtlOpReleaseInteractive;
    Request.SessionId = SessionId;
    Request.PtyIndex = PtyIndex;
    Request.ReaderIndex = ReaderIndex;

    RoslPipeWriteExact(Pipe, &Request, sizeof(Request));
    RoslPipeReadExact(Pipe, &Response, sizeof(Response));
}

/* ---- Bundled image path resolution -------------------------------------- */

static BOOL GetExeDir(char *buf, DWORD bufSize)
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

/*
 * Resolve kernel path for disk-backed boots.
 *
 * Preferred order:
 *   1) Bundled kernel path (e.g. X:\reactos\system32\rosv\vmlinuz)
 *   2) Installed alongside VHDX: <disk_drive>:\reactos\system32\rosv\vmlinuz
 *      (e.g. C:\reactos\system32\rosv\vmlinuz when C:\...\ubuntu24.vhdx found)
 *   3) Root-level fallback: <disk_drive>:\vmlinuz
 */
static const char *ResolveDiskBootKernelPath(const char *defaultKernelPath,
                                             const char *diskImagePath,
                                             char *fallbackBuf,
                                             DWORD fallbackBufSize)
{
    if (defaultKernelPath != NULL &&
        GetFileAttributesA(defaultKernelPath) != INVALID_FILE_ATTRIBUTES)
    {
        return defaultKernelPath;
    }

    if (fallbackBuf != NULL && fallbackBufSize > 0 &&
        diskImagePath != NULL &&
        diskImagePath[0] >= 'A' && diskImagePath[0] <= 'Z' &&
        diskImagePath[1] == ':')
    {
        /* Probe 2: standard ReactOS installation path on the data drive */
        _snprintf(fallbackBuf, fallbackBufSize,
                  "%c:\\reactos\\system32\\rosv\\vmlinuz", diskImagePath[0]);
        fallbackBuf[fallbackBufSize - 1] = '\0';
        if (GetFileAttributesA(fallbackBuf) != INVALID_FILE_ATTRIBUTES)
            return fallbackBuf;

        /* Probe 3: root-level vmlinuz on the data drive */
        _snprintf(fallbackBuf, fallbackBufSize, "%c:\\vmlinuz", diskImagePath[0]);
        fallbackBuf[fallbackBufSize - 1] = '\0';
        if (GetFileAttributesA(fallbackBuf) != INVALID_FILE_ATTRIBUTES)
            return fallbackBuf;
    }

    return defaultKernelPath;
}

/* ---- Usage -------------------------------------------------------------- */

static void PrintUsage(void)
{
    RoslLog("Usage: rosl.exe [options] [bzImage] [initrd] [ram_mb] [cmdline]\n");
    RoslLog("       rosl.exe --disk <image> [options] [bzImage] [ram_mb] [cmdline]\n");
    RoslLog("       rosl.exe --attach <session-id>\n");
    RoslLog("\nOptions:\n");
    RoslLog("  --service         Start the ROSL supervisor service (default)\n");
    RoslLog("  --attach <id>     Connect as an interactive client to service session <id>\n");
    RoslLog("  --pty             Client terminal uses PTY data plane (default for --attach)\n");
    RoslLog("  --uart            Legacy direct UART mode (service supervision only)\n");
    RoslLog("  --disk <path>     Use disk image as virtio-blk root (raw .img or .vhdx)\n");
    RoslLog("  --rows <N>        Initial terminal rows (default: auto-detect)\n");
    RoslLog("  --cols <N>        Initial terminal columns (default: auto-detect)\n");
    RoslLog("  --probe-wsl2      Force-enable ROSL PTY command probe batch\n");
    RoslLog("  --no-probe-wsl2   Force-disable ROSL PTY command probe batch\n");
    RoslLog("  --net <mode>      Network backend: netio (default), netd, none\n");
    RoslLog("\nWith no arguments, uses bundled images from <exedir>\\rosv\\.\n");
    RoslLog("If a supervisor is already running, a plain `rosl.exe` auto-attaches as a client.\n");
    RoslLog("With --disk, initrd is optional. RAM defaults to %u MB (vs %u MB for initramfs).\n",
            DEFAULT_DISK_RAM_MB, DEFAULT_RAM_MB);
    RoslLog("Service mode owns serial supervision; later rosl runs auto-attach and get fresh PTYs.\n");
}

/* ---- Argument parsing --------------------------------------------------- */

/*
 * Parse --options from argv, stripping them from the positional argument list.
 * Returns the number of remaining positional arguments in posArgs[].
 */
static int ParseOptions(int argc, char *argv[], char *posArgs[], int maxPos)
{
    int i, posCount = 0;

    for (i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--pty") == 0)
        {
            g_IoMode = RoslModePty;
        }
        else if (strcmp(argv[i], "--uart") == 0)
        {
            g_IoMode = RoslModeUart;
        }
        else if (strcmp(argv[i], "--attach") == 0)
        {
            if (i + 1 >= argc)
            {
                RoslLog("[FAIL] --attach requires a session ID\n");
                return -1;
            }
            g_AttachSessionId = (ULONG)atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--service") == 0)
        {
            g_ForceService = TRUE;
        }
        else if (strcmp(argv[i], "--rows") == 0)
        {
            if (i + 1 >= argc)
            {
                RoslLog("[FAIL] --rows requires a value\n");
                return -1;
            }
            g_InitialRows = (USHORT)atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--cols") == 0)
        {
            if (i + 1 >= argc)
            {
                RoslLog("[FAIL] --cols requires a value\n");
                return -1;
            }
            g_InitialCols = (USHORT)atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--disk") == 0)
        {
            if (i + 1 >= argc)
            {
                RoslLog("[FAIL] --disk requires a file path\n");
                return -1;
            }
            g_DiskImagePath = argv[++i];
        }
        else if (strcmp(argv[i], "--cmd") == 0)
        {
            if (i + 1 >= argc)
            {
                RoslLog("[FAIL] --cmd requires a command string\n");
                return -1;
            }
            g_BootCmd = argv[++i];
        }
        else if (strcmp(argv[i], "--probe-wsl2") == 0)
        {
            g_WslProbeMode = RoslWslProbeEnabled;
        }
        else if (strcmp(argv[i], "--no-probe-wsl2") == 0)
        {
            g_WslProbeMode = RoslWslProbeDisabled;
        }
        else if (strcmp(argv[i], "--net") == 0)
        {
            if (i + 1 >= argc)
            {
                RoslLog("[FAIL] --net requires: netio|netd|none\n");
                return -1;
            }
            i++;
            if (strcmp(argv[i], "netio") == 0)
                g_NetBackendType = 2;
            else if (strcmp(argv[i], "netd") == 0)
                g_NetBackendType = 1;
            else if (strcmp(argv[i], "none") == 0)
                g_NetBackendType = 0;
            else
            {
                RoslLog("[FAIL] --net: unknown backend '%s' (use netio|netd|none)\n", argv[i]);
                return -1;
            }
        }
        else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0)
        {
            PrintUsage();
            return -1;
        }
        else if (argv[i][0] == '-' && argv[i][1] == '-')
        {
            RoslLog("[FAIL] Unknown option: %s\n", argv[i]);
            return -1;
        }
        else
        {
            /* Positional argument */
            if (posCount < maxPos)
                posArgs[posCount] = argv[i];
            posCount++;
        }
    }

    return posCount;
}

static int
RoslRunInteractiveClient(
    _In_opt_ HANDLE hMutex,
    _In_ BOOL DeviceAlreadyOpen)
{
    HANDLE ControlPipe;
    USHORT DetectedRows = 0;
    USHORT DetectedCols = 0;
    ULONG ControlFlags = 0;

    SetConsoleCtrlHandler(CtrlHandler, TRUE);

    if (g_IoMode == RoslModeUart)
    {
        RoslLog("[FAIL] Client mode requires PTY; the service owns the serial path\n");
        if (g_hDev != INVALID_HANDLE_VALUE)
        {
            CloseHandle(g_hDev);
            g_hDev = INVALID_HANDLE_VALUE;
        }
        if (hMutex) CloseHandle(hMutex);
        return 2;
    }

    if (!DeviceAlreadyOpen)
    {
        if (!EnsureDriverLoaded())
            Die("Failed to load rosv.sys driver");

        RoslLog("[INFO] Opening \\\\.\\RosvHypervisor\n");
        g_hDev = OpenRosvDeviceWithRetry(50, 100);
        if (g_hDev == INVALID_HANDLE_VALUE)
            Die("Cannot open \\\\.\\RosvHypervisor - is rosv.sys loaded?");
        RoslLog("[OK]   Device opened\n");
    }

    if (g_InitialRows == 0 || g_InitialCols == 0)
    {
        TerminalGetSize(&DetectedRows, &DetectedCols);
        if (g_InitialRows == 0)
            g_InitialRows = DetectedRows;
        if (g_InitialCols == 0)
            g_InitialCols = DetectedCols;
    }
    if (g_InitialRows == 0)
        g_InitialRows = 24;
    if (g_InitialCols == 0)
        g_InitialCols = 80;

    RoslLog("[INFO] Connecting to service session %lu\n", g_AttachSessionId);
    ControlPipe = RoslClientConnectControl(g_AttachSessionId);
    if (ControlPipe == INVALID_HANDLE_VALUE)
        Die("Failed to connect to ROSL control plane");

    if (!RoslClientAllocatePty(ControlPipe,
                               g_AttachSessionId,
                               g_InitialRows,
                               g_InitialCols,
                               &g_PtyIndex,
                               &g_ReaderIndex,
                               &ControlFlags))
    {
        CloseHandle(ControlPipe);
        Die("Service refused interactive PTY allocation");
    }

    RoslLog("[OK]   Service allocated PTY %lu reader %lu (%ux%u)\n",
            g_PtyIndex, g_ReaderIndex, g_InitialCols, g_InitialRows);

    g_IoMode = RoslModePty;
    ConfigurePtyLowLatencyMode();
    PtyInteractiveLoop();

    RoslClientReleasePty(ControlPipe, g_AttachSessionId, g_PtyIndex, g_ReaderIndex);
    CloseHandle(ControlPipe);

    if (g_hDev != INVALID_HANDLE_VALUE)
    {
        CloseHandle(g_hDev);
        g_hDev = INVALID_HANDLE_VALUE;
    }

    if (hMutex) CloseHandle(hMutex);
    RoslLog("[DONE] Detached.\n");
    return 0;
}

/* ---- Main --------------------------------------------------------------- */

int main(int argc, char *argv[])
{
    ROSV_VM_CREATE_RESULT tmpCreateRes;
    DWORD ret;
    BYTE *kernelBuf = NULL;
    DWORD kernelSz = 0, initrdSz = 0;
    ULONG ramMB;
    ULONG minRamMB;
    ULONG actualRamMB = 0;
    DWORD vmConfigErr = ERROR_SUCCESS;
    const char *cmdline;
    const char *kernelPath;
    const char *initrdPath;
    char exeDir[MAX_PATH];
    char defaultKernel[MAX_PATH];
    char defaultInitrd[MAX_PATH];
    HANDLE hMutex;
    char *posArgs[8];
    int posCount;
    ROSL_INITRD_INFO initrdInfo;

    RoslLog("=== rosl - ReactOS VMX Hypervisor Launcher ===\n\n");

    if (0)
    {
        VconInteractiveLoop();
        UartInteractiveLoop();
    }

    /* Multiple rosl instances can connect to the same running VM.
     * No single-instance enforcement needed. */
    hMutex = NULL;

    /* ---- Parse options -------------------------------------------------- */
    memset(posArgs, 0, sizeof(posArgs));
    posCount = ParseOptions(argc, argv, posArgs, 8);
    if (posCount < 0)
    {
        if (hMutex) CloseHandle(hMutex);
        return 1;
    }

    /* Reserved command names fail closed until implemented. */
    if (posCount >= 1 &&
        (strcmp(posArgs[0], "download") == 0 ||
         strcmp(posArgs[0], "manage") == 0))
    {
        RoslLog("[FAIL] Subcommand '%s' is unavailable in this build.\n", posArgs[0]);
        PrintUsage();
        if (hMutex) CloseHandle(hMutex);
        return 2;
    }

    /* ---- Handle --attach mode ------------------------------------------- */
    if (g_AttachSessionId != (ULONG)-1)
        return RoslRunInteractiveClient(hMutex, FALSE);

    /*
     * Auto-client fast path: if a supervisor is already running, become a
     * client before doing any image/path resolution work.
     */
    if (!g_ForceService)
    {
        ROSV_VM_STATE_INFO ExistingState;
        ULONG AutoSessionId = 0;

        if (!EnsureDriverLoaded())
            Die("Failed to load rosv.sys driver");

        g_hDev = OpenRosvDeviceWithRetry(50, 100);
        if (g_hDev == INVALID_HANDLE_VALUE)
            Die("Cannot open \\\\.\\RosvHypervisor - is rosv.sys loaded?");

        memset(&ExistingState, 0, sizeof(ExistingState));
        if (Ioctl(ROSV_IOCTL_GET_STATE, NULL, 0, &ExistingState, sizeof(ExistingState), &ret) &&
            ExistingState.State == RosvVmStateRunning)
        {
            DWORD WaitDeadline = GetTickCount() + 5000;

            do
            {
                if (RoslDiscoverServiceSession(&AutoSessionId))
                    break;

                Sleep(100);
            } while (GetTickCount() < WaitDeadline);

            if (AutoSessionId != 0)
            {
                g_AttachSessionId = AutoSessionId;
                RoslLog("[INFO] Detected active supervisor session %lu; switching to client mode\n",
                        g_AttachSessionId);
                return RoslRunInteractiveClient(hMutex, TRUE);
            }
        }

        CloseHandle(g_hDev);
        g_hDev = INVALID_HANDLE_VALUE;
    }

    /* ---- Resolve kernel/initrd paths ------------------------------------ */
    if (g_DiskImagePath != NULL)
    {
        /* Disk mode: initrd is optional.
         * posArgs[0] = bzImage (optional), posArgs[1] = initrd (optional) */
        static char diskKernelFallback[MAX_PATH];

        if (posCount >= 1)
        {
            kernelPath = posArgs[0];
        }
        else
        {
            if (!GetExeDir(exeDir, sizeof(exeDir)))
            {
                if (hMutex) CloseHandle(hMutex);
                return 1;
            }
            _snprintf(defaultKernel, sizeof(defaultKernel), "%srosv\\vmlinuz", exeDir);
            defaultKernel[sizeof(defaultKernel) - 1] = '\0';
            kernelPath = defaultKernel;
        }

        if (posCount < 1)
        {
            const char *resolvedKernel = ResolveDiskBootKernelPath(kernelPath,
                                                                    g_DiskImagePath,
                                                                    diskKernelFallback,
                                                                    sizeof(diskKernelFallback));
            if (resolvedKernel != kernelPath)
            {
                RoslLog("[INFO] Disk mode: bundled kernel missing, using %s\n",
                        resolvedKernel);
            }
            kernelPath = resolvedKernel;
        }

        /* In disk mode, initrd is optional (for small boot initrd) */
        initrdPath = (posCount >= 2) ? posArgs[1] : NULL;

        RoslLog("[INFO] Disk mode: image=%s\n", g_DiskImagePath);
        RoslLog("[INFO]   Kernel: %s\n", kernelPath);
        if (initrdPath)
            RoslLog("[INFO]   Initrd: %s\n", initrdPath);
        else
            RoslLog("[INFO]   Initrd: none (disk-backed root)\n");
    }
    else if (posCount >= 2)
    {
        /* Explicit paths provided */
        kernelPath = posArgs[0];
        initrdPath = posArgs[1];
    }
    else if (posCount == 0)
    {
        /* No args: look for bundled images in <exedir>\rosv\ */
        if (!GetExeDir(exeDir, sizeof(exeDir)))
        {
            if (hMutex) CloseHandle(hMutex);
            return 1;
        }

        _snprintf(defaultKernel, sizeof(defaultKernel), "%srosv\\vmlinuz", exeDir);
        defaultKernel[sizeof(defaultKernel) - 1] = '\0';

        /* Auto-detect VHDX:
         *  1) Prefer non-boot, non-CD volumes (wait briefly for late mounts).
         *  2) Fall back to bundled image next to the kernel.
         *  3) Last resort: scan all volumes, including CD media.
         *
         * Keep fixed/removable media ahead of optical media to reduce boot-time
         * variability during large random-read phases. */
        {
            static char defaultDisk[MAX_PATH];
            static char scannedDisk[MAX_PATH];
            static const char *Suffixes[] = {
                "\\ubuntu24.vhdx",
                "\\reactos\\system32\\rosv\\ubuntu24.vhdx"
            };
            char fallbackDrive = 0;
            DWORD startTick = GetTickCount();
            const DWORD waitMs = 15000;
            int loggedWait = 0;
            size_t si;
            char drive;

            _snprintf(defaultDisk, sizeof(defaultDisk), "%srosv\\ubuntu24.vhdx", exeDir);
            defaultDisk[sizeof(defaultDisk) - 1] = '\0';

            /* Bundled VHDX next to the kernel (e.g. X:\reactos\system32\rosv\).
             * If present, use it immediately — no need to poll data drives. */
            if (GetFileAttributesA(defaultDisk) != INVALID_FILE_ATTRIBUTES)
            {
                g_DiskImagePath = defaultDisk;
                RoslLog("[INFO] Found bundled VHDX: %s\n", defaultDisk);
            }

            /* Pass 1: poll non-boot non-CD volumes briefly for late mounts. */
            while (g_DiskImagePath == NULL &&
                   (GetTickCount() - startTick) < waitMs)
            {
                DWORD logicalDrives = GetLogicalDrives();

                for (si = 0; si < sizeof(Suffixes) / sizeof(Suffixes[0]) && g_DiskImagePath == NULL; si++)
                {
                    for (drive = 'C'; drive <= 'Z'; drive++)
                    {
                        char rootPath[4] = "C:\\";
                        UINT driveType;

                        if ((logicalDrives & (1UL << (drive - 'A'))) == 0)
                            continue;

                        rootPath[0] = drive;
                        driveType = GetDriveTypeA(rootPath);
                        if (driveType == DRIVE_UNKNOWN || driveType == DRIVE_NO_ROOT_DIR)
                            continue;
                        if (driveType == DRIVE_CDROM)
                            continue;
                        if (fallbackDrive != 0 && drive == fallbackDrive)
                            continue;

                        _snprintf(scannedDisk, sizeof(scannedDisk), "%c:%s", drive, Suffixes[si]);
                        scannedDisk[sizeof(scannedDisk) - 1] = '\0';
                        if (GetFileAttributesA(scannedDisk) != INVALID_FILE_ATTRIBUTES)
                        {
                            g_DiskImagePath = scannedDisk;
                            break;
                        }
                    }
                }

                if (g_DiskImagePath != NULL)
                    break;

                if (!loggedWait && fallbackDrive != 0)
                {
                    RoslLog("[INFO] Waiting up to %lu ms for non-CD VHDX volume...\n",
                            (unsigned long)waitMs);
                    loggedWait = 1;
                }
                Sleep(250);
            }

            /* Pass 2: bundled fallback next to kernel image. */
            if (g_DiskImagePath == NULL &&
                GetFileAttributesA(defaultDisk) != INVALID_FILE_ATTRIBUTES)
            {
                g_DiskImagePath = defaultDisk;
            }

            /* Pass 3: final broad scan including CD fallback.
             * A cleaner long-term direction is volume-GUID enumeration once
             * those APIs are consistently available on ReactOS. */
            if (g_DiskImagePath == NULL)
            {
                DWORD logicalDrives = GetLogicalDrives();
                int pass;

                for (pass = 0; pass < 2 && g_DiskImagePath == NULL; pass++)
                {
                    for (si = 0; si < sizeof(Suffixes) / sizeof(Suffixes[0]) && g_DiskImagePath == NULL; si++)
                    {
                        for (drive = 'C'; drive <= 'Z'; drive++)
                        {
                            char rootPath[4] = "C:\\";
                            UINT driveType;

                            if ((logicalDrives & (1UL << (drive - 'A'))) == 0)
                                continue;

                            rootPath[0] = drive;
                            driveType = GetDriveTypeA(rootPath);
                            if (driveType == DRIVE_UNKNOWN || driveType == DRIVE_NO_ROOT_DIR)
                                continue;

                            if (pass == 0)
                            {
                                if (driveType == DRIVE_CDROM)
                                    continue;
                            }
                            else
                            {
                                if (driveType != DRIVE_CDROM)
                                    continue;
                            }

                            _snprintf(scannedDisk, sizeof(scannedDisk), "%c:%s", drive, Suffixes[si]);
                            scannedDisk[sizeof(scannedDisk) - 1] = '\0';
                            if (GetFileAttributesA(scannedDisk) != INVALID_FILE_ATTRIBUTES)
                            {
                                g_DiskImagePath = scannedDisk;
                                break;
                            }
                        }
                    }
                }
            }
        }

        if (g_DiskImagePath != NULL)
        {
            static char autoKernelFallback[MAX_PATH];
            kernelPath = ResolveDiskBootKernelPath(defaultKernel,
                                                   g_DiskImagePath,
                                                   autoKernelFallback,
                                                   sizeof(autoKernelFallback));
            initrdPath = NULL;  /* no initrd needed for disk boot */
            RoslLog("[INFO] Auto-detected VHDX: %s\n", g_DiskImagePath);
            RoslLog("[INFO]   Kernel: %s\n", kernelPath);
            RoslLog("[INFO]   Initrd: none (disk-backed root)\n");
        }
        else
        {
            /* Fallback to initrd mode */
            _snprintf(defaultInitrd, sizeof(defaultInitrd), "%srosv\\initrd.img", exeDir);
            defaultInitrd[sizeof(defaultInitrd) - 1] = '\0';

            RoslLog("[INFO] No arguments given, looking for bundled images:\n");
            RoslLog("[INFO]   Kernel: %s\n", defaultKernel);
            RoslLog("[INFO]   Initrd: %s\n", defaultInitrd);

            kernelPath = defaultKernel;
            initrdPath = defaultInitrd;
        }
    }
    else
    {
        PrintUsage();
        if (hMutex) CloseHandle(hMutex);
        return 1;
    }

    ramMB = (posCount >= 3) ? (ULONG)atoi(posArgs[2])
                            : (g_DiskImagePath ? DEFAULT_DISK_RAM_MB : DEFAULT_RAM_MB);

    if (posCount >= 4)
    {
        /* Explicit cmdline from user - use as-is */
        cmdline = posArgs[3];
    }
    else if (g_DiskImagePath)
    {
        static char tscArg[64];
        /* Disk boot: use disk cmdline with virtio_mmio.device= parameters.
         * Both virtio-blk (disk) and virtio-net (network) MMIO devices. */
        static char diskCmdlineBuf[1152];
        ULONG tscEarlyKHz = RoslDetectTscEarlyKHz();
        const char *tscExtra = "";

        if (tscEarlyKHz != 0)
        {
            _snprintf(tscArg, sizeof(tscArg), " tsc_early_khz=%lu clocksource=tsc",
                      (unsigned long)tscEarlyKHz);
            tscArg[sizeof(tscArg) - 1] = '\0';
            tscExtra = tscArg;
            RoslLog("[INFO] TSC hint: tsc_early_khz=%lu\n", (unsigned long)tscEarlyKHz);
        }

        _snprintf(diskCmdlineBuf, sizeof(diskCmdlineBuf),
                  "%s%s %s %s %s %s",
                  DEFAULT_CMDLINE_COMMON,
                  tscExtra,
                  DEFAULT_DISK_ROOT_ARGS,
                  VIRTIO_MMIO_BLK_PARAM,
                  VIRTIO_MMIO_NET_PARAM,
                  VIRTIO_MMIO_CON_PARAM);
        diskCmdlineBuf[sizeof(diskCmdlineBuf) - 1] = '\0';
        cmdline = diskCmdlineBuf;
        RoslLog("[INFO] Disk boot mode: root=/dev/vda via virtio-blk (%s)\n",
                VIRTIO_MMIO_BLK_PARAM);
        RoslLog("[INFO] Network: virtio-net at %s\n",
                VIRTIO_MMIO_NET_PARAM);
    }
    else
    {
        static char tscArg[64];
        /* Ramdisk boot: use standard initramfs root args + virtio-net. */
        static char initrdCmdlineBuf[1152];
        ULONG tscEarlyKHz = RoslDetectTscEarlyKHz();
        const char *tscExtra = "";

        if (tscEarlyKHz != 0)
        {
            _snprintf(tscArg, sizeof(tscArg), " tsc_early_khz=%lu clocksource=tsc",
                      (unsigned long)tscEarlyKHz);
            tscArg[sizeof(tscArg) - 1] = '\0';
            tscExtra = tscArg;
            RoslLog("[INFO] TSC hint: tsc_early_khz=%lu\n", (unsigned long)tscEarlyKHz);
        }

        _snprintf(initrdCmdlineBuf, sizeof(initrdCmdlineBuf),
                  "%s%s %s %s %s",
                  DEFAULT_CMDLINE_COMMON,
                  tscExtra,
                  DEFAULT_INITRD_ROOT_ARGS,
                  VIRTIO_MMIO_NET_PARAM,
                  VIRTIO_MMIO_CON_PARAM);
        initrdCmdlineBuf[sizeof(initrdCmdlineBuf) - 1] = '\0';
        cmdline = initrdCmdlineBuf;
        RoslLog("[INFO] Network: virtio-net at %s\n",
                VIRTIO_MMIO_NET_PARAM);
    }

    if (ramMB == 0 || ramMB > 4096)
    {
        RoslLog("[FAIL] Invalid RAM size %lu MB (must be 1-4096)\n", ramMB);
        if (hMutex) CloseHandle(hMutex);
        return 1;
    }

    /* Detect initrd format and auto-size RAM if needed.
     * In disk mode with no initrd, skip this entirely. */
    memset(&initrdInfo, 0, sizeof(initrdInfo));
    if (initrdPath != NULL && GetInitrdInfo(initrdPath, &initrdInfo))
    {
        ULONG compMB = (initrdInfo.CompressedSize + (1024 * 1024 - 1)) / (1024 * 1024);
        ULONG uncompMB = (initrdInfo.UncompressedSize + (1024 * 1024 - 1)) / (1024 * 1024);

        if (initrdInfo.IsGzip && initrdInfo.UncompressedSize > 0)
        {
            ULONG neededMB;
            RoslLog("[INFO] Initrd image: gzip compressed=%lu MB, uncompressed=%lu MB\n",
                    compMB, uncompMB);

            /* During initramfs extraction, Linux needs free pages proportional
             * to total RAM (hash tables, page structs, etc.).  Empirically,
             * uncomp * 2 + 256 is the minimum that works reliably. */
            neededMB = ((uncompMB * 2 + 576) + 63) & ~63UL;
            if (neededMB < 512)
                neededMB = 512;
            if (neededMB > 3584)
                neededMB = 3584; /* Cap: leave room for host OS */

            if (posCount < 3 && neededMB > ramMB)
            {
                RoslLog("[INFO] Auto-sizing VM RAM from %lu MB to %lu MB for initrd unpack\n",
                        ramMB, neededMB);
                ramMB = neededMB;
            }

            /* Minimum RAM: must fit compressed initrd + 128 MB for kernel/overhead */
            minRamMB = compMB + 128;
        }
        else
        {
            RoslLog("[INFO] Initrd image: %lu MB (not gzip compressed)\n", compMB);
            minRamMB = compMB + 128;
        }
    }
    else
    {
        minRamMB = RAM_MIN_FLOOR_MB;
    }

    /* Round minRamMB up to 64 MB boundary */
    minRamMB = (minRamMB + 63) & ~63UL;
    if (minRamMB < RAM_MIN_FLOOR_MB)
        minRamMB = RAM_MIN_FLOOR_MB;

    RoslLog("[INFO] Role: %s\n",
            (g_AttachSessionId != (ULONG)-1) ? "client" : "service");
    RoslLog("[INFO] I/O mode: %s\n",
            g_IoMode == RoslModeVcon ? "VCON (virtio-console)" :
            g_IoMode == RoslModePty ? "PTY" : "UART (legacy)");
    if (g_AttachSessionId == (ULONG)-1 && g_IoMode == RoslModePty)
    {
        RoslLog("[INFO] Service keeps serial supervision; interactive PTYs are allocated only for clients\n");
    }

    SetConsoleCtrlHandler(CtrlHandler, TRUE);

    /* Step 0: Load driver if not already running */
    if (!EnsureDriverLoaded())
        Die("Failed to load rosv.sys driver");

    /* Step 1: Open device */
    RoslLog("[INFO] Opening \\\\.\\RosvHypervisor\n");
    g_hDev = OpenRosvDeviceWithRetry(50, 100);
    if (g_hDev == INVALID_HANDLE_VALUE)
        Die("Cannot open \\\\.\\RosvHypervisor - is rosv.sys loaded?");
    RoslLog("[OK]   Device opened\n");

    /* A running VM must be managed through the service control plane. */
    {
        ROSV_VM_STATE_INFO existingState;
        ULONG AutoSessionId = 0;
        memset(&existingState, 0, sizeof(existingState));
        if (Ioctl(ROSV_IOCTL_GET_STATE, NULL, 0, &existingState, sizeof(existingState), &ret) &&
            existingState.State == RosvVmStateRunning)
        {
            if (!g_ForceService && RoslDiscoverServiceSession(&AutoSessionId))
            {
                g_AttachSessionId = AutoSessionId;
                RoslLog("[INFO] Detected active supervisor session %lu; switching to client mode\n",
                        g_AttachSessionId);
                return RoslRunInteractiveClient(hMutex, TRUE);
            }

            RoslLog("[FAIL] A ROSL VM is already running. Re-run without --service to auto-attach as a client.\n");
            CloseHandle(g_hDev);
            g_hDev = INVALID_HANDLE_VALUE;
            if (hMutex) CloseHandle(hMutex);
            return 2;
        }
    }

    /* Steps 2+3: Create VM and configure memory (with retry on low memory). */
    if (!CreateAndConfigureVm(ramMB, minRamMB, &tmpCreateRes, &actualRamMB, &vmConfigErr))
    {
        SetLastError(vmConfigErr);
        Die("Failed to create VM and configure memory");
    }

    if (actualRamMB != ramMB)
    {
        RoslLog("[INFO] VM running with %lu MB (requested %lu MB, host memory limited)\n",
                actualRamMB, ramMB);
    }

    /* Step 4: Load kernel (read on demand to reduce peak host memory) */
    kernelBuf = ReadWholeFile(kernelPath, &kernelSz);
    if (!kernelBuf)
        Die("Failed to read kernel image");

    RoslLog("[INFO] ROSV_IOCTL_LOAD_KERNEL (%lu bytes)\n", kernelSz);
    if (!Ioctl(ROSV_IOCTL_LOAD_KERNEL, kernelBuf, kernelSz, NULL, 0, &ret))
        Die("IOCTL_LOAD_KERNEL failed");
    RoslLog("[OK]   Kernel loaded\n");
    HeapFree(GetProcessHeap(), 0, kernelBuf);
    kernelBuf = NULL;

    /* Step 5: Load initrd (optional in disk mode) */
    if (initrdPath != NULL)
    {
        if (!StreamInitrdFile(initrdPath, &initrdSz))
            Die("Failed to stream initrd image");
    }
    else
    {
        RoslLog("[INFO] No initrd - booting with disk-backed root only\n");
    }

    /* Step 5.5: Attach disk image (disk mode only) */
    if (g_DiskImagePath != NULL)
    {
        ROSV_DISK_ATTACH_REQUEST diskReq;
        ROSV_DISK_ATTACH_RESULT diskRes;
        DWORD pathLen = (DWORD)strlen(g_DiskImagePath);
        int wideLen;

        if (pathLen >= ROSV_DISK_PATH_MAX)
        {
            RoslLog("[FAIL] Disk image path too long (%lu chars, max %d)\n",
                    pathLen, ROSV_DISK_PATH_MAX - 1);
            Die("Disk image path exceeds maximum length");
        }

        memset(&diskReq, 0, sizeof(diskReq));
        diskReq.DiskSizeBytes = 0; /* auto-detect from file */
        diskReq.Flags = 0;

        /* Convert narrow path to wide chars for the kernel driver */
        wideLen = MultiByteToWideChar(CP_ACP, 0, g_DiskImagePath, (int)pathLen,
                                      diskReq.Path, ROSV_DISK_PATH_MAX - 1);
        if (wideLen <= 0)
        {
            RoslLog("[FAIL] Failed to convert disk path to wide chars (err=%lu)\n",
                    GetLastError());
            Die("Disk path conversion failed");
        }
        diskReq.PathLength = (ULONG)wideLen;
        diskReq.Path[wideLen] = L'\0';

        memset(&diskRes, 0, sizeof(diskRes));
        RoslLog("[INFO] ROSV_IOCTL_ATTACH_DISK: \"%s\"\n", g_DiskImagePath);
        if (!Ioctl(ROSV_IOCTL_ATTACH_DISK,
                   &diskReq, sizeof(diskReq),
                   &diskRes, sizeof(diskRes), &ret))
        {
            RoslLog("[FAIL] IOCTL_ATTACH_DISK failed (err=%lu)\n", GetLastError());
            Die("Failed to attach disk image");
        }
        {
            const char *modeName = (diskRes.DiskMode == 1) ? "demand-paged" : "ramdisk";
            const char *backendName = (diskRes.BackendType == 1) ? "VHDX" : "RAW";
            RoslLog("[OK]   Disk attached (%s, %s): index=%lu, size=%llu bytes (%llu MB)\n",
                    modeName, backendName,
                    diskRes.DiskIndex,
                    (unsigned long long)diskRes.DiskSizeBytes,
                    (unsigned long long)(diskRes.DiskSizeBytes / (1024 * 1024)));
        }
    }

    /* Step 6: Set command line */
    RoslLog("[INFO] ROSV_IOCTL_SET_CMDLINE: \"%s\"\n", cmdline);
    if (!Ioctl(ROSV_IOCTL_SET_CMDLINE, (void *)cmdline, (DWORD)(strlen(cmdline) + 1), NULL, 0, &ret))
        Die("IOCTL_SET_CMDLINE failed");
    RoslLog("[OK]   Command line set\n");

    /* Step 7: Start VM */
    RoslLog("[INFO] ROSV_IOCTL_START_VM\n");
    if (!Ioctl(ROSV_IOCTL_START_VM, NULL, 0, NULL, 0, &ret))
        Die("IOCTL_START_VM failed");
    RoslLog("[OK]   VM started!\n\n");

    RoslLog("[INFO] Network backend: %s\n",
            g_NetBackendType == 2 ? "in-kernel netio" :
            g_NetBackendType == 1 ? "external netd/slirp (launch rosl_netd)" : "none");

    /* Step 8b: Launch rosl_netd only for the legacy external netd backend */
    if (g_NetBackendType == 1)
    {
        STARTUPINFOA si;
        PROCESS_INFORMATION pi;
        char netdCmd[] = "rosl_netd.exe --backend=slirp";

        memset(&si, 0, sizeof(si));
        si.cb = sizeof(si);
        memset(&pi, 0, sizeof(pi));

        RoslLog("[INFO] Launching: %s\n", netdCmd);
        if (CreateProcessA(NULL, netdCmd, NULL, NULL, FALSE,
                           CREATE_NEW_CONSOLE, NULL, NULL, &si, &pi))
        {
            RoslLog("[OK]   rosl_netd started (PID=%lu)\n", pi.dwProcessId);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
        else
        {
            RoslLog("[WARN] Failed to launch rosl_netd (err=%lu)\n", GetLastError());
        }
    }

    if (!RoslStartControlServer(tmpCreateRes.VmId))
    {
        RoslLog("[FAIL] Failed to start service control plane\n");
        Ioctl(ROSV_IOCTL_DESTROY_VM, NULL, 0, NULL, 0, &ret);
        CloseHandle(g_hDev);
        g_hDev = INVALID_HANDLE_VALUE;
        RoslDeleteControlState();
        if (hMutex) CloseHandle(hMutex);
        return 1;
    }

    RoslLog("[INFO] Service session ID: %lu\n", g_ServiceSessionId);
    RoslLog("[INFO] Attach interactive clients with: rosl.exe --attach %lu\n",
            g_ServiceSessionId);

    /* Step 8: Run the serial supervisor loop */
    ServiceSupervisorLoop();

    RoslStopControlServer();
    RoslLog("[INFO] ROSV_IOCTL_DESTROY_VM\n");
    if (!Ioctl(ROSV_IOCTL_DESTROY_VM, NULL, 0, NULL, 0, &ret))
    {
        RoslLog("[WARN] IOCTL_DESTROY_VM failed (err=%lu)\n", GetLastError());
    }
    else
    {
        RoslLog("[OK]   VM destroyed\n");
    }

    CloseHandle(g_hDev);
    g_hDev = INVALID_HANDLE_VALUE;
    RoslDeleteControlState();
    if (hMutex) CloseHandle(hMutex);
    RoslLog("[DONE] Service stopped.\n");
    return 0;
}
