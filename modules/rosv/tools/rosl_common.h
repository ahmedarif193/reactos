/*
 * PROJECT:     ReactOS VMX Hypervisor Launcher (rosl.exe)
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Shared header for rosl multi-file build
 * COPYRIGHT:   Copyright 2025-2026 Ahmed Arif
 */

#pragma once

#include <windows.h>
#include <winioctl.h>
#include <intrin.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "log_style.h"

/* DbgPrint from ntdll - writes to kernel debug output (serial port) */
ULONG __cdecl DbgPrint(PCSTR Format, ...);

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

/* ---- Enums and typedefs ------------------------------------------------- */

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

typedef struct {
    DWORD CompressedSize;       /* File size on disk */
    DWORD UncompressedSize;     /* From gzip trailer (0 if not gzip) */
    BOOL  IsGzip;
} ROSL_INITRD_INFO;

/* ---- Constants ---------------------------------------------------------- */

#define DEFAULT_RAM_MB  1024
#define DEFAULT_DISK_RAM_MB  1024  /* Need ~1GB for kernels with KASAN/debug features */
#define DEFAULT_CMDLINE_COMMON \
    "console=ttyS0,115200n8 " \
    "earlyprintk=serial,ttyS0,115200 " \
    "nokaslr kasan=off nosoftlockup " \
    "i8042.noaux i8042.nopnp " \
    "nomodeset video=vesafb:off " \
    "systemd.getty_auto=no " \
    "systemd.mask=console-getty.service " \
    "systemd.mask=getty@tty1.service " \
    "systemd.mask=landscape-client.service " \
    "systemd.mask=systemd-timesyncd.service " \
    "systemd.mask=apt-daily.service " \
    "systemd.mask=apt-daily.timer " \
    "systemd.mask=apt-daily-upgrade.service " \
    "systemd.mask=apt-daily-upgrade.timer " \
    "systemd.mask=unattended-upgrades.service " \
    "systemd.mask=motd-news.service " \
    "systemd.mask=motd-news.timer " \
    "systemd.mask=ua-timer.timer " \
    "systemd.mask=ubuntu-advantage.service " \
    "sysctl.net.ipv4.ping_group_range=\"0 2147483647\""
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
#define ROSL_WSL_PROBE_MAX_CMDS 16
#define ROSL_WSL_PROBE_CMD_COUNT g_WslProbeCmdCount

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

#define ROSV_SERVICE_NAME   "rosv"
#define ROSV_DRIVER_PATH    "system32\\drivers\\rosv.sys"
#define ROSV_SERVICE_STATE_TIMEOUT_MS 5000

#define RAM_RETRY_STEP_MB   64
#define RAM_MIN_FLOOR_MB    256

/* ---- Extern global variables -------------------------------------------- */

extern HANDLE g_hDev;
extern volatile BOOL g_Running;
extern HANDLE g_hStdin;
extern HANDLE g_hStdout;
extern DWORD g_OrigConsoleMode;
extern DWORD g_OrigStdoutMode;
extern UINT g_OrigConsoleCP;
extern UINT g_OrigStdoutCP;
extern BOOL g_StdinIsConsole;
extern BOOL g_StdoutIsConsole;
extern BOOL g_ConsoleCpChanged;
extern BOOL g_StdoutCpChanged;
extern DWORD g_StdinType;
extern ROSL_IO_MODE g_IoMode;
extern ROSL_WSL_PROBE_MODE g_WslProbeMode;
extern ULONG g_PtyIndex;
extern ULONG g_ReaderIndex;
extern ULONG g_VconPort;
extern ULONG g_AttachSessionId;
extern BOOL g_ForceService;
extern BOOL g_SelfTestMode;
extern USHORT g_InitialRows;
extern USHORT g_InitialCols;
extern const char *g_DiskImagePath;
extern const char *g_BootCmd;
extern ULONG g_NetBackendType;
extern BOOL g_ProbeNetTests;
extern BOOL g_CursorKeysApplication;
extern UCHAR g_VtSeqBuf[16];
extern DWORD g_VtSeqLen;
extern ULONG g_ServiceSessionId;
extern BOOL g_ServiceSerialPromptSeen;
extern HANDLE g_ControlStopEvent;
extern HANDLE g_ControlListenerThread;
extern HANDLE g_ServicePromptEvent;
extern CRITICAL_SECTION g_ControlStateLock;
extern BOOL g_ControlStateLockInitialized;
extern char g_ControlPipeName[128];
extern HANDLE g_DiscoveryMapping;
extern ROSL_DISCOVERY_SHARED *g_DiscoveryView;

