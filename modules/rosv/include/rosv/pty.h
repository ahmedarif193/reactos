/*
 * PROJECT:     ReactOS VMX Hypervisor Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     PTY/PTS emulation structures and interfaces
 * COPYRIGHT:   Copyright 2025-2026 Ahmed Arif
 */

#pragma once

#include <rosv/rosv.h>

/* ---- PTY constants ------------------------------------------------------ */

#define ROSV_PTY_MAX            16      /* Max PTY pairs per VM */
#define ROSV_PTY_MAX_READERS    4       /* Max readers per PTY output buffer */
#define ROSV_PTY_INPUT_SIZE     4096    /* Input ring buffer (host -> guest) */
#define ROSV_PTY_OUTPUT_SIZE    16384   /* Output ring buffer (guest -> host) */
#define ROSV_PTY_LINE_MAX       4096    /* Max canonical line length */

/* ---- Linux-compatible termios definitions ------------------------------- */

#define ROSV_NCCS               32      /* Number of control characters */

/* Input flags (c_iflag) */
#define ROSV_IGNBRK     0x00001
#define ROSV_BRKINT     0x00002
#define ROSV_IGNPAR     0x00004
#define ROSV_PARMRK     0x00008
#define ROSV_INPCK      0x00010
#define ROSV_ISTRIP     0x00020
#define ROSV_INLCR      0x00040
#define ROSV_IGNCR      0x00080
#define ROSV_ICRNL      0x00100
#define ROSV_IUCLC      0x00200
#define ROSV_IXON       0x00400
#define ROSV_IXANY      0x00800
#define ROSV_IXOFF      0x01000
#define ROSV_IMAXBEL    0x02000
#define ROSV_IUTF8      0x04000

/* Output flags (c_oflag) */
#define ROSV_OPOST      0x00001
#define ROSV_OLCUC      0x00002
#define ROSV_ONLCR      0x00004
#define ROSV_OCRNL      0x00008
#define ROSV_ONOCR      0x00010
#define ROSV_ONLRET     0x00020
#define ROSV_OFILL      0x00040
#define ROSV_OFDEL      0x00080

/* Control flags (c_cflag) */
#define ROSV_CSIZE      0x00030
#define ROSV_CS5        0x00000
#define ROSV_CS6        0x00010
#define ROSV_CS7        0x00020
#define ROSV_CS8        0x00030
#define ROSV_CSTOPB     0x00040
#define ROSV_CREAD      0x00080
#define ROSV_PARENB     0x00100
#define ROSV_PARODD     0x00200
#define ROSV_HUPCL      0x00400
#define ROSV_CLOCAL     0x00800

/* Local flags (c_lflag) */
#define ROSV_ISIG       0x00001
#define ROSV_ICANON     0x00002
#define ROSV_XCASE      0x00004
#define ROSV_ECHO       0x00008
#define ROSV_ECHOE      0x00010
#define ROSV_ECHOK      0x00020
#define ROSV_ECHONL     0x00040
#define ROSV_NOFLSH     0x00080
#define ROSV_TOSTOP     0x00100
#define ROSV_ECHOCTL    0x00200
#define ROSV_ECHOPRT    0x00400
#define ROSV_ECHOKE     0x00800
#define ROSV_IEXTEN     0x08000

/* Control character indices (c_cc) */
#define ROSV_VINTR      0
#define ROSV_VQUIT      1
#define ROSV_VERASE     2
#define ROSV_VKILL      3
#define ROSV_VEOF       4
#define ROSV_VTIME      5
#define ROSV_VMIN       6
#define ROSV_VSWTC      7
#define ROSV_VSTART     8
#define ROSV_VSTOP      9
#define ROSV_VSUSP      10
#define ROSV_VEOL       11
#define ROSV_VREPRINT   12
#define ROSV_VDISCARD   13
#define ROSV_VWERASE    14
#define ROSV_VLNEXT     15
#define ROSV_VEOL2      16

/* Default control character values */
#define ROSV_CTRL(c)    ((c) & 0x1F)

/* Linux signal numbers */
#define ROSV_SIGHUP     1
#define ROSV_SIGINT     2
#define ROSV_SIGQUIT    3
#define ROSV_SIGKILL    9
#define ROSV_SIGTERM    15
#define ROSV_SIGTSTP    20
#define ROSV_SIGCONT    18
#define ROSV_SIGWINCH   28

/* tcsetattr actions */
#define ROSV_TCSANOW    0
#define ROSV_TCSADRAIN  1
#define ROSV_TCSAFLUSH  2

