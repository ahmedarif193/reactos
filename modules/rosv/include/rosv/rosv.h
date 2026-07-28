/*
 * PROJECT:     ReactOS VMX Hypervisor Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Main public header - IOCTLs, config structs, debug macros
 * COPYRIGHT:   Copyright 2025 Ahmed Arif
 */

#pragma once

#include <ntddk.h>

/* ---- Driver identity ---------------------------------------------------- */

#define ROSV_DEVICE_NAME    L"\\Device\\RosvHypervisor"
#define ROSV_SYMLINK_NAME   L"\\DosDevices\\RosvHypervisor"
#define ROSV_DRIVER_TAG     'vsoR'

/* ---- Logging macros ------------------------------------------------------ */

#define ROSV_ERR(fmt, ...) \
    DbgPrint("[ROSV:ERR] " fmt "\n", ##__VA_ARGS__)
#define ROSV_WARN(fmt, ...) \
    DbgPrint("[ROSV:WARN] " fmt "\n", ##__VA_ARGS__)

/*
 * TRACE/DEBUG logging is opt-in.
 * Define ROSV_ENABLE_TRACE=1 at compile time to enable it.
 */
#if defined(ROSV_ENABLE_TRACE) && (ROSV_ENABLE_TRACE != 0)
#define ROSV_TRACE(fmt, ...) \
    DbgPrint("[ROSV] " fmt "\n", ##__VA_ARGS__)
#if DBG
#define ROSV_DEBUG(fmt, ...) \
    DbgPrint("[ROSV:DBG] " fmt "\n", ##__VA_ARGS__)
#else
#define ROSV_DEBUG(fmt, ...)  do { } while (0)
#endif
#else
#define ROSV_TRACE(fmt, ...)  do { } while (0)
#define ROSV_DEBUG(fmt, ...)  do { } while (0)
#endif

#define ROSV_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        ROSV_ERR("ASSERTION FAILED: %s at %s:%d - %s", \
                 #cond, __FILE__, __LINE__, msg); \
        DbgBreakPoint(); \
    } \
} while(0)

/* ---- IOCTL codes -------------------------------------------------------- */

#define ROSV_IOCTL_TYPE     0x8000  /* Private device type */

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
#define ROSV_IOCTL_NET_ATTACH   CTL_CODE(ROSV_IOCTL_TYPE, 0x80C, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define ROSV_IOCTL_NET_DETACH   CTL_CODE(ROSV_IOCTL_TYPE, 0x80D, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define ROSV_IOCTL_NET_TX       CTL_CODE(ROSV_IOCTL_TYPE, 0x80E, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define ROSV_IOCTL_NET_RX       CTL_CODE(ROSV_IOCTL_TYPE, 0x80F, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define ROSV_IOCTL_NET_GET_STATS CTL_CODE(ROSV_IOCTL_TYPE, 0x810, METHOD_BUFFERED, FILE_ANY_ACCESS)

/* Chunked initrd transfer (avoids huge METHOD_BUFFERED allocations) */
#define ROSV_IOCTL_INITRD_BEGIN CTL_CODE(ROSV_IOCTL_TYPE, 0x811, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define ROSV_IOCTL_INITRD_CHUNK CTL_CODE(ROSV_IOCTL_TYPE, 0x812, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define ROSV_IOCTL_INITRD_COMMIT CTL_CODE(ROSV_IOCTL_TYPE, 0x813, METHOD_BUFFERED, FILE_ANY_ACCESS)

/* PTY/PTS IOCTLs */
#define ROSV_IOCTL_PTY_CREATE       CTL_CODE(ROSV_IOCTL_TYPE, 0x820, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define ROSV_IOCTL_PTY_DESTROY      CTL_CODE(ROSV_IOCTL_TYPE, 0x821, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define ROSV_IOCTL_PTY_READ         CTL_CODE(ROSV_IOCTL_TYPE, 0x822, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define ROSV_IOCTL_PTY_WRITE        CTL_CODE(ROSV_IOCTL_TYPE, 0x823, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define ROSV_IOCTL_PTY_RESIZE       CTL_CODE(ROSV_IOCTL_TYPE, 0x824, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define ROSV_IOCTL_PTY_GET_TERMIOS  CTL_CODE(ROSV_IOCTL_TYPE, 0x825, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define ROSV_IOCTL_PTY_SET_TERMIOS  CTL_CODE(ROSV_IOCTL_TYPE, 0x826, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define ROSV_IOCTL_PTY_SIGNAL       CTL_CODE(ROSV_IOCTL_TYPE, 0x827, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define ROSV_IOCTL_PTY_FLUSH        CTL_CODE(ROSV_IOCTL_TYPE, 0x828, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define ROSV_IOCTL_PTY_GET_INFO     CTL_CODE(ROSV_IOCTL_TYPE, 0x829, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define ROSV_IOCTL_PTY_ATTACH       CTL_CODE(ROSV_IOCTL_TYPE, 0x82A, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define ROSV_IOCTL_PTY_DETACH       CTL_CODE(ROSV_IOCTL_TYPE, 0x82B, METHOD_BUFFERED, FILE_ANY_ACCESS)

/* Virtio-console port IOCTLs */
#define ROSV_IOCTL_VCON_PORT_WRITE  CTL_CODE(ROSV_IOCTL_TYPE, 0x880, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define ROSV_IOCTL_VCON_PORT_READ   CTL_CODE(ROSV_IOCTL_TYPE, 0x881, METHOD_BUFFERED, FILE_ANY_ACCESS)

/* Session lifecycle IOCTLs */
#define ROSV_IOCTL_SESSION_CREATE   CTL_CODE(ROSV_IOCTL_TYPE, 0x830, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define ROSV_IOCTL_SESSION_START    CTL_CODE(ROSV_IOCTL_TYPE, 0x831, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define ROSV_IOCTL_SESSION_STOP     CTL_CODE(ROSV_IOCTL_TYPE, 0x832, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define ROSV_IOCTL_SESSION_DESTROY  CTL_CODE(ROSV_IOCTL_TYPE, 0x833, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define ROSV_IOCTL_SESSION_LIST     CTL_CODE(ROSV_IOCTL_TYPE, 0x834, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define ROSV_IOCTL_SESSION_ATTACH   CTL_CODE(ROSV_IOCTL_TYPE, 0x835, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define ROSV_IOCTL_SESSION_DETACH   CTL_CODE(ROSV_IOCTL_TYPE, 0x836, METHOD_BUFFERED, FILE_ANY_ACCESS)

/* Filesystem mount IOCTLs */
#define ROSV_IOCTL_FS_MOUNT         CTL_CODE(ROSV_IOCTL_TYPE, 0x840, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define ROSV_IOCTL_FS_UNMOUNT       CTL_CODE(ROSV_IOCTL_TYPE, 0x841, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define ROSV_IOCTL_FS_LIST          CTL_CODE(ROSV_IOCTL_TYPE, 0x842, METHOD_BUFFERED, FILE_ANY_ACCESS)

/* Network port forwarding IOCTLs */
#define ROSV_IOCTL_NET_FORWARD      CTL_CODE(ROSV_IOCTL_TYPE, 0x850, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define ROSV_IOCTL_NET_UNFORWARD    CTL_CODE(ROSV_IOCTL_TYPE, 0x851, METHOD_BUFFERED, FILE_ANY_ACCESS)

/* VM stats IOCTL */
#define ROSV_IOCTL_GET_VM_STATS     CTL_CODE(ROSV_IOCTL_TYPE, 0x870, METHOD_BUFFERED, FILE_ANY_ACCESS)

/* Disk image IOCTLs (virtio-blk backend) */
#define ROSV_IOCTL_ATTACH_DISK      CTL_CODE(ROSV_IOCTL_TYPE, 0x860, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define ROSV_IOCTL_DETACH_DISK      CTL_CODE(ROSV_IOCTL_TYPE, 0x861, METHOD_BUFFERED, FILE_ANY_ACCESS)

/* ---- VM states ---------------------------------------------------------- */

typedef enum _ROSV_VM_STATE {
    RosvVmStateCreated = 0,
    RosvVmStateMemorySet,
    RosvVmStateKernelLoaded,
    RosvVmStateRunning,
    RosvVmStateStopped,
    RosvVmStateError,
    RosvVmStateCrashed
} ROSV_VM_STATE;

/* ---- Boot trace checkpoints --------------------------------------------- */

typedef enum _ROSV_CHECKPOINT {
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

/* ---- IOCTL structures --------------------------------------------------- */

typedef struct _ROSV_VCON_PORT_IO {
    ULONG PortIndex;            /* Which port (0 = hvc0, 1 = hvc1, ...) */
    ULONG Length;               /* Bytes to write / max bytes to read */
    UCHAR Data[4096];           /* Data payload */
} ROSV_VCON_PORT_IO, *PROSV_VCON_PORT_IO;

typedef struct _ROSV_VM_CONFIG {
    ULONG RamSizeMB;
    ULONG NetBackendType;  /* ROSV_NET_BACKEND_TYPE: 0=none, 1=netd/slirp, 2=netio (default) */
} ROSV_VM_CONFIG, *PROSV_VM_CONFIG;

typedef struct _ROSV_VM_CREATE_RESULT {
    ULONG VmId;
    NTSTATUS Status;
} ROSV_VM_CREATE_RESULT, *PROSV_VM_CREATE_RESULT;

typedef struct _ROSV_VM_STATE_INFO {
    ROSV_VM_STATE State;
    ULONG64 ExitCount;
    ULONG LastExitReason;
    ROSV_CHECKPOINT LastCheckpoint;
} ROSV_VM_STATE_INFO, *PROSV_VM_STATE_INFO;

typedef struct _ROSV_VM_STATS {
    ULONG64 ExitCount;
    ULONG64 ExitHlt;
    ULONG64 ExitPreempt;
    ULONG64 ExitEpt;
    ULONG64 ExitIo;
    ULONG64 ExitMsr;
    ULONG64 ExitExtInt;
    ULONG64 ExitIntWin;
    ULONG64 ExitOther;
    ULONG64 TimerInjected;
    ULONG64 HltYield;
    ULONG64 SpinYield;
    ULONG64 HltTicks;      /* QPC ticks spent in HLT sleep */
    ULONG64 TotalTicks;    /* QPC ticks total wall clock */
} ROSV_VM_STATS, *PROSV_VM_STATS;

/* ---- Chunked initrd transfer ------------------------------------------- */

typedef struct _ROSV_INITRD_BEGIN_REQUEST {
    ULONG64 TotalSize;     /* Total initrd bytes expected */
    ULONG Flags;           /* Reserved, must be 0 for now */
} ROSV_INITRD_BEGIN_REQUEST, *PROSV_INITRD_BEGIN_REQUEST;

typedef struct _ROSV_INITRD_CHUNK_REQUEST {
    ULONG64 Offset;        /* File offset of this chunk */
    ULONG DataLength;      /* Valid bytes in Data[] */
    ULONG Flags;           /* Reserved, must be 0 for now */
    UCHAR Data[1];         /* Variable payload bytes */
} ROSV_INITRD_CHUNK_REQUEST, *PROSV_INITRD_CHUNK_REQUEST;

/* ---- Networking control plane (rosl-netd <-> rosv) --------------------- */

#define ROSV_NET_PACKET_MAX  1600

typedef enum _ROSV_NET_BACKEND_TYPE {
    RosvNetBackendNone = 0,
    RosvNetBackendSlirp = 1,
    RosvNetBackendNetio = 2
} ROSV_NET_BACKEND_TYPE;

typedef struct _ROSV_NET_ATTACH_REQUEST {
    ULONG BackendType;    /* ROSV_NET_BACKEND_TYPE */
    ULONG Flags;
    UCHAR GuestMac[6];
    UCHAR Reserved[2];
} ROSV_NET_ATTACH_REQUEST, *PROSV_NET_ATTACH_REQUEST;

typedef struct _ROSV_NET_PACKET {
    ULONG Length;         /* bytes used in Data[] */
    UCHAR Data[ROSV_NET_PACKET_MAX];
} ROSV_NET_PACKET, *PROSV_NET_PACKET;

typedef struct _ROSV_NET_STATS {
    ULONG BackendType;    /* ROSV_NET_BACKEND_TYPE */
    ULONG Attached;       /* BOOLEAN as ULONG for user-mode ABI */
    ULONG64 TxPackets;
    ULONG64 TxBytes;
    ULONG64 RxPackets;
    ULONG64 RxBytes;
} ROSV_NET_STATS, *PROSV_NET_STATS;

/* ---- Exit log entry ----------------------------------------------------- */

#define ROSV_EXIT_LOG_SIZE  4096

typedef struct _ROSV_EXIT_LOG_ENTRY {
    ULONG64 Timestamp;
    ULONG64 ExitQualification;
    ULONG64 GuestRip;
    ULONG ExitReason;
    ULONG InstructionLength;
} ROSV_EXIT_LOG_ENTRY, *PROSV_EXIT_LOG_ENTRY;

C_ASSERT(sizeof(ROSV_EXIT_LOG_ENTRY) == 32);

/* ---- Disk image (virtio-blk backend) ------------------------------------ */

#define ROSV_DISK_PATH_MAX  260  /* MAX_PATH */

typedef struct _ROSV_DISK_ATTACH_REQUEST {
    ULONG64 DiskSizeBytes;      /* 0 = auto-detect from file */
    ULONG Flags;                /* Reserved, must be 0 */
    ULONG PathLength;           /* Characters in Path[], excluding NUL */
    WCHAR Path[ROSV_DISK_PATH_MAX]; /* Win32 path to raw image (wide chars) */
} ROSV_DISK_ATTACH_REQUEST, *PROSV_DISK_ATTACH_REQUEST;

typedef struct _ROSV_DISK_ATTACH_RESULT {
    ULONG64 DiskSizeBytes;      /* Actual disk size */
    ULONG   DiskIndex;          /* Assigned disk index (0 = /dev/vda) */
    LONG    Status;             /* NTSTATUS */
    ULONG   DiskMode;           /* 0=ramdisk, 1=demand-paged (ROSV_DISK_MODE) */
    ULONG   BackendType;        /* 0=raw, 1=vhdx (ROSV_DISK_BACKEND_TYPE) */
} ROSV_DISK_ATTACH_RESULT, *PROSV_DISK_ATTACH_RESULT;

/* ---- Forward declarations for internal structures ----------------------- */

typedef struct _ROSV_VM ROSV_VM, *PROSV_VM;
typedef struct _ROSV_VCPU ROSV_VCPU, *PROSV_VCPU;
typedef struct _ROSV_EPT_CONTEXT ROSV_EPT_CONTEXT, *PROSV_EPT_CONTEXT;
typedef struct _ROSV_CONSOLE_CONTEXT ROSV_CONSOLE_CONTEXT, *PROSV_CONSOLE_CONTEXT;
typedef struct _ROSV_PTY_STATE ROSV_PTY_STATE, *PROSV_PTY_STATE;
typedef struct _ROSV_PTY_MANAGER ROSV_PTY_MANAGER, *PROSV_PTY_MANAGER;