/* WSL probe command arrays */
extern const char *g_WslProbeCommands[ROSL_WSL_PROBE_MAX_CMDS];
extern DWORD g_WslProbeCmdCount;

/* ---- Function prototypes (shared across files) -------------------------- */

/* rosl_common.c */
void RoslLog(const char *fmt, ...);
ULONG RoslDetectTscEarlyKHz(VOID);
const char *StateName(ROSV_VM_STATE s);
const char *CheckpointName(ROSV_CHECKPOINT cp);
void Die(const char *msg);
BOOL Ioctl(DWORD code, void *in, DWORD inSz, void *out, DWORD outSz, DWORD *ret);
void RoslInitControlState(void);
void RoslDeleteControlState(void);
void RoslBuildControlPipeName(_In_ ULONG SessionId, _Out_writes_(BufferSize) char *Buffer, _In_ size_t BufferSize);
BOOL RoslConsoleSendInput(_In_reads_bytes_(Length) const void *Data, _In_ DWORD Length);
BOOL RoslQueryVmStats(_Out_ ROSV_VM_STATS *Stats);
ULONG RoslComputeBusyPercent(_In_ ULONGLONG TotalTicks, _In_ ULONGLONG HltTicks);
void RoslLogVmCpuUsage(_In_z_ const char *Tag, _In_ const ROSV_VM_STATS *Stats, _Inout_ ULONGLONG *LastExitCount, _Inout_ ULONGLONG *LastTotalTicks, _Inout_ ULONGLONG *LastHltTicks);
BOOL RoslWriteVconPort(_In_ ULONG PortIndex, _In_reads_bytes_(Length) const void *Data, _In_ DWORD Length);
void RoslResetTerminalInputMode(void);
BOOL RoslIsVtFinalByte(UCHAR Byte);
void RoslApplyVtSequence(const UCHAR *Seq, DWORD Length);
DWORD RoslFeedTerminalOutput(_Inout_updates_bytes_(Length) UCHAR *Data, _In_ DWORD Length);
DWORD RoslTranslateKeyEvent(_In_ const KEY_EVENT_RECORD *KeyEvent, _Out_writes_bytes_(SeqSize) UCHAR *Seq, _In_ DWORD SeqSize);
void ConsoleSetRawMode(void);
void ConsoleEnableVtOutput(void);
void ConsoleRestoreMode(void);
DWORD ReadRedirectedStdin(_Out_writes_bytes_(BufferSize) UCHAR *Buffer, _In_ DWORD BufferSize);
BOOL WINAPI CtrlHandler(DWORD type);
BOOL PtySendInput(const void *data, DWORD len);
BOOL VconSendInput(const void *data, DWORD len);
BOOL VconReadOutput(void *buf, DWORD bufSize, DWORD *bytesRead);
BOOL PtyResize(USHORT rows, USHORT cols);
void ConfigurePtyLowLatencyMode(void);
BOOL RoslPipeReadExact(_In_ HANDLE Pipe, _Out_writes_bytes_(Size) void *Buffer, _In_ DWORD Size);
BOOL RoslPipeWriteExact(_In_ HANDLE Pipe, _In_reads_bytes_(Size) const void *Buffer, _In_ DWORD Size);
BOOL WaitForServiceState(_In_ SC_HANDLE hSvc, _In_ DWORD DesiredState, _In_ DWORD TimeoutMs);
BOOL EnsureDriverLoaded(void);
HANDLE OpenRosvDeviceWithRetry(_In_ ULONG Attempts, _In_ DWORD DelayMs);
BOOL GetInitrdInfo(const char *path, ROSL_INITRD_INFO *info);
BYTE *ReadWholeFile(const char *path, DWORD *outSize);
BOOL StreamInitrdFile(const char *path, DWORD *outSize);
BOOL GetExeDir(char *buf, DWORD bufSize);