/* tcflush queue selectors */
#define ROSV_TCIFLUSH   0
#define ROSV_TCOFLUSH   1
#define ROSV_TCIOFLUSH  2

/* ---- PTY structures ----------------------------------------------------- */

typedef struct _ROSV_TERMIOS {
    ULONG c_iflag;                      /* Input mode flags */
    ULONG c_oflag;                      /* Output mode flags */
    ULONG c_cflag;                      /* Control mode flags */
    ULONG c_lflag;                      /* Local mode flags */
    UCHAR c_cc[ROSV_NCCS];             /* Control characters */
    ULONG c_ispeed;                     /* Input baud rate */
    ULONG c_ospeed;                     /* Output baud rate */
} ROSV_TERMIOS, *PROSV_TERMIOS;

typedef struct _ROSV_WINSIZE {
    USHORT ws_row;                      /* Rows (in characters) */
    USHORT ws_col;                      /* Columns (in characters) */
    USHORT ws_xpixel;                   /* Horizontal size (pixels, unused) */
    USHORT ws_ypixel;                   /* Vertical size (pixels, unused) */
} ROSV_WINSIZE, *PROSV_WINSIZE;

/* ---- PTY state (per PTY pair) ------------------------------------------- */

typedef enum _ROSV_PTY_FLAGS {
    RosvPtyFlagNone       = 0x00,
    RosvPtyFlagAllocated  = 0x01,       /* PTY slot is in use */
    RosvPtyFlagMasterOpen = 0x02,       /* Host has master side open */
    RosvPtyFlagSlaveOpen  = 0x04,       /* Guest has slave side open */
    RosvPtyFlagNonBlock   = 0x08,       /* Non-blocking I/O mode */
    RosvPtyFlagPacket     = 0x10,       /* Packet mode (TIOCPKT) */
} ROSV_PTY_FLAGS;

/*
 * Per-reader cursor for the shared output ring buffer.
 * Each reader independently tracks its position in OutputBuf.
 * The output buffer is write-once, read-many: data stays until
 * all active readers have consumed it (tracked via MinReadCursor).
 */
typedef struct _ROSV_PTY_READER {
    BOOLEAN Active;                     /* Slot is in use */
    ULONG64 ReadCursor;                 /* Monotonic read position, modulo OUTPUT_SIZE for indexing */
} ROSV_PTY_READER, *PROSV_PTY_READER;

typedef struct _ROSV_PTY_STATE {
    /* Identity */
    ULONG PtyIndex;                     /* pts/N number */
    ROSV_PTY_FLAGS Flags;

    /* Terminal settings */
    ROSV_TERMIOS Termios;
    ROSV_WINSIZE Winsize;

    /* Session / process group tracking */
    ULONG SessionId;                    /* Guest session leader PID */
    ULONG ForegroundPgid;              /* Guest foreground process group */

    /* Input buffer: host -> termios processing -> guest reads */
    UCHAR InputBuf[ROSV_PTY_INPUT_SIZE];
    ULONG InputHead;
    ULONG InputTail;
    ULONG InputCount;

    /* Canonical line editing buffer (only used when ICANON is set) */
    UCHAR LineBuf[ROSV_PTY_LINE_MAX];
    ULONG LineLen;

    /* Output buffer: guest writes -> output processing -> host reads.
     * Shared ring buffer with per-reader cursors for fan-out.
     * OutputHead = next write sequence number.
     * OutputTail = minimum of all active reader cursors (data before this is reclaimable).
     * OutputCount = number of bytes between OutputTail and OutputHead. */
    UCHAR OutputBuf[ROSV_PTY_OUTPUT_SIZE];
    ULONG64 OutputHead;
    ULONG64 OutputTail;
    ULONG OutputCount;

    /* Per-reader cursors for fan-out reads */
    ROSV_PTY_READER Readers[ROSV_PTY_MAX_READERS];
    ULONG ActiveReaderCount;

    /* Flow control */
    BOOLEAN OutputStopped;              /* XOFF received (IXON) */
    BOOLEAN InputStopped;              /* XOFF sent (IXOFF) */

    /* Bound virtio-console port for transport (replaces UART splice).
     * When BoundVconPort != (ULONG)-1, PTY I/O is bridged through the
     * vcon port instead of the UART, enabling per-client independent PTY. */
    ULONG BoundVconPort;                /* (ULONG)-1 if unbound */
    struct _ROSV_VIRTIO_CON_STATE *BoundVconState; /* Pointer to VirtioCon device */

    /* Synchronization */
    KSPIN_LOCK Lock;

    /* Pending signal for guest (0 = none) */
    ULONG PendingSignal;

    /* Statistics */
    ULONG64 BytesWrittenToGuest;
    ULONG64 BytesReadFromGuest;
} ROSV_PTY_STATE, *PROSV_PTY_STATE;