/* rosl_service.c */
BOOL RoslServiceStartVconShell(_In_ ULONG PortIndex);
BOOL RoslServiceWaitForSerialPrompt(void);
void RoslServiceNoteSerialPrompt(void);
void RoslBuildProbeCommandList(void);
void RoslWslProbeInit(_Out_ ROSL_WSL_PROBE_STATE *Probe, _In_ BOOL Enable, _In_opt_ ROSL_SEND_INPUT_ROUTINE SendInput, _In_opt_z_ const char *ChannelName);
void RoslWslProbeAppendTail(_Inout_ ROSL_WSL_PROBE_STATE *Probe, _In_reads_bytes_(Length) const UCHAR *Data, _In_ DWORD Length);
BOOL RoslWslProbePromptDetected(_In_ const ROSL_WSL_PROBE_STATE *Probe);
BOOL RoslWslProbePromptAtTail(_In_ const ROSL_WSL_PROBE_STATE *Probe);
void RoslWslProbeFeedOutput(_Inout_ ROSL_WSL_PROBE_STATE *Probe, _In_reads_bytes_(Length) const UCHAR *Data, _In_ DWORD Length);
void RoslWslProbeTick(_Inout_ ROSL_WSL_PROBE_STATE *Probe);
void ServiceSupervisorLoop(void);
BOOL CreateAndConfigureVm(_In_ ULONG ramMb, _In_ ULONG minRamMb, _Out_ ROSV_VM_CREATE_RESULT *createRes, _Out_ ULONG *actualRamMb, _Out_opt_ DWORD *lastErrorOut);
DWORD WINAPI RoslControlClientThread(_In_ LPVOID Parameter);
DWORD WINAPI RoslControlListenerThread(_In_ LPVOID Parameter);
void RoslWakeControlListener(void);
BOOL RoslStartControlServer(_In_ ULONG SessionId);
void RoslStopControlServer(void);
BOOL RoslPublishServiceDiscovery(_In_ ULONG SessionId);
void RoslUnpublishServiceDiscovery(void);

/* rosl_client.c */
void VconInteractiveLoop(void);
void PtyInteractiveLoop(void);
void UartInteractiveLoop(void);
HANDLE RoslClientConnectControl(_In_ ULONG SessionId);
BOOL RoslClientAllocatePty(_In_ HANDLE Pipe, _In_ ULONG SessionId, _In_ USHORT Rows, _In_ USHORT Cols, _Out_ PULONG PtyIndex, _Out_ PULONG ReaderIndex, _Out_opt_ PULONG Flags);
BOOL RoslDiscoverServiceSession(_Out_ PULONG SessionId);
void RoslClientReleasePty(_In_ HANDLE Pipe, _In_ ULONG SessionId, _In_ ULONG PtyIndex, _In_ ULONG ReaderIndex);
int RoslRunInteractiveClient(_In_opt_ HANDLE hMutex, _In_ BOOL DeviceAlreadyOpen);

/* rosl_main.c */
void PrintUsage(void);
int ParseOptions(int argc, char *argv[], char *posArgs[], int maxPos);
const char *ResolveDiskBootKernelPath(const char *defaultKernelPath, const char *diskImagePath, char *fallbackBuf, DWORD fallbackBufSize);