/* ---- PTY manager (per-VM) ----------------------------------------------- */

typedef struct _ROSV_PTY_MANAGER {
    ROSV_PTY_STATE Ptys[ROSV_PTY_MAX];
    ULONG AllocatedCount;
    KSPIN_LOCK Lock;
} ROSV_PTY_MANAGER, *PROSV_PTY_MANAGER;

/* ---- IOCTL request/response structures ---------------------------------- */

typedef struct _ROSV_PTY_CREATE_REQUEST {
    USHORT InitialRows;                 /* Initial window rows (0 = default 24) */
    USHORT InitialCols;                 /* Initial window cols (0 = default 80) */
    ULONG Flags;                        /* ROSV_PTY_FLAGS */
} ROSV_PTY_CREATE_REQUEST, *PROSV_PTY_CREATE_REQUEST;

typedef struct _ROSV_PTY_CREATE_RESULT {
    ULONG PtyIndex;                     /* Allocated pts/N index */
    ULONG ReaderIndex;                  /* Assigned reader slot (always 0 for creator) */
    ULONG VconPort;                     /* Bound vcon port for guest shell launch */
    NTSTATUS Status;
} ROSV_PTY_CREATE_RESULT, *PROSV_PTY_CREATE_RESULT;

/* PTY_ATTACH: second reader attaches to an existing PTY */
typedef struct _ROSV_PTY_ATTACH_REQUEST {
    ULONG PtyIndex;                     /* Which PTY to attach to */
} ROSV_PTY_ATTACH_REQUEST, *PROSV_PTY_ATTACH_REQUEST;

typedef struct _ROSV_PTY_ATTACH_RESULT {
    ULONG PtyIndex;                     /* Echoed back */
    ULONG ReaderIndex;                  /* Assigned reader slot */
    NTSTATUS Status;
} ROSV_PTY_ATTACH_RESULT, *PROSV_PTY_ATTACH_RESULT;

/* PTY_DETACH: release a reader from an existing PTY */
typedef struct _ROSV_PTY_DETACH_REQUEST {
    ULONG PtyIndex;                     /* Which PTY to detach from */
    ULONG ReaderIndex;                  /* Which reader slot to release */
} ROSV_PTY_DETACH_REQUEST, *PROSV_PTY_DETACH_REQUEST;

/* PTY_READ input: identifies which PTY and which reader cursor to use */
typedef struct _ROSV_PTY_READ_REQUEST {
    ULONG PtyIndex;                     /* Which PTY */
    ULONG ReaderIndex;                  /* Which reader cursor */
} ROSV_PTY_READ_REQUEST, *PROSV_PTY_READ_REQUEST;

typedef struct _ROSV_PTY_IO_REQUEST {
    ULONG PtyIndex;                     /* Which PTY */
    ULONG DataLength;                   /* Bytes in Data[] */
    UCHAR Data[1];                      /* Variable-length data (ANYSIZE_ARRAY) */
} ROSV_PTY_IO_REQUEST, *PROSV_PTY_IO_REQUEST;

typedef struct _ROSV_PTY_IO_RESULT {
    ULONG BytesTransferred;
    NTSTATUS Status;
    UCHAR Data[1];                      /* Variable-length data (ANYSIZE_ARRAY) */
} ROSV_PTY_IO_RESULT, *PROSV_PTY_IO_RESULT;

typedef struct _ROSV_PTY_RESIZE_REQUEST {
    ULONG PtyIndex;
    ROSV_WINSIZE Winsize;
} ROSV_PTY_RESIZE_REQUEST, *PROSV_PTY_RESIZE_REQUEST;

typedef struct _ROSV_PTY_TERMIOS_REQUEST {
    ULONG PtyIndex;
    ULONG Action;                       /* ROSV_TCSANOW, TCSADRAIN, TCSAFLUSH */
    ROSV_TERMIOS Termios;
} ROSV_PTY_TERMIOS_REQUEST, *PROSV_PTY_TERMIOS_REQUEST;

typedef struct _ROSV_PTY_SIGNAL_REQUEST {
    ULONG PtyIndex;
    ULONG Signal;                       /* ROSV_SIGINT, ROSV_SIGTSTP, etc. */
} ROSV_PTY_SIGNAL_REQUEST, *PROSV_PTY_SIGNAL_REQUEST;

typedef struct _ROSV_PTY_FLUSH_REQUEST {
    ULONG PtyIndex;
    ULONG Queue;                        /* ROSV_TCIFLUSH, TCOFLUSH, TCIOFLUSH */
} ROSV_PTY_FLUSH_REQUEST, *PROSV_PTY_FLUSH_REQUEST;

typedef struct _ROSV_PTY_INFO {
    ULONG PtyIndex;
    ROSV_PTY_FLAGS Flags;
    ROSV_WINSIZE Winsize;
    ULONG SessionId;
    ULONG ForegroundPgid;
    ULONG64 BytesWrittenToGuest;
    ULONG64 BytesReadFromGuest;
} ROSV_PTY_INFO, *PROSV_PTY_INFO;

/* ---- Session management structures -------------------------------------- */

typedef enum _ROSV_SESSION_STATE {
    RosvSessionCreated = 0,
    RosvSessionStarting,
    RosvSessionRunning,
    RosvSessionSuspended,
    RosvSessionStopping,
    RosvSessionStopped,
    RosvSessionDestroyed
} ROSV_SESSION_STATE;

typedef struct _ROSV_SESSION_CREATE_REQUEST {
    ULONG RamSizeMB;                   /* VM RAM size */
    USHORT InitialRows;                /* Terminal rows */
    USHORT InitialCols;                /* Terminal cols */
    ULONG Flags;
} ROSV_SESSION_CREATE_REQUEST, *PROSV_SESSION_CREATE_REQUEST;

typedef struct _ROSV_SESSION_CREATE_RESULT {
    ULONG SessionId;
    ULONG PtyIndex;                    /* Primary PTY allocated */
    NTSTATUS Status;
} ROSV_SESSION_CREATE_RESULT, *PROSV_SESSION_CREATE_RESULT;

typedef struct _ROSV_SESSION_INFO {
    ULONG SessionId;
    ROSV_SESSION_STATE State;
    ULONG PrimaryPtyIndex;
    ULONG AttachCount;
    LARGE_INTEGER CreateTime;
    LARGE_INTEGER Uptime;
} ROSV_SESSION_INFO, *PROSV_SESSION_INFO;

typedef struct _ROSV_SESSION_LIST_RESULT {
    ULONG Count;
    ROSV_SESSION_INFO Sessions[1];      /* Variable-length array */
} ROSV_SESSION_LIST_RESULT, *PROSV_SESSION_LIST_RESULT;

/* ---- Filesystem mount structures ---------------------------------------- */

#define ROSV_MOUNT_PATH_MAX     260
#define ROSV_MOUNT_MAX          16

typedef struct _ROSV_FS_MOUNT_REQUEST {
    WCHAR HostPath[ROSV_MOUNT_PATH_MAX];   /* Host directory to share */
    CHAR GuestMountPoint[ROSV_MOUNT_PATH_MAX]; /* Guest mount point (e.g. "/mnt/c") */
    ULONG Flags;                           /* Read-only, etc. */
} ROSV_FS_MOUNT_REQUEST, *PROSV_FS_MOUNT_REQUEST;

#define ROSV_FS_MOUNT_FLAG_READONLY  0x01

typedef struct _ROSV_FS_MOUNT_INFO {
    WCHAR HostPath[ROSV_MOUNT_PATH_MAX];
    CHAR GuestMountPoint[ROSV_MOUNT_PATH_MAX];
    ULONG Flags;
    BOOLEAN Active;
} ROSV_FS_MOUNT_INFO, *PROSV_FS_MOUNT_INFO;

typedef struct _ROSV_FS_MOUNT_LIST {
    ULONG Count;
    ROSV_FS_MOUNT_INFO Mounts[ROSV_MOUNT_MAX];
} ROSV_FS_MOUNT_LIST, *PROSV_FS_MOUNT_LIST;

/* ---- Network port forwarding structures --------------------------------- */

#define ROSV_PORT_FORWARD_MAX   32

typedef struct _ROSV_NET_FORWARD_REQUEST {
    USHORT HostPort;
    USHORT GuestPort;
    ULONG Protocol;                     /* 6=TCP, 17=UDP */
} ROSV_NET_FORWARD_REQUEST, *PROSV_NET_FORWARD_REQUEST;

/* ---- Function prototypes (device/pty.c) --------------------------------- */

NTSTATUS
RosvPtyManagerInitialize(
    _Out_ PROSV_PTY_MANAGER Manager);

VOID
RosvPtyManagerDestroy(
    _Inout_ PROSV_PTY_MANAGER Manager);

NTSTATUS
RosvPtyCreate(
    _Inout_ PROSV_PTY_MANAGER Manager,
    _In_ USHORT InitialRows,
    _In_ USHORT InitialCols,
    _Out_ PULONG PtyIndex,
    _Out_ PULONG ReaderIndex);

NTSTATUS
RosvPtyAttachReader(
    _Inout_ PROSV_PTY_MANAGER Manager,
    _In_ ULONG PtyIndex,
    _Out_ PULONG ReaderIndex);

NTSTATUS
RosvPtyDetachReader(
    _Inout_ PROSV_PTY_MANAGER Manager,
    _In_ ULONG PtyIndex,
    _In_ ULONG ReaderIndex);

NTSTATUS
RosvPtyDestroy(
    _Inout_ PROSV_PTY_MANAGER Manager,
    _In_ ULONG PtyIndex);

NTSTATUS
RosvPtyMasterWrite(
    _Inout_ PROSV_PTY_STATE Pty,
    _In_reads_(Length) const UCHAR *Data,
    _In_ ULONG Length,
    _Out_ PULONG BytesWritten);

NTSTATUS
RosvPtyMasterRead(
    _Inout_ PROSV_PTY_STATE Pty,
    _In_ ULONG ReaderIndex,
    _Out_writes_(BufferSize) UCHAR *Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesRead);

NTSTATUS
RosvPtySlaveWrite(
    _Inout_ PROSV_PTY_STATE Pty,
    _In_reads_(Length) const UCHAR *Data,
    _In_ ULONG Length,
    _Out_ PULONG BytesWritten);

/* Raw output: bypass OPOST processing, used for UART splice */
NTSTATUS
RosvPtySlaveWriteRaw(
    _Inout_ PROSV_PTY_STATE Pty,
    _In_reads_(Length) const UCHAR *Data,
    _In_ ULONG Length,
    _Out_ PULONG BytesWritten);

NTSTATUS
RosvPtySlaveRead(
    _Inout_ PROSV_PTY_STATE Pty,
    _Out_writes_(BufferSize) UCHAR *Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesRead);

NTSTATUS
RosvPtySetTermios(
    _Inout_ PROSV_PTY_STATE Pty,
    _In_ ULONG Action,
    _In_ const ROSV_TERMIOS *Termios);

NTSTATUS
RosvPtyGetTermios(
    _In_ PROSV_PTY_STATE Pty,
    _Out_ PROSV_TERMIOS Termios);

NTSTATUS
RosvPtySetWinsize(
    _Inout_ PROSV_PTY_STATE Pty,
    _In_ const ROSV_WINSIZE *Winsize);

NTSTATUS
RosvPtyGetWinsize(
    _In_ PROSV_PTY_STATE Pty,
    _Out_ PROSV_WINSIZE Winsize);

NTSTATUS
RosvPtyFlush(
    _Inout_ PROSV_PTY_STATE Pty,
    _In_ ULONG Queue);

NTSTATUS
RosvPtySendSignal(
    _Inout_ PROSV_PTY_STATE Pty,
    _In_ ULONG Signal);

NTSTATUS
RosvPtyGetInfo(
    _In_ PROSV_PTY_STATE Pty,
    _Out_ PROSV_PTY_INFO Info);

/* ---- PTY-Vcon bridge functions ------------------------------------------ */

/* Drain PTY InputBuf into bound vcon port TxBuf (host→guest via vcon) */
VOID
RosvPtyPumpToVcon(
    _Inout_ PROSV_PTY_STATE Pty);

/* Drain vcon port RxBuf into PTY OutputBuf (guest→host via vcon) */
VOID
RosvVconPumpToPty(
    _Inout_ PROSV_PTY_STATE Pty);

/* Pump all vcon-bound PTYs bidirectionally */
VOID
RosvPtyPumpAllVconBound(
    _Inout_ PROSV_PTY_MANAGER Manager);

/* ---- Termios processing helpers ----------------------------------------- */

VOID
RosvPtyTermiosSetDefaults(
    _Out_ PROSV_TERMIOS Termios);

ULONG
RosvPtyTermiosProcessInput(
    _Inout_ PROSV_PTY_STATE Pty,
    _In_ UCHAR Ch);

ULONG
RosvPtyTermiosProcessOutput(
    _Inout_ PROSV_PTY_STATE Pty,
    _In_ UCHAR Ch,
    _Out_writes_(MaxOut) UCHAR *OutBuf,
    _In_ ULONG MaxOut);
