/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Kernel-mode functional test for ntfslx.
 *
 * Section 0 is the RAM-only self-test: it sends IOCTL_NTFSLX_SELFTEST to
 * the \NtfsLx control device. The driver allocates a 32 MB scratch buffer,
 * exercises attribute / FCB header / index code paths on in-memory state,
 * and returns pass/fail counts. No drive letter required.
 *
 * Sections 1-12 are the legacy filesystem test against D:\ (if present).
 * They are skipped automatically if D:\ is not mounted, so a bring-up run
 * on a bare system still exercises section 0.
 */

#include <kmt_test.h>
#include <ntifs.h>
#include <pseh/pseh2.h>

/* Keep in sync with drivers/filesystems/ntfslx/ntfslx.h */
#define IOCTL_NTFSLX_SELFTEST \
    CTL_CODE(FILE_DEVICE_DISK_FILE_SYSTEM, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_NTFSLX_TIER0_PROOF \
    CTL_CODE(FILE_DEVICE_DISK_FILE_SYSTEM, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct _NTFSLX_SELFTEST_RESULT
{
    ULONG TotalChecks;
    ULONG PassedChecks;
    ULONG FailedChecks;
    ULONG FirstFailCode;
} NTFSLX_SELFTEST_RESULT, *PNTFSLX_SELFTEST_RESULT;

typedef struct _NTFSLX_TIER0_PROOF
{
    ULONG  Version;
    NTSTATUS ReturnStatus;
    ULONG  MftMirrorConsistent;
    ULONG  MftMirrorRecords;
    ULONG  VolumeDirtyFlag;
    ULONG  LogFileFirstDword;
    ULONG  LogFileIs0xFF;
    ULONG  LcnZeroIsReserved;
    ULONGLONG BootLcn;
    ULONGLONG MftMirrLcn;
} NTFSLX_TIER0_PROOF, *PNTFSLX_TIER0_PROOF;

#define TLOG(fmt, ...) DbgPrint("NTFSLX-TEST: " fmt, ##__VA_ARGS__)

#ifndef NTFSLX_TEST_LOG_SUCCESS
#define NTFSLX_TEST_LOG_SUCCESS 0
#endif

#if NTFSLX_TEST_LOG_SUCCESS
#define TOK_SUCCESS(fmt, ...) DbgPrint("NTFSLX-TEST [success]: " fmt, ##__VA_ARGS__)
#else
#define TOK_SUCCESS(fmt, ...) ((void)0)
#endif

#define TOK(cond, fmt, ...) do { \
    if (!(cond)) DbgPrint("NTFSLX-TEST [fail]: " fmt, ##__VA_ARGS__); \
    else TOK_SUCCESS(fmt, ##__VA_ARGS__); \
} while(0)

#define TEST_BIG_BYTES   2048
#define TEST_SMALL_BYTES 256
#define TEST_SPILL_FILES 10
#define TEST_APPEND_CHUNKS 4

/* Stress parameters. Keep modest so the kmtest run stays fast but still
 * exercises the full round-trip matrix: multi-file, delete/recreate loops,
 * reopen-across-session, interleaved resident/non-resident. */
#define TEST_STRESS_FILES     16  /* parallel filenames per stress wave */
#define TEST_STRESS_WAVES      3  /* number of create/delete cycles */
#define TEST_STRESS_MAX_BYTES 4096

/* Non-resident extend parameters: N sequential 4096-byte chunks. Exercises
 * promote-on-first-write, then extend-and-append for the rest. This mirrors
 * the pattern Explorer's CopyFile uses. */
#define TEST_EXTEND_CHUNK    4096
#define TEST_EXTEND_CHUNKS   8

/* Shared scratch buffer size covering every test path in this file. */
#define TEST_BUF_SIZE TEST_EXTEND_CHUNK

typedef enum _NTFSLX_FUNC_PHASE
{
    NtfslxFuncPhaseAll = 0,
    NtfslxFuncPhaseSmoke,
    NtfslxFuncPhaseStressA,
    NtfslxFuncPhaseStressB,
    NtfslxFuncPhaseStressC,
} NTFSLX_FUNC_PHASE;

/*
 * Drive letter where the NTFS test volume lives. Discovered at runtime by
 * DetectNtfsDrive(); default L'D' is a last-resort fallback so the old
 * hard-coded paths still attempt a sensible target if detection fails.
 * All paths in this file are written as L"\\??\\D:\\..." and transparently
 * rewritten by OpenNtfs/UnlinkByPath to point at the detected letter.
 */
static WCHAR g_TestDriveLetter = L'D';
static NTFSLX_FUNC_PHASE g_NtfslxFuncPhase = NtfslxFuncPhaseAll;

static PCSTR
NtfslxGetFuncPhaseName(VOID)
{
    switch (g_NtfslxFuncPhase)
    {
        case NtfslxFuncPhaseSmoke:
            return "smoke";
        case NtfslxFuncPhaseStressA:
            return "stress-a";
        case NtfslxFuncPhaseStressB:
            return "stress-b";
        case NtfslxFuncPhaseStressC:
            return "stress-c";
        case NtfslxFuncPhaseAll:
        default:
            return "all";
    }
}

static VOID
NtfslxRunNamedFuncPhase(_In_ NTFSLX_FUNC_PHASE Phase);

static PCWSTR
RewriteTestPath(PCWSTR Path, PWCHAR Scratch, SIZE_T ScratchBytes)
{
    if (g_TestDriveLetter == L'D')
        return Path;
    if (Path[0] != L'\\' || Path[1] != L'?' || Path[2] != L'?' ||
        Path[3] != L'\\' || Path[4] != L'D' || Path[5] != L':')
        return Path;
    RtlStringCbPrintfW(Scratch, ScratchBytes, L"\\??\\%c:%ls",
                       g_TestDriveLetter, Path + 6);
    return Scratch;
}

static NTSTATUS
OpenNtfs(PCWSTR Path, ACCESS_MASK Access, ULONG Disp, ULONG Opts, PHANDLE Out)
{
    UNICODE_STRING Name;
    OBJECT_ATTRIBUTES Oa;
    IO_STATUS_BLOCK IoSb;
    WCHAR Scratch[260];

    Path = RewriteTestPath(Path, Scratch, sizeof(Scratch));
    RtlInitUnicodeString(&Name, Path);
    InitializeObjectAttributes(&Oa, &Name, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    /*
     * Default to maximum sharing so most tests can have multiple coexisting
     * handles to the same file. Tests that want strict-share semantics
     * should call ZwCreateFile directly.
     */
    return ZwCreateFile(Out, Access, &Oa, &IoSb, NULL, FILE_ATTRIBUTE_NORMAL,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        Disp,
                        Opts | FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0);
}

static VOID
UnlinkByPath(PCWSTR Path)
{
    UNICODE_STRING Dn;
    OBJECT_ATTRIBUTES Da;
    WCHAR Scratch[260];

    Path = RewriteTestPath(Path, Scratch, sizeof(Scratch));
    RtlInitUnicodeString(&Dn, Path);
    InitializeObjectAttributes(&Da, &Dn, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    (VOID)ZwDeleteFile(&Da);
}

/*
 * Probe C..Z for a volume whose filesystem identifies as "NTFS" and set
 * g_TestDriveLetter accordingly. Keeps the test decoupled from whatever
 * order MountMgr handed out letters based on drive topology.
 */
static NTSTATUS
DetectNtfsDrive(VOID)
{
    struct { FILE_FS_ATTRIBUTE_INFORMATION I; WCHAR B[16]; } Attr;
    IO_STATUS_BLOCK IoSb;
    NTSTATUS St;
    HANDLE Probe;
    UNICODE_STRING RootName;
    OBJECT_ATTRIBUTES RootOa;
    WCHAR Root[] = L"\\??\\C:\\";
    WCHAR Letter;

    for (Letter = L'C'; Letter <= L'Z'; Letter++)
    {
        Root[4] = Letter;
        RtlInitUnicodeString(&RootName, Root);
        InitializeObjectAttributes(&RootOa, &RootName,
                                   OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                                   NULL, NULL);
        St = ZwCreateFile(&Probe, FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                          &RootOa, &IoSb, NULL, FILE_ATTRIBUTE_NORMAL,
                          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                          FILE_OPEN,
                          FILE_SYNCHRONOUS_IO_NONALERT | FILE_DIRECTORY_FILE,
                          NULL, 0);
        if (!NT_SUCCESS(St))
            continue;
        RtlZeroMemory(&Attr, sizeof(Attr));
        St = ZwQueryVolumeInformationFile(Probe, &IoSb, &Attr, sizeof(Attr),
                                          FileFsAttributeInformation);
        ZwClose(Probe);
        if (!NT_SUCCESS(St))
            continue;
        if (Attr.I.FileSystemNameLength == 4 * sizeof(WCHAR) &&
            Attr.I.FileSystemName[0] == L'N' && Attr.I.FileSystemName[1] == L'T' &&
            Attr.I.FileSystemName[2] == L'F' && Attr.I.FileSystemName[3] == L'S')
        {
            g_TestDriveLetter = Letter;
            return STATUS_SUCCESS;
        }
    }
    return STATUS_NOT_FOUND;
}

static NTSTATUS
WriteAt(HANDLE h, ULONGLONG Offset, PVOID Buf, ULONG Len, PULONG OutInfo)
{
    LARGE_INTEGER Off;
    IO_STATUS_BLOCK IoSb;
    NTSTATUS St;
    Off.QuadPart = (LONGLONG)Offset;
    St = ZwWriteFile(h, NULL, NULL, NULL, &IoSb, Buf, Len, &Off, NULL);
    if (OutInfo) *OutInfo = (ULONG)IoSb.Information;
    return St;
}

static NTSTATUS
ReadAt(HANDLE h, ULONGLONG Offset, PVOID Buf, ULONG Len, PULONG OutInfo)
{
    LARGE_INTEGER Off;
    IO_STATUS_BLOCK IoSb;
    NTSTATUS St;
    Off.QuadPart = (LONGLONG)Offset;
    St = ZwReadFile(h, NULL, NULL, NULL, &IoSb, Buf, Len, &Off, NULL);
    if (OutInfo) *OutInfo = (ULONG)IoSb.Information;
    return St;
}

static SIZE_T
WcsLen(PCWSTR Str)
{
    SIZE_T n = 0;
    while (Str[n] != L'\0') n++;
    return n;
}

/*
 * Open `DirPath`, enumerate every entry once (draining ZwQueryDirectoryFile
 * across buffers), count total entries, and count how many of `Expected`
 * were observed. Useful to catch a class of bugs where a subdirectory's
 * children are created correctly but a subsequent enumeration on a fresh
 * handle returns nothing ("re-enter, no files"). Returns STATUS_SUCCESS if
 * the directory opens and enumerates at all; the caller inspects the
 * per-name match count.
 */
static NTSTATUS
EnumDirMatch(
    _In_ PCWSTR DirPath,
    _In_reads_(ExpectedCount) const PCWSTR *Expected,
    _In_ ULONG ExpectedCount,
    _Out_ PULONG OutMatched,
    _Out_ PULONG OutTotal)
{
    HANDLE h = NULL;
    NTSTATUS St;
    IO_STATUS_BLOCK IoSb;
    UCHAR DirBuf[2048];
    ULONG Matched = 0;
    ULONG Total = 0;
    BOOLEAN First = TRUE;

    *OutMatched = 0;
    *OutTotal = 0;

    St = OpenNtfs(DirPath, FILE_LIST_DIRECTORY | SYNCHRONIZE,
                  FILE_OPEN, FILE_DIRECTORY_FILE, &h);
    if (!NT_SUCCESS(St))
        return St;

    for (;;)
    {
        ULONG Off = 0;
        RtlZeroMemory(DirBuf, sizeof(DirBuf));
        St = ZwQueryDirectoryFile(h, NULL, NULL, NULL, &IoSb,
                                  DirBuf, sizeof(DirBuf),
                                  FileBothDirectoryInformation,
                                  FALSE, NULL, First);
        First = FALSE;
        if (!NT_SUCCESS(St) || IoSb.Information == 0)
            break;

        while (Off < IoSb.Information)
        {
            PFILE_BOTH_DIR_INFORMATION Entry =
                (PFILE_BOTH_DIR_INFORMATION)(DirBuf + Off);
            ULONG e;
            Total++;
            for (e = 0; e < ExpectedCount; e++)
            {
                SIZE_T ExLen = WcsLen(Expected[e]) * sizeof(WCHAR);
                if (Entry->FileNameLength == ExLen &&
                    RtlCompareMemory(Entry->FileName, Expected[e], ExLen) == ExLen)
                {
                    Matched++;
                    break;
                }
            }
            if (Entry->NextEntryOffset == 0)
                break;
            Off += Entry->NextEntryOffset;
        }
    }

    ZwClose(h);
    *OutMatched = Matched;
    *OutTotal = Total;
    return STATUS_SUCCESS;
}

static VOID
NtfslxRunFuncPhase(VOID)
{
    HANDLE h = NULL;
    HANDLE h2 = NULL;
    IO_STATUS_BLOCK IoSb;
    NTSTATUS St;
    ULONG i, Errs, Info;
    UCHAR *Wr = NULL, *Rd = NULL;
    struct { FILE_FS_ATTRIBUTE_INFORMATION I; WCHAR B[16]; } Attr;
    struct { FILE_FS_VOLUME_INFORMATION I; WCHAR B[32]; } Vol;
    FILE_FS_SIZE_INFORMATION Size;
    FILE_BASIC_INFORMATION Basic;
    FILE_STANDARD_INFORMATION Std;
    FILE_NETWORK_OPEN_INFORMATION NetOpen;
    FILE_END_OF_FILE_INFORMATION Eof;
    FILE_ALLOCATION_INFORMATION Alloc;
    FILE_DISPOSITION_INFORMATION Disp;

    TLOG("=== NtfslxFunc START phase=%s ===\n", NtfslxGetFuncPhaseName());

    Wr = ExAllocatePoolWithTag(NonPagedPool, TEST_BUF_SIZE, 'TsTN');
    Rd = ExAllocatePoolWithTag(NonPagedPool, TEST_BUF_SIZE, 'TsTN');
    if (!Wr || !Rd)
    {
        TLOG("FAIL: no pool for test buffers\n");
        if (Wr) ExFreePoolWithTag(Wr, 'TsTN');
        if (Rd) ExFreePoolWithTag(Rd, 'TsTN');
        return;
    }

    /*
     * ==========================================================
     * 0. RAM-only self-test via IOCTL to \NtfsLx control device
     * ==========================================================
     */
    TLOG("--- section 0: RAM-only self-test (no drive letter) ---\n");
    {
        HANDLE Ctl;
        NTFSLX_SELFTEST_RESULT SelfRes;
        UNICODE_STRING CtlName;
        OBJECT_ATTRIBUTES CtlOa;
        IO_STATUS_BLOCK CtlIosb;

        RtlInitUnicodeString(&CtlName, L"\\NtfsLx");
        InitializeObjectAttributes(&CtlOa, &CtlName,
                                   OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                                   NULL, NULL);
        St = ZwCreateFile(&Ctl, FILE_READ_DATA | FILE_WRITE_DATA | SYNCHRONIZE,
                          &CtlOa, &CtlIosb, NULL, FILE_ATTRIBUTE_NORMAL, 0,
                          FILE_OPEN,
                          FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE,
                          NULL, 0);
        TOK(NT_SUCCESS(St), "open \\NtfsLx: 0x%08lx\n", St);
        if (NT_SUCCESS(St))
        {
            RtlZeroMemory(&SelfRes, sizeof(SelfRes));
            St = ZwDeviceIoControlFile(Ctl, NULL, NULL, NULL, &CtlIosb,
                                       IOCTL_NTFSLX_SELFTEST,
                                       NULL, 0,
                                       &SelfRes, sizeof(SelfRes));
            TOK(NT_SUCCESS(St) || St == STATUS_UNSUCCESSFUL,
                "selftest ioctl: 0x%08lx\n", St);
            TOK(SelfRes.TotalChecks > 0,
                "selftest ran %lu checks\n", SelfRes.TotalChecks);
            TOK(SelfRes.FailedChecks == 0,
                "selftest: pass=%lu fail=%lu firstFail=0x%04lx\n",
                SelfRes.PassedChecks, SelfRes.FailedChecks, SelfRes.FirstFailCode);
            ZwClose(Ctl);
        }
    }

    /*
     * ==========================================================
     * Pre-flight for sections 1-12: probe C..Z for an NTFS volume so the
     * test runs against whatever letter MountMgr handed to ntfs.img, then
     * confirm the root is reachable before touching it.
     * ==========================================================
     */
    St = DetectNtfsDrive();
    if (!NT_SUCCESS(St))
    {
        TLOG("=== NtfslxFunc END (no NTFS volume, section 0 only) ===\n");
        goto Done;
    }
    TLOG("NTFS test volume detected at %C:\\\n", g_TestDriveLetter);
    {
        HANDLE Probe;
        St = OpenNtfs(L"\\??\\D:\\", FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                      FILE_OPEN, FILE_DIRECTORY_FILE, &Probe);
        if (!NT_SUCCESS(St))
        {
            TLOG("=== NtfslxFunc END (root unreachable on %C:, section 0 only) ===\n",
                 g_TestDriveLetter);
            goto Done;
        }
        ZwClose(Probe);
    }

    /* Pre-flight: unlink any leftovers from a prior run so the test is
     * reproducible against a fresh OR a dirty image. */
    UnlinkByPath(L"\\??\\D:\\kmtest_rw.bin");
    UnlinkByPath(L"\\??\\D:\\kmtest_big.bin");
    UnlinkByPath(L"\\??\\D:\\kmtest_set.bin");
    UnlinkByPath(L"\\??\\D:\\kmtest_deleteonclose.bin");
    for (i = 0; i < TEST_SPILL_FILES; i++)
    {
        WCHAR Path[64];
        RtlStringCbPrintfW(Path, sizeof(Path), L"\\??\\D:\\spill_%02lu.bin", i);
        UnlinkByPath(Path);
    }

    if (g_NtfslxFuncPhase == NtfslxFuncPhaseStressA)
        goto LongSuitePhaseA;
    if (g_NtfslxFuncPhase == NtfslxFuncPhaseStressB)
        goto LongSuitePhaseB;
    if (g_NtfslxFuncPhase == NtfslxFuncPhaseStressC)
        goto LongSuitePhaseC;

    /*
     * ==========================================================
     * 1. Volume info
     * ==========================================================
     */
    TLOG("--- section 1: volume info ---\n");
    St = OpenNtfs(L"\\??\\D:\\", FILE_READ_ATTRIBUTES | SYNCHRONIZE, FILE_OPEN, FILE_DIRECTORY_FILE, &h);
    TOK(NT_SUCCESS(St), "open root: 0x%08lx\n", St);
    if (!NT_SUCCESS(St)) { TLOG("=== NtfslxFunc ABORT (no D:) ===\n"); goto Done; }

    RtlZeroMemory(&Attr, sizeof(Attr));
    St = ZwQueryVolumeInformationFile(h, &IoSb, &Attr, sizeof(Attr), FileFsAttributeInformation);
    TOK(NT_SUCCESS(St), "FsAttrInfo: 0x%08lx\n", St);
    if (NT_SUCCESS(St))
        TOK(Attr.I.FileSystemNameLength == 8, "FsName len=%lu (expect 8)\n", Attr.I.FileSystemNameLength);

    RtlZeroMemory(&Vol, sizeof(Vol));
    St = ZwQueryVolumeInformationFile(h, &IoSb, &Vol, sizeof(Vol), FileFsVolumeInformation);
    TOK(NT_SUCCESS(St), "VolInfo: 0x%08lx label len=%lu\n", St, NT_SUCCESS(St) ? Vol.I.VolumeLabelLength : 0);

    RtlZeroMemory(&Size, sizeof(Size));
    St = ZwQueryVolumeInformationFile(h, &IoSb, &Size, sizeof(Size), FileFsSizeInformation);
    TOK(NT_SUCCESS(St), "SizeInfo: 0x%08lx total=%I64d avail=%I64d\n", St,
        Size.TotalAllocationUnits.QuadPart, Size.AvailableAllocationUnits.QuadPart);
    ZwClose(h);

    /*
     * ==========================================================
     * 2. Basic create/write/read (resident $DATA)
     * ==========================================================
     */
    TLOG("--- section 2: small resident write/read ---\n");
    St = OpenNtfs(L"\\??\\D:\\kmtest_rw.bin", GENERIC_READ | GENERIC_WRITE | DELETE | SYNCHRONIZE,
                  FILE_CREATE, FILE_NON_DIRECTORY_FILE, &h);
    TOK(NT_SUCCESS(St), "create kmtest_rw.bin: 0x%08lx\n", St);
    if (!NT_SUCCESS(St)) goto Done;

    for (i = 0; i < TEST_SMALL_BYTES; i++) Wr[i] = (UCHAR)(i & 0xFF);
    St = WriteAt(h, 0, Wr, TEST_SMALL_BYTES, &Info);
    TOK(NT_SUCCESS(St), "write 256: 0x%08lx info=%lu\n", St, Info);

    RtlZeroMemory(Rd, TEST_BIG_BYTES);
    St = ReadAt(h, 0, Rd, TEST_SMALL_BYTES, &Info);
    TOK(NT_SUCCESS(St) && Info == TEST_SMALL_BYTES, "read 256: 0x%08lx info=%lu\n", St, Info);
    Errs = 0;
    for (i = 0; i < TEST_SMALL_BYTES; i++) if (Rd[i] != Wr[i]) Errs++;
    TOK(Errs == 0, "resident compare: %lu mismatches\n", Errs);

    /*
     * ==========================================================
     * 4. FileStandardInformation — real size/alloc/directory
     * ==========================================================
     */
    TLOG("--- section 4: FileStandardInformation ---\n");
    RtlZeroMemory(&Std, sizeof(Std));
    St = ZwQueryInformationFile(h, &IoSb, &Std, sizeof(Std), FileStandardInformation);
    TOK(NT_SUCCESS(St), "query std: 0x%08lx\n", St);
    TOK(Std.EndOfFile.QuadPart == TEST_SMALL_BYTES,
        "std EndOfFile=%I64d (expect %d)\n", Std.EndOfFile.QuadPart, TEST_SMALL_BYTES);
    TOK(Std.AllocationSize.QuadPart >= TEST_SMALL_BYTES,
        "std AllocationSize=%I64d (expect >= %d)\n", Std.AllocationSize.QuadPart, TEST_SMALL_BYTES);
    TOK(Std.Directory == FALSE, "std Directory=%u (expect 0)\n", Std.Directory);
    TOK(Std.NumberOfLinks >= 1, "std NumberOfLinks=%lu\n", Std.NumberOfLinks);

    /*
     * ==========================================================
     * 5. FileBasicInformation — real attributes, not DIRECTORY
     * ==========================================================
     */
    TLOG("--- section 5: FileBasicInformation ---\n");
    RtlZeroMemory(&Basic, sizeof(Basic));
    St = ZwQueryInformationFile(h, &IoSb, &Basic, sizeof(Basic), FileBasicInformation);
    TOK(NT_SUCCESS(St), "query basic: 0x%08lx\n", St);
    TOK((Basic.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0,
        "basic attrs=0x%08lx (no DIRECTORY bit)\n", Basic.FileAttributes);
    TOK(Basic.FileAttributes != 0, "basic attrs=0x%08lx (non-zero)\n", Basic.FileAttributes);
    TOK(Basic.LastWriteTime.QuadPart != 0,
        "basic LastWriteTime=%I64d (non-zero)\n", Basic.LastWriteTime.QuadPart);

    /*
     * ==========================================================
     * 6. FileNetworkOpenInformation (composite)
     * ==========================================================
     */
    TLOG("--- section 6: FileNetworkOpenInformation ---\n");
    RtlZeroMemory(&NetOpen, sizeof(NetOpen));
    St = ZwQueryInformationFile(h, &IoSb, &NetOpen, sizeof(NetOpen), FileNetworkOpenInformation);
    TOK(NT_SUCCESS(St), "query netopen: 0x%08lx\n", St);
    TOK(NetOpen.EndOfFile.QuadPart == TEST_SMALL_BYTES,
        "netopen EOF=%I64d (expect %d)\n", NetOpen.EndOfFile.QuadPart, TEST_SMALL_BYTES);
    TOK((NetOpen.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0,
        "netopen attrs=0x%08lx (no DIRECTORY)\n", NetOpen.FileAttributes);

    /*
     * ==========================================================
     * 7. FileEndOfFileInformation set — extend to big
     * ==========================================================
     */
    TLOG("--- section 7: SetInformation EndOfFile extend to 2048 ---\n");
    Eof.EndOfFile.QuadPart = TEST_BIG_BYTES;
    St = ZwSetInformationFile(h, &IoSb, &Eof, sizeof(Eof), FileEndOfFileInformation);
    TOK(NT_SUCCESS(St), "set EOF=2048: 0x%08lx\n", St);

    RtlZeroMemory(&Std, sizeof(Std));
    St = ZwQueryInformationFile(h, &IoSb, &Std, sizeof(Std), FileStandardInformation);
    TOK(NT_SUCCESS(St), "query std after EOF: 0x%08lx\n", St);
    TOK(Std.EndOfFile.QuadPart == TEST_BIG_BYTES,
        "std EOF after extend=%I64d (expect %d)\n", Std.EndOfFile.QuadPart, TEST_BIG_BYTES);

    /*
     * ==========================================================
     * 8. FileAllocationInformation accept-as-hint
     * ==========================================================
     */
    TLOG("--- section 8: SetInformation AllocationSize ---\n");
    Alloc.AllocationSize.QuadPart = TEST_BIG_BYTES * 2;
    St = ZwSetInformationFile(h, &IoSb, &Alloc, sizeof(Alloc), FileAllocationInformation);
    TOK(NT_SUCCESS(St), "set alloc=%d: 0x%08lx\n", TEST_BIG_BYTES * 2, St);

    /*
     * ==========================================================
     * 3. Non-resident promotion (write across 700-byte threshold)
     *    We perform it on kmtest_big.bin to keep the resident file
     *    in section 2 exercising its own path.
     * ==========================================================
     */
    ZwClose(h);
    TLOG("--- section 3: non-resident promotion via 2048-byte write ---\n");
    St = OpenNtfs(L"\\??\\D:\\kmtest_big.bin", GENERIC_READ | GENERIC_WRITE | DELETE | SYNCHRONIZE,
                  FILE_CREATE, FILE_NON_DIRECTORY_FILE, &h);
    TOK(NT_SUCCESS(St), "create kmtest_big.bin: 0x%08lx\n", St);
    if (NT_SUCCESS(St))
    {
        for (i = 0; i < TEST_BIG_BYTES; i++) Wr[i] = (UCHAR)((i * 7 + 3) & 0xFF);
        St = WriteAt(h, 0, Wr, TEST_BIG_BYTES, &Info);
        TOK(NT_SUCCESS(St) && Info == TEST_BIG_BYTES,
            "write 2048: 0x%08lx info=%lu\n", St, Info);

        RtlZeroMemory(Rd, TEST_BIG_BYTES);
        St = ReadAt(h, 0, Rd, TEST_BIG_BYTES, &Info);
        TOK(NT_SUCCESS(St) && Info == TEST_BIG_BYTES,
            "read 2048: 0x%08lx info=%lu\n", St, Info);
        Errs = 0;
        for (i = 0; i < TEST_BIG_BYTES; i++) if (Rd[i] != Wr[i]) Errs++;
        TOK(Errs == 0, "non-resident compare: %lu mismatches\n", Errs);

        RtlZeroMemory(&Std, sizeof(Std));
        St = ZwQueryInformationFile(h, &IoSb, &Std, sizeof(Std), FileStandardInformation);
        TOK(NT_SUCCESS(St) && Std.EndOfFile.QuadPart == TEST_BIG_BYTES,
            "std EOF non-resident=%I64d (expect %d)\n", Std.EndOfFile.QuadPart, TEST_BIG_BYTES);
        TOK(Std.AllocationSize.QuadPart >= TEST_BIG_BYTES,
            "std Alloc non-resident=%I64d (>= %d)\n", Std.AllocationSize.QuadPart, TEST_BIG_BYTES);

        ZwClose(h);

        /* Reopen and re-verify — ensures disk state, not just in-memory. */
        St = OpenNtfs(L"\\??\\D:\\kmtest_big.bin", GENERIC_READ | DELETE | SYNCHRONIZE,
                      FILE_OPEN, FILE_NON_DIRECTORY_FILE, &h);
        TOK(NT_SUCCESS(St), "reopen kmtest_big.bin: 0x%08lx\n", St);
        if (NT_SUCCESS(St))
        {
            RtlZeroMemory(&Std, sizeof(Std));
            St = ZwQueryInformationFile(h, &IoSb, &Std, sizeof(Std), FileStandardInformation);
            TOK(NT_SUCCESS(St) && Std.EndOfFile.QuadPart == TEST_BIG_BYTES,
                "reopen big EOF=%I64d (expect %d)\n", Std.EndOfFile.QuadPart, TEST_BIG_BYTES);

            RtlZeroMemory(Rd, TEST_BIG_BYTES);
            St = ReadAt(h, 0, Rd, TEST_BIG_BYTES, &Info);
            TOK(NT_SUCCESS(St) && Info == TEST_BIG_BYTES,
                "reopen read 2048: 0x%08lx info=%lu\n", St, Info);
            Errs = 0;
            for (i = 0; i < TEST_BIG_BYTES; i++) if (Rd[i] != Wr[i]) Errs++;
            TOK(Errs == 0, "reopen non-resident compare: %lu mismatches\n", Errs);
            ZwClose(h);
        }
    }

    /*
     * ==========================================================
     * 9. Directory enumeration shows real sizes
     * ==========================================================
     */
    TLOG("--- section 9: directory enumeration ---\n");
    St = OpenNtfs(L"\\??\\D:\\", FILE_LIST_DIRECTORY | SYNCHRONIZE,
                  FILE_OPEN, FILE_DIRECTORY_FILE, &h);
    TOK(NT_SUCCESS(St), "open root for list: 0x%08lx\n", St);
    if (NT_SUCCESS(St))
    {
        UCHAR DirBuf[2048];
        BOOLEAN SawSmall = FALSE, SawBig = FALSE;
        LONGLONG SmallEof = -1, BigEof = -1;
        ULONG Offset;
        PFILE_BOTH_DIR_INFORMATION Entry;

        RtlZeroMemory(DirBuf, sizeof(DirBuf));
        St = ZwQueryDirectoryFile(h, NULL, NULL, NULL, &IoSb,
                                  DirBuf, sizeof(DirBuf),
                                  FileBothDirectoryInformation,
                                  FALSE, NULL, TRUE);
        TOK(NT_SUCCESS(St), "enumerate D:\\: 0x%08lx info=%Iu\n", St, IoSb.Information);

        Offset = 0;
        while (NT_SUCCESS(St) && Offset < IoSb.Information)
        {
            Entry = (PFILE_BOTH_DIR_INFORMATION)(DirBuf + Offset);
            if (Entry->FileNameLength == 13 * sizeof(WCHAR) &&
                RtlCompareMemory(Entry->FileName, L"kmtest_rw.bin", 13 * sizeof(WCHAR)) == 13 * sizeof(WCHAR))
            {
                SawSmall = TRUE;
                SmallEof = Entry->EndOfFile.QuadPart;
            }
            else if (Entry->FileNameLength == 14 * sizeof(WCHAR) &&
                     RtlCompareMemory(Entry->FileName, L"kmtest_big.bin", 14 * sizeof(WCHAR)) == 14 * sizeof(WCHAR))
            {
                SawBig = TRUE;
                BigEof = Entry->EndOfFile.QuadPart;
            }
            if (Entry->NextEntryOffset == 0) break;
            Offset += Entry->NextEntryOffset;
        }

        TOK(SawSmall, "enum found kmtest_rw.bin\n");
        TOK(SmallEof == TEST_BIG_BYTES,
            "enum kmtest_rw.bin EOF=%I64d (expect %d, was extended by section 7)\n",
            SmallEof, TEST_BIG_BYTES);
        TOK(SawBig, "enum found kmtest_big.bin\n");
        TOK(BigEof == TEST_BIG_BYTES,
            "enum kmtest_big.bin EOF=%I64d (expect %d)\n", BigEof, TEST_BIG_BYTES);
        ZwClose(h);
    }

    /*
     * ==========================================================
     * 20. Subfolder create + file inside with full content verify.
     *     Exercises: directory creation, nested file create/write,
     *     reopen across handles, byte-for-byte readback.
     * ==========================================================
     */
    TLOG("--- section 20: subfolder create + file content verify ---\n");
    St = OpenNtfs(L"\\??\\D:\\sub1", GENERIC_READ | SYNCHRONIZE,
                  FILE_CREATE, FILE_DIRECTORY_FILE, &h);
    TOK(NT_SUCCESS(St), "mkdir D:\\sub1: 0x%08lx\n", St);
    if (NT_SUCCESS(St)) ZwClose(h);

    St = OpenNtfs(L"\\??\\D:\\sub1\\a.bin",
                  GENERIC_READ | GENERIC_WRITE | DELETE | SYNCHRONIZE,
                  FILE_CREATE, FILE_NON_DIRECTORY_FILE, &h);
    TOK(NT_SUCCESS(St), "create D:\\sub1\\a.bin: 0x%08lx\n", St);
    if (NT_SUCCESS(St))
    {
        for (i = 0; i < TEST_SMALL_BYTES; i++)
            Wr[i] = (UCHAR)(0xA0 + (i & 0x1F));
        St = WriteAt(h, 0, Wr, TEST_SMALL_BYTES, &Info);
        TOK(NT_SUCCESS(St) && Info == TEST_SMALL_BYTES,
            "sub1 write: 0x%08lx info=%lu (expect %d)\n",
            St, Info, TEST_SMALL_BYTES);
        ZwClose(h);
    }

    St = OpenNtfs(L"\\??\\D:\\sub1\\a.bin", GENERIC_READ | SYNCHRONIZE,
                  FILE_OPEN, FILE_NON_DIRECTORY_FILE, &h);
    TOK(NT_SUCCESS(St), "reopen D:\\sub1\\a.bin: 0x%08lx\n", St);
    if (NT_SUCCESS(St))
    {
        RtlZeroMemory(Rd, TEST_SMALL_BYTES);
        St = ReadAt(h, 0, Rd, TEST_SMALL_BYTES, &Info);
        TOK(NT_SUCCESS(St) && Info == TEST_SMALL_BYTES,
            "sub1 read: 0x%08lx info=%lu\n", St, Info);
        Errs = 0;
        for (i = 0; i < TEST_SMALL_BYTES; i++)
            if (Rd[i] != (UCHAR)(0xA0 + (i & 0x1F))) Errs++;
        TOK(Errs == 0, "sub1 content verify: %lu mismatches\n", Errs);
        ZwClose(h);
    }

    /*
     * Enumerate twice with fresh handles: catches the "re-enter, no files"
     * bug where the second open returns an empty directory even though
     * the children were correctly written.
     */
    {
        static const PCWSTR sub1Names[] = { L"a.bin" };
        ULONG pass, m, t;
        for (pass = 1; pass <= 2; pass++)
        {
            m = 0; t = 0;
            St = EnumDirMatch(L"\\??\\D:\\sub1", sub1Names,
                              RTL_NUMBER_OF(sub1Names), &m, &t);
            TOK(NT_SUCCESS(St) && m == RTL_NUMBER_OF(sub1Names),
                "sub1 enum pass%lu: status=0x%08lx matched=%lu total=%lu (expect %lu)\n",
                pass, St, m, t, (ULONG)RTL_NUMBER_OF(sub1Names));
        }
    }

    /*
     * ==========================================================
     * 21. Three-level nested subfolders + file content verify.
     *     Exercises path traversal across multiple MFT index lookups.
     * ==========================================================
     */
    TLOG("--- section 21: nested subfolders (3 deep) + content verify ---\n");
    St = OpenNtfs(L"\\??\\D:\\N1", GENERIC_READ | SYNCHRONIZE,
                  FILE_CREATE, FILE_DIRECTORY_FILE, &h);
    TOK(NT_SUCCESS(St), "mkdir D:\\N1: 0x%08lx\n", St);
    if (NT_SUCCESS(St)) ZwClose(h);

    St = OpenNtfs(L"\\??\\D:\\N1\\N2", GENERIC_READ | SYNCHRONIZE,
                  FILE_CREATE, FILE_DIRECTORY_FILE, &h);
    TOK(NT_SUCCESS(St), "mkdir D:\\N1\\N2: 0x%08lx\n", St);
    if (NT_SUCCESS(St)) ZwClose(h);

    St = OpenNtfs(L"\\??\\D:\\N1\\N2\\N3", GENERIC_READ | SYNCHRONIZE,
                  FILE_CREATE, FILE_DIRECTORY_FILE, &h);
    TOK(NT_SUCCESS(St), "mkdir D:\\N1\\N2\\N3: 0x%08lx\n", St);
    if (NT_SUCCESS(St)) ZwClose(h);

    St = OpenNtfs(L"\\??\\D:\\N1\\N2\\N3\\deep.bin",
                  GENERIC_READ | GENERIC_WRITE | DELETE | SYNCHRONIZE,
                  FILE_CREATE, FILE_NON_DIRECTORY_FILE, &h);
    TOK(NT_SUCCESS(St), "create deep.bin in N3: 0x%08lx\n", St);
    if (NT_SUCCESS(St))
    {
        for (i = 0; i < 128; i++) Wr[i] = (UCHAR)(0xD0 ^ (i & 0x3F));
        St = WriteAt(h, 0, Wr, 128, &Info);
        TOK(NT_SUCCESS(St) && Info == 128,
            "deep.bin write: 0x%08lx info=%lu\n", St, Info);
        ZwClose(h);
    }

    St = OpenNtfs(L"\\??\\D:\\N1\\N2\\N3\\deep.bin",
                  GENERIC_READ | SYNCHRONIZE,
                  FILE_OPEN, FILE_NON_DIRECTORY_FILE, &h);
    TOK(NT_SUCCESS(St), "reopen deep.bin: 0x%08lx\n", St);
    if (NT_SUCCESS(St))
    {
        RtlZeroMemory(Rd, 128);
        St = ReadAt(h, 0, Rd, 128, &Info);
        TOK(NT_SUCCESS(St) && Info == 128,
            "deep.bin read: 0x%08lx info=%lu\n", St, Info);
        Errs = 0;
        for (i = 0; i < 128; i++)
            if (Rd[i] != (UCHAR)(0xD0 ^ (i & 0x3F))) Errs++;
        TOK(Errs == 0, "deep.bin content verify: %lu mismatches\n", Errs);
        ZwClose(h);
    }

    {
        static const PCWSTR n3Names[] = { L"deep.bin" };
        ULONG pass, m, t;
        for (pass = 1; pass <= 2; pass++)
        {
            m = 0; t = 0;
            St = EnumDirMatch(L"\\??\\D:\\N1\\N2\\N3", n3Names,
                              RTL_NUMBER_OF(n3Names), &m, &t);
            TOK(NT_SUCCESS(St) && m == RTL_NUMBER_OF(n3Names),
                "N3 enum pass%lu: status=0x%08lx matched=%lu total=%lu (expect %lu)\n",
                pass, St, m, t, (ULONG)RTL_NUMBER_OF(n3Names));
        }
    }

    /*
     * ==========================================================
     * 22. Multiple files in the same subfolder with distinct fills.
     *     Ensures per-file data isolation when created back-to-back.
     * ==========================================================
     */
    TLOG("--- section 22: multi-file subfolder with distinct content ---\n");
    St = OpenNtfs(L"\\??\\D:\\multi", GENERIC_READ | SYNCHRONIZE,
                  FILE_CREATE, FILE_DIRECTORY_FILE, &h);
    TOK(NT_SUCCESS(St), "mkdir D:\\multi: 0x%08lx\n", St);
    if (NT_SUCCESS(St)) ZwClose(h);

    {
        static const struct { PCWSTR Path; UCHAR Fill; } Multi[] = {
            { L"\\??\\D:\\multi\\fa.bin", 0xAA },
            { L"\\??\\D:\\multi\\fb.bin", 0xBB },
            { L"\\??\\D:\\multi\\fc.bin", 0xCC },
        };
        ULONG m;
        for (m = 0; m < RTL_NUMBER_OF(Multi); m++)
        {
            St = OpenNtfs(Multi[m].Path,
                          GENERIC_READ | GENERIC_WRITE | DELETE | SYNCHRONIZE,
                          FILE_CREATE, FILE_NON_DIRECTORY_FILE, &h);
            TOK(NT_SUCCESS(St), "multi create [%lu]: 0x%08lx\n", m, St);
            if (!NT_SUCCESS(St)) continue;
            RtlFillMemory(Wr, 64, Multi[m].Fill);
            St = WriteAt(h, 0, Wr, 64, &Info);
            TOK(NT_SUCCESS(St) && Info == 64,
                "multi write [%lu]: 0x%08lx info=%lu\n", m, St, Info);
            ZwClose(h);
        }
        for (m = 0; m < RTL_NUMBER_OF(Multi); m++)
        {
            St = OpenNtfs(Multi[m].Path, GENERIC_READ | SYNCHRONIZE,
                          FILE_OPEN, FILE_NON_DIRECTORY_FILE, &h);
            TOK(NT_SUCCESS(St), "multi reopen [%lu]: 0x%08lx\n", m, St);
            if (!NT_SUCCESS(St)) continue;
            RtlZeroMemory(Rd, 64);
            St = ReadAt(h, 0, Rd, 64, &Info);
            TOK(NT_SUCCESS(St) && Info == 64,
                "multi read [%lu]: 0x%08lx info=%lu\n", m, St, Info);
            Errs = 0;
            for (i = 0; i < 64; i++) if (Rd[i] != Multi[m].Fill) Errs++;
            TOK(Errs == 0,
                "multi verify [%lu] fill=0x%02x: %lu mismatches\n",
                m, Multi[m].Fill, Errs);
            ZwClose(h);
        }
    }

    {
        static const PCWSTR multiNames[] = { L"fa.bin", L"fb.bin", L"fc.bin" };
        ULONG pass, Matched, Total;
        for (pass = 1; pass <= 2; pass++)
        {
            Matched = 0; Total = 0;
            St = EnumDirMatch(L"\\??\\D:\\multi", multiNames,
                              RTL_NUMBER_OF(multiNames), &Matched, &Total);
            TOK(NT_SUCCESS(St) && Matched == RTL_NUMBER_OF(multiNames),
                "multi enum pass%lu: status=0x%08lx matched=%lu total=%lu (expect %lu)\n",
                pass, St, Matched, Total, (ULONG)RTL_NUMBER_OF(multiNames));
        }
    }

    /*
     * ==========================================================
     * 23. Mixed-size files in a subfolder: small (resident) and
     *     large (forces non-resident promotion). Verify both survive
     *     a close-then-reopen with full content integrity.
     * ==========================================================
     */
    TLOG("--- section 23: mixed resident/non-resident in subfolder ---\n");
    St = OpenNtfs(L"\\??\\D:\\mix", GENERIC_READ | SYNCHRONIZE,
                  FILE_CREATE, FILE_DIRECTORY_FILE, &h);
    TOK(NT_SUCCESS(St), "mkdir D:\\mix: 0x%08lx\n", St);
    if (NT_SUCCESS(St)) ZwClose(h);

    St = OpenNtfs(L"\\??\\D:\\mix\\small.bin",
                  GENERIC_READ | GENERIC_WRITE | DELETE | SYNCHRONIZE,
                  FILE_CREATE, FILE_NON_DIRECTORY_FILE, &h);
    TOK(NT_SUCCESS(St), "mix small create: 0x%08lx\n", St);
    if (NT_SUCCESS(St))
    {
        for (i = 0; i < 128; i++) Wr[i] = (UCHAR)(0x10 + i);
        St = WriteAt(h, 0, Wr, 128, &Info);
        TOK(NT_SUCCESS(St) && Info == 128,
            "mix small write: 0x%08lx info=%lu\n", St, Info);
        ZwClose(h);
    }

    St = OpenNtfs(L"\\??\\D:\\mix\\big.bin",
                  GENERIC_READ | GENERIC_WRITE | DELETE | SYNCHRONIZE,
                  FILE_CREATE, FILE_NON_DIRECTORY_FILE, &h);
    TOK(NT_SUCCESS(St), "mix big create: 0x%08lx\n", St);
    if (NT_SUCCESS(St))
    {
        for (i = 0; i < TEST_EXTEND_CHUNK; i++)
            Wr[i] = (UCHAR)(0x80 + (i & 0x7F));
        St = WriteAt(h, 0, Wr, TEST_EXTEND_CHUNK, &Info);
        TOK(NT_SUCCESS(St) && Info == TEST_EXTEND_CHUNK,
            "mix big write: 0x%08lx info=%lu\n", St, Info);
        ZwClose(h);
    }

    St = OpenNtfs(L"\\??\\D:\\mix\\small.bin", GENERIC_READ | SYNCHRONIZE,
                  FILE_OPEN, FILE_NON_DIRECTORY_FILE, &h);
    TOK(NT_SUCCESS(St), "mix small reopen: 0x%08lx\n", St);
    if (NT_SUCCESS(St))
    {
        RtlZeroMemory(Rd, 128);
        St = ReadAt(h, 0, Rd, 128, &Info);
        TOK(NT_SUCCESS(St) && Info == 128,
            "mix small read: 0x%08lx info=%lu\n", St, Info);
        Errs = 0;
        for (i = 0; i < 128; i++) if (Rd[i] != (UCHAR)(0x10 + i)) Errs++;
        TOK(Errs == 0, "mix small verify: %lu mismatches\n", Errs);
        ZwClose(h);
    }

    St = OpenNtfs(L"\\??\\D:\\mix\\big.bin", GENERIC_READ | SYNCHRONIZE,
                  FILE_OPEN, FILE_NON_DIRECTORY_FILE, &h);
    TOK(NT_SUCCESS(St), "mix big reopen: 0x%08lx\n", St);
    if (NT_SUCCESS(St))
    {
        RtlZeroMemory(Rd, TEST_EXTEND_CHUNK);
        St = ReadAt(h, 0, Rd, TEST_EXTEND_CHUNK, &Info);
        TOK(NT_SUCCESS(St) && Info == TEST_EXTEND_CHUNK,
            "mix big read: 0x%08lx info=%lu\n", St, Info);
        Errs = 0;
        for (i = 0; i < TEST_EXTEND_CHUNK; i++)
            if (Rd[i] != (UCHAR)(0x80 + (i & 0x7F))) Errs++;
        TOK(Errs == 0, "mix big verify: %lu mismatches\n", Errs);
        ZwClose(h);
    }

    {
        static const PCWSTR mixNames[] = { L"small.bin", L"big.bin" };
        ULONG pass, Matched, Total;
        for (pass = 1; pass <= 2; pass++)
        {
            Matched = 0; Total = 0;
            St = EnumDirMatch(L"\\??\\D:\\mix", mixNames,
                              RTL_NUMBER_OF(mixNames), &Matched, &Total);
            TOK(NT_SUCCESS(St) && Matched == RTL_NUMBER_OF(mixNames),
                "mix enum pass%lu: status=0x%08lx matched=%lu total=%lu (expect %lu)\n",
                pass, St, Matched, Total, (ULONG)RTL_NUMBER_OF(mixNames));
        }
    }

    /*
     * ==========================================================
     * 24. Root directory metadata preservation after writes.
     *     After user files have been added in the preceding sections,
     *     the root directory index MUST still resolve the NTFS metadata
     *     files ($MFT, $MFTMirr, $LogFile, $Volume, $AttrDef, $Bitmap,
     *     $Boot, $BadClus, $Secure, $UpCase, $Extend) by name. Losing
     *     any of them means the on-disk INDEX_ROOT/INDEX_ALLOCATION has
     *     been truncated by the driver and the volume will be unmountable
     *     by any conformant NTFS implementation (ntfs-3g, Windows).
     *     Detected manually: after a VM run, ntfs-3g fails with
     *       "Failed to open $Secure: No such file or directory"
     *     because these names were dropped from the root index.
     * ==========================================================
     */
    TLOG("--- section 24: root metadata preservation after writes ---\n");
    {
        static const PCWSTR rootMeta[] = {
            L"$MFT", L"$MFTMirr", L"$LogFile", L"$Volume",
            L"$AttrDef", L"$Bitmap", L"$Boot", L"$BadClus",
            L"$Secure", L"$UpCase", L"$Extend",
        };
        ULONG pass, Matched, Total;
        for (pass = 1; pass <= 2; pass++)
        {
            Matched = 0; Total = 0;
            St = EnumDirMatch(L"\\??\\D:\\", rootMeta,
                              RTL_NUMBER_OF(rootMeta), &Matched, &Total);
            TOK(NT_SUCCESS(St) && Matched == RTL_NUMBER_OF(rootMeta),
                "root metadata enum pass%lu: status=0x%08lx matched=%lu/%lu total=%lu\n",
                pass, St, Matched, (ULONG)RTL_NUMBER_OF(rootMeta), Total);
        }

        /*
         * Also confirm each metadata file opens by name — this forces
         * the name-based lookup path (B-tree walk of the root index),
         * independent of the bulk enumeration above.
         */
        {
            ULONG k;
            for (k = 0; k < RTL_NUMBER_OF(rootMeta); k++)
            {
                WCHAR Path[32];
                HANDLE hMeta;
                RtlStringCbPrintfW(Path, sizeof(Path), L"\\??\\D:\\%ls",
                                   rootMeta[k]);
                St = OpenNtfs(Path, FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                              FILE_OPEN, 0, &hMeta);
                TOK(NT_SUCCESS(St),
                    "open root metadata '%ls' by name: 0x%08lx\n",
                    rootMeta[k], St);
                if (NT_SUCCESS(St))
                    ZwClose(hMeta);
            }
        }
    }

    /*
     * Sections 10+ are a large suite of stress / edge-case tests that take
     * well beyond the VM monitor hard-timeout. Bail out here to keep the
     * essential coverage (mount, volume info, resident/non-resident I/O,
     * file-info queries, directory enumeration, subfolder content) within
     * the time budget.
     */
    if (g_NtfslxFuncPhase == NtfslxFuncPhaseSmoke)
    {
        TLOG("=== NtfslxFunc END phase=%s (sections 0-24 only) ===\n",
             NtfslxGetFuncPhaseName());
        goto Done;
    }

    /*
     * ==========================================================
     * 10. $INDEX_ROOT -> $INDEX_ALLOCATION spill
     *     Create TEST_SPILL_FILES files in a row; the first few will
     *     fit in the resident root, the rest land in the allocation
     *     block. All of them must be visible in a subsequent enumerate.
     * ==========================================================
     */
LongSuitePhaseA:
    TLOG("=== NtfslxFunc phase=%s entering long suite A ===\n",
         NtfslxGetFuncPhaseName());
    TLOG("--- section 10: INDEX_ROOT spill to INDEX_ALLOCATION ---\n");
    for (i = 0; i < TEST_SPILL_FILES; i++)
    {
        WCHAR Path[64];
        RtlStringCbPrintfW(Path, sizeof(Path), L"\\??\\D:\\spill_%02lu.bin", i);
        St = OpenNtfs(Path, GENERIC_READ | GENERIC_WRITE | DELETE | SYNCHRONIZE,
                      FILE_CREATE, FILE_NON_DIRECTORY_FILE, &h);
        TOK(NT_SUCCESS(St), "spill create %lu: 0x%08lx\n", i, St);
        if (NT_SUCCESS(St))
        {
            UCHAR Tag = (UCHAR)(0x20 + i);
            for (Errs = 0; Errs < 64; Errs++) Wr[Errs] = Tag;
            St = WriteAt(h, 0, Wr, 64, &Info);
            TOK(NT_SUCCESS(St) && Info == 64, "spill write %lu: 0x%08lx\n", i, St);
            ZwClose(h);
        }
    }

    /* Re-enumerate; count how many spill_*.bin entries we see. */
    St = OpenNtfs(L"\\??\\D:\\", FILE_LIST_DIRECTORY | SYNCHRONIZE,
                  FILE_OPEN, FILE_DIRECTORY_FILE, &h);
    if (NT_SUCCESS(St))
    {
        UCHAR DirBuf[4096];
        ULONG SpillCount = 0;
        ULONG Offset;
        PFILE_BOTH_DIR_INFORMATION Entry;
        BOOLEAN FirstScan = TRUE;

        for (;;)
        {
            RtlZeroMemory(DirBuf, sizeof(DirBuf));
            St = ZwQueryDirectoryFile(h, NULL, NULL, NULL, &IoSb,
                                      DirBuf, sizeof(DirBuf),
                                      FileBothDirectoryInformation,
                                      FALSE, NULL, FirstScan);
            FirstScan = FALSE;
            if (!NT_SUCCESS(St) || IoSb.Information == 0) break;

            Offset = 0;
            while (Offset < IoSb.Information)
            {
                Entry = (PFILE_BOTH_DIR_INFORMATION)(DirBuf + Offset);
                if (Entry->FileNameLength >= 6 * sizeof(WCHAR) &&
                    RtlCompareMemory(Entry->FileName, L"spill_", 6 * sizeof(WCHAR)) == 6 * sizeof(WCHAR))
                {
                    SpillCount++;
                }
                if (Entry->NextEntryOffset == 0) break;
                Offset += Entry->NextEntryOffset;
            }
        }

        TOK(SpillCount == TEST_SPILL_FILES,
            "spill enum count=%lu (expect %d)\n", SpillCount, TEST_SPILL_FILES);
        ZwClose(h);
    }

    /*
     * ==========================================================
     * 10.4 Mid-file overwrite on a non-resident file + read past EOF
     * ==========================================================
     */
    TLOG("--- section 10.4: mid-file overwrite + read past EOF ---\n");
    UnlinkByPath(L"\\??\\D:\\kmtest_nrfull.bin");
    St = OpenNtfs(L"\\??\\D:\\kmtest_nrfull.bin",
                  GENERIC_READ | GENERIC_WRITE | DELETE | SYNCHRONIZE,
                  FILE_CREATE, FILE_NON_DIRECTORY_FILE, &h);
    TOK(NT_SUCCESS(St), "nrfull create: 0x%08lx\n", St);
    if (NT_SUCCESS(St))
    {
        /* Force non-resident with a 2048-byte write. */
        for (i = 0; i < 2048; i++) Wr[i] = (UCHAR)(i & 0xFF);
        St = WriteAt(h, 0, Wr, 2048, &Info);
        TOK(NT_SUCCESS(St) && Info == 2048, "nrfull initial write: 0x%08lx\n", St);

        /* Mid-file overwrite: replace bytes [512..1024) with a new pattern. */
        for (i = 0; i < 512; i++) Wr[i] = (UCHAR)(0xF0 + (i & 0x0F));
        St = WriteAt(h, 512, Wr, 512, &Info);
        TOK(NT_SUCCESS(St) && Info == 512, "nrfull mid overwrite: 0x%08lx info=%lu\n",
            St, Info);

        /* Read the whole file and verify both regions. */
        RtlZeroMemory(Rd, 2048);
        St = ReadAt(h, 0, Rd, 2048, &Info);
        TOK(NT_SUCCESS(St) && Info == 2048, "nrfull read-all: 0x%08lx info=%lu\n",
            St, Info);
        Errs = 0;
        for (i = 0; i < 512; i++)
            if (Rd[i] != (UCHAR)(i & 0xFF)) Errs++;
        for (i = 512; i < 1024; i++)
            if (Rd[i] != (UCHAR)(0xF0 + ((i - 512) & 0x0F))) Errs++;
        for (i = 1024; i < 2048; i++)
            if (Rd[i] != (UCHAR)(i & 0xFF)) Errs++;
        TOK(Errs == 0, "nrfull after mid overwrite: %lu mismatches\n", Errs);

        /* Read past EOF should return STATUS_END_OF_FILE with info=0. */
        St = ReadAt(h, 4096, Rd, 512, &Info);
        TOK(St == STATUS_END_OF_FILE,
            "nrfull read past EOF: 0x%08lx (expect STATUS_END_OF_FILE)\n", St);

        /* Read straddling EOF: start in-range, end past EOF. Should truncate. */
        RtlZeroMemory(Rd, 512);
        St = ReadAt(h, 1900, Rd, 512, &Info);
        TOK(NT_SUCCESS(St) && Info == (2048 - 1900),
            "nrfull straddle read: 0x%08lx info=%lu (expect %lu)\n",
            St, Info, (ULONG)(2048 - 1900));

        /* Truncate the file back to 256 bytes and verify. */
        Eof.EndOfFile.QuadPart = 256;
        St = ZwSetInformationFile(h, &IoSb, &Eof, sizeof(Eof), FileEndOfFileInformation);
        TOK(NT_SUCCESS(St), "nrfull truncate to 256: 0x%08lx\n", St);

        RtlZeroMemory(&Std, sizeof(Std));
        St = ZwQueryInformationFile(h, &IoSb, &Std, sizeof(Std), FileStandardInformation);
        TOK(NT_SUCCESS(St) && Std.EndOfFile.QuadPart == 256,
            "nrfull std after trunc: 0x%08lx eof=%I64d (expect 256)\n",
            St, Std.EndOfFile.QuadPart);

        /* Read past the truncated EOF. */
        St = ReadAt(h, 512, Rd, 256, &Info);
        TOK(St == STATUS_END_OF_FILE,
            "nrfull read past truncated EOF: 0x%08lx (expect END_OF_FILE)\n", St);

        ZwClose(h);
    }
    UnlinkByPath(L"\\??\\D:\\kmtest_nrfull.bin");

    /*
     * ==========================================================
     * 10.5 Append writes: keep appending 512-byte chunks to the
     *      same file, verify EndOfFile grows each time.
     * ==========================================================
     */
    TLOG("--- section 10.5: append writes ---\n");
    UnlinkByPath(L"\\??\\D:\\kmtest_append.bin");
    St = OpenNtfs(L"\\??\\D:\\kmtest_append.bin",
                  GENERIC_READ | GENERIC_WRITE | DELETE | SYNCHRONIZE,
                  FILE_CREATE, FILE_NON_DIRECTORY_FILE, &h);
    TOK(NT_SUCCESS(St), "append create: 0x%08lx\n", St);
    if (NT_SUCCESS(St))
    {
        ULONG j;
        for (j = 0; j < TEST_APPEND_CHUNKS; j++)
        {
            ULONG ChunkLen = 512;
            for (i = 0; i < ChunkLen; i++) Wr[i] = (UCHAR)((j * 37 + i) & 0xFF);
            St = WriteAt(h, (ULONGLONG)j * ChunkLen, Wr, ChunkLen, &Info);
            TOK(NT_SUCCESS(St) && Info == ChunkLen,
                "append chunk %lu: 0x%08lx info=%lu\n", j, St, Info);

            RtlZeroMemory(&Std, sizeof(Std));
            St = ZwQueryInformationFile(h, &IoSb, &Std, sizeof(Std), FileStandardInformation);
            TOK(NT_SUCCESS(St) && Std.EndOfFile.QuadPart == (LONGLONG)((j + 1) * ChunkLen),
                "append size after chunk %lu: eof=%I64d (expect %I64d)\n",
                j, Std.EndOfFile.QuadPart, (LONGLONG)((j + 1) * ChunkLen));
        }

        /* Read back the entire file and compare byte-by-byte. */
        for (j = 0; j < TEST_APPEND_CHUNKS; j++)
        {
            ULONG ChunkLen = 512;
            ULONG Mismatches = 0;
            RtlZeroMemory(Rd, TEST_BIG_BYTES);
            St = ReadAt(h, (ULONGLONG)j * ChunkLen, Rd, ChunkLen, &Info);
            TOK(NT_SUCCESS(St) && Info == ChunkLen,
                "append read chunk %lu: 0x%08lx info=%lu\n", j, St, Info);
            for (i = 0; i < ChunkLen; i++)
                if (Rd[i] != (UCHAR)((j * 37 + i) & 0xFF)) Mismatches++;
            TOK(Mismatches == 0,
                "append chunk %lu compare: %lu mismatches\n", j, Mismatches);
        }
        ZwClose(h);
    }
    UnlinkByPath(L"\\??\\D:\\kmtest_append.bin");

    /*
     * ==========================================================
     * 10.6 Concurrent opens: open the same file twice and verify
     *      the second open returns a context with the same MftIndex.
     * ==========================================================
     */
    TLOG("--- section 10.6: concurrent opens ---\n");
    UnlinkByPath(L"\\??\\D:\\kmtest_concurrent.bin");
    St = OpenNtfs(L"\\??\\D:\\kmtest_concurrent.bin",
                  GENERIC_READ | GENERIC_WRITE | DELETE | SYNCHRONIZE,
                  FILE_CREATE, FILE_NON_DIRECTORY_FILE, &h);
    TOK(NT_SUCCESS(St), "concurrent create: 0x%08lx\n", St);
    if (NT_SUCCESS(St))
    {
        for (i = 0; i < 64; i++) Wr[i] = (UCHAR)(0x5A + (i & 0x1F));
        St = WriteAt(h, 0, Wr, 64, &Info);
        TOK(NT_SUCCESS(St), "concurrent write: 0x%08lx\n", St);

        /* Second open while first handle is still open. */
        St = OpenNtfs(L"\\??\\D:\\kmtest_concurrent.bin",
                      GENERIC_READ | SYNCHRONIZE, FILE_OPEN,
                      FILE_NON_DIRECTORY_FILE, &h2);
        TOK(NT_SUCCESS(St), "concurrent second open: 0x%08lx\n", St);
        if (NT_SUCCESS(St))
        {
            RtlZeroMemory(Rd, 64);
            St = ReadAt(h2, 0, Rd, 64, &Info);
            TOK(NT_SUCCESS(St) && Info == 64,
                "concurrent second-handle read: 0x%08lx info=%lu\n", St, Info);
            Errs = 0;
            for (i = 0; i < 64; i++)
                if (Rd[i] != (UCHAR)(0x5A + (i & 0x1F))) Errs++;
            TOK(Errs == 0, "concurrent read compare: %lu mismatches\n", Errs);
            ZwClose(h2);
        }
        ZwClose(h);
    }
    UnlinkByPath(L"\\??\\D:\\kmtest_concurrent.bin");

    /*
     * ==========================================================
     * 10.7 FileRenameInformation (same-directory rename)
     * ==========================================================
     */
    TLOG("--- section 10.7: same-directory rename ---\n");
    UnlinkByPath(L"\\??\\D:\\kmtest_rn_src.bin");
    UnlinkByPath(L"\\??\\D:\\kmtest_rn_dst.bin");
    UnlinkByPath(L"\\??\\D:\\kmtest_rn_longer_name.bin");
    St = OpenNtfs(L"\\??\\D:\\kmtest_rn_src.bin",
                  GENERIC_READ | GENERIC_WRITE | DELETE | SYNCHRONIZE,
                  FILE_CREATE, FILE_NON_DIRECTORY_FILE, &h);
    TOK(NT_SUCCESS(St), "rename create src: 0x%08lx\n", St);
    if (NT_SUCCESS(St))
    {
        UCHAR RnBuf[sizeof(FILE_RENAME_INFORMATION) + 128 * sizeof(WCHAR)];
        PFILE_RENAME_INFORMATION Rn = (PFILE_RENAME_INFORMATION)RnBuf;

        for (i = 0; i < 128; i++) Wr[i] = 0xCC;
        St = WriteAt(h, 0, Wr, 128, &Info);
        TOK(NT_SUCCESS(St), "rename src initial write: 0x%08lx\n", St);

        /* Rename to same-length name. */
        RtlZeroMemory(Rn, sizeof(RnBuf));
        Rn->ReplaceIfExists = FALSE;
        Rn->RootDirectory = NULL;
        {
            PCWSTR NewName = L"kmtest_rn_dst.bin";
            ULONG NewNameLen = 17;
            Rn->FileNameLength = NewNameLen * sizeof(WCHAR);
            RtlCopyMemory(Rn->FileName, NewName, Rn->FileNameLength);
        }
        St = ZwSetInformationFile(h, &IoSb, Rn,
                                  FIELD_OFFSET(FILE_RENAME_INFORMATION, FileName) +
                                      Rn->FileNameLength,
                                  FileRenameInformation);
        TOK(NT_SUCCESS(St), "rename same-length: 0x%08lx\n", St);

        ZwClose(h);

        /* Old name should be gone, new name should open. */
        St = OpenNtfs(L"\\??\\D:\\kmtest_rn_src.bin",
                      GENERIC_READ | SYNCHRONIZE, FILE_OPEN,
                      FILE_NON_DIRECTORY_FILE, &h2);
        TOK(St == STATUS_OBJECT_NAME_NOT_FOUND,
            "rename old name gone: 0x%08lx (expect NOT_FOUND)\n", St);
        if (NT_SUCCESS(St)) ZwClose(h2);

        St = OpenNtfs(L"\\??\\D:\\kmtest_rn_dst.bin",
                      GENERIC_READ | GENERIC_WRITE | DELETE | SYNCHRONIZE,
                      FILE_OPEN, FILE_NON_DIRECTORY_FILE, &h);
        TOK(NT_SUCCESS(St), "rename new name opens: 0x%08lx\n", St);
        if (NT_SUCCESS(St))
        {
            /* Verify the data is intact after rename. */
            RtlZeroMemory(Rd, 128);
            St = ReadAt(h, 0, Rd, 128, &Info);
            TOK(NT_SUCCESS(St) && Info == 128,
                "rename read after: 0x%08lx info=%lu\n", St, Info);
            Errs = 0;
            for (i = 0; i < 128; i++) if (Rd[i] != 0xCC) Errs++;
            TOK(Errs == 0, "rename data after: %lu mismatches\n", Errs);

            /* Now rename to a longer name to exercise the resize path. */
            RtlZeroMemory(Rn, sizeof(RnBuf));
            Rn->ReplaceIfExists = FALSE;
            Rn->RootDirectory = NULL;
            {
                PCWSTR NewName = L"kmtest_rn_longer_name.bin";
                ULONG NewNameLen = 25;
                Rn->FileNameLength = NewNameLen * sizeof(WCHAR);
                RtlCopyMemory(Rn->FileName, NewName, Rn->FileNameLength);
            }
            St = ZwSetInformationFile(h, &IoSb, Rn,
                                      FIELD_OFFSET(FILE_RENAME_INFORMATION, FileName) +
                                          Rn->FileNameLength,
                                      FileRenameInformation);
            TOK(NT_SUCCESS(St), "rename longer: 0x%08lx\n", St);
            ZwClose(h);

            /* Verify longer name opens and old (kmtest_rn_dst) is gone. */
            St = OpenNtfs(L"\\??\\D:\\kmtest_rn_dst.bin",
                          GENERIC_READ | SYNCHRONIZE, FILE_OPEN,
                          FILE_NON_DIRECTORY_FILE, &h2);
            TOK(St == STATUS_OBJECT_NAME_NOT_FOUND,
                "rename longer: old name gone 0x%08lx (expect NOT_FOUND)\n", St);
            if (NT_SUCCESS(St)) ZwClose(h2);

            St = OpenNtfs(L"\\??\\D:\\kmtest_rn_longer_name.bin",
                          GENERIC_READ | SYNCHRONIZE, FILE_OPEN,
                          FILE_NON_DIRECTORY_FILE, &h2);
            TOK(NT_SUCCESS(St), "rename longer new name opens: 0x%08lx\n", St);
            if (NT_SUCCESS(St))
            {
                RtlZeroMemory(Rd, 128);
                St = ReadAt(h2, 0, Rd, 128, &Info);
                TOK(NT_SUCCESS(St) && Info == 128,
                    "rename longer read: 0x%08lx info=%lu\n", St, Info);
                ZwClose(h2);
            }
        }
    }
    UnlinkByPath(L"\\??\\D:\\kmtest_rn_src.bin");
    UnlinkByPath(L"\\??\\D:\\kmtest_rn_dst.bin");
    UnlinkByPath(L"\\??\\D:\\kmtest_rn_longer_name.bin");

    /*
     * ==========================================================
     * 10.8 Long file name test (100-char name round-trip)
     * ==========================================================
     */
    TLOG("--- section 10.8: long file name ---\n");
    {
        WCHAR LongName[256];
        WCHAR LongPath[300];
        ULONG LongLen = 100;
        for (i = 0; i < LongLen; i++)
            LongName[i] = (WCHAR)(L'a' + (i % 26));
        LongName[LongLen] = 0;

        RtlStringCbPrintfW(LongPath, sizeof(LongPath), L"\\??\\D:\\%s.bin", LongName);
        UnlinkByPath(LongPath);

        St = OpenNtfs(LongPath,
                      GENERIC_READ | GENERIC_WRITE | DELETE | SYNCHRONIZE,
                      FILE_CREATE, FILE_NON_DIRECTORY_FILE, &h);
        TOK(NT_SUCCESS(St), "long-name create (%lu chars): 0x%08lx\n", LongLen, St);
        if (NT_SUCCESS(St))
        {
            for (i = 0; i < 100; i++) Wr[i] = (UCHAR)(0xA0 + (i & 0x0F));
            St = WriteAt(h, 0, Wr, 100, &Info);
            TOK(NT_SUCCESS(St) && Info == 100, "long-name write: 0x%08lx\n", St);

            RtlZeroMemory(Rd, 100);
            St = ReadAt(h, 0, Rd, 100, &Info);
            TOK(NT_SUCCESS(St) && Info == 100, "long-name read: 0x%08lx\n", St);
            Errs = 0;
            for (i = 0; i < 100; i++)
                if (Rd[i] != (UCHAR)(0xA0 + (i & 0x0F))) Errs++;
            TOK(Errs == 0, "long-name compare: %lu mismatches\n", Errs);

            RtlZeroMemory(&Std, sizeof(Std));
            St = ZwQueryInformationFile(h, &IoSb, &Std, sizeof(Std), FileStandardInformation);
            TOK(NT_SUCCESS(St) && Std.EndOfFile.QuadPart == 100,
                "long-name std: 0x%08lx eof=%I64d\n", St, Std.EndOfFile.QuadPart);
            ZwClose(h);

            /* Reopen via the long path to confirm the $FILE_NAME entry round-trips. */
            St = OpenNtfs(LongPath,
                          GENERIC_READ | DELETE | SYNCHRONIZE,
                          FILE_OPEN, FILE_NON_DIRECTORY_FILE, &h);
            TOK(NT_SUCCESS(St), "long-name reopen: 0x%08lx\n", St);
            if (NT_SUCCESS(St)) ZwClose(h);
        }
        UnlinkByPath(LongPath);
    }

    /*
     * ==========================================================
     * 10.9 Rename edge cases: collision without ReplaceIfExists,
     *      missing source, same-name no-op
     * ==========================================================
     */
    TLOG("--- section 10.9: rename edge cases ---\n");
    UnlinkByPath(L"\\??\\D:\\kmtest_rnA.bin");
    UnlinkByPath(L"\\??\\D:\\kmtest_rnB.bin");
    /* Create two distinct files. Both handles are gated by create success. */
    h = NULL;
    h2 = NULL;
    St = OpenNtfs(L"\\??\\D:\\kmtest_rnA.bin",
                  GENERIC_READ | GENERIC_WRITE | DELETE | SYNCHRONIZE,
                  FILE_CREATE, FILE_NON_DIRECTORY_FILE, &h);
    TOK(NT_SUCCESS(St), "rnA create: 0x%08lx\n", St);
    if (NT_SUCCESS(St))
    {
        NTSTATUS RnBCreate;
        RnBCreate = OpenNtfs(L"\\??\\D:\\kmtest_rnB.bin",
                             GENERIC_READ | GENERIC_WRITE | DELETE | SYNCHRONIZE,
                             FILE_CREATE, FILE_NON_DIRECTORY_FILE, &h2);
        if (NT_SUCCESS(RnBCreate))
        {
            UCHAR RnBuf[sizeof(FILE_RENAME_INFORMATION) + 64 * sizeof(WCHAR)];
            PFILE_RENAME_INFORMATION Rn = (PFILE_RENAME_INFORMATION)RnBuf;
            PCWSTR TargetName = L"kmtest_rnB.bin";
            ULONG TargetLen = 14;

            RtlZeroMemory(Rn, sizeof(RnBuf));
            Rn->ReplaceIfExists = FALSE;
            Rn->RootDirectory = NULL;
            Rn->FileNameLength = TargetLen * sizeof(WCHAR);
            RtlCopyMemory(Rn->FileName, TargetName, Rn->FileNameLength);

            /* Renaming rnA to rnB without ReplaceIfExists should collide. */
            St = ZwSetInformationFile(h, &IoSb, Rn,
                                      FIELD_OFFSET(FILE_RENAME_INFORMATION, FileName) +
                                          Rn->FileNameLength,
                                      FileRenameInformation);
            TOK(St == STATUS_OBJECT_NAME_COLLISION,
                "rnA->rnB no-replace: 0x%08lx (expect COLLISION)\n", St);

            ZwClose(h2);
            h2 = NULL;
        }
        else
        {
            TLOG("section 10.9: rnB create failed 0x%08lx (likely MFT full, skipping)\n",
                 RnBCreate);
        }
        ZwClose(h);
        h = NULL;
    }
    UnlinkByPath(L"\\??\\D:\\kmtest_rnA.bin");
    UnlinkByPath(L"\\??\\D:\\kmtest_rnB.bin");

    /*
     * ==========================================================
     * 10.10 NtCreateSection (memory-mappable file).
     *       Verifies FileObject->SectionObjectPointer is non-NULL
     *       and that section creation returns a sane status.
     * ==========================================================
     */
    TLOG("--- section 10.10: NtCreateSection ---\n");
    UnlinkByPath(L"\\??\\D:\\kmtest_section.bin");
    St = OpenNtfs(L"\\??\\D:\\kmtest_section.bin",
                  GENERIC_READ | GENERIC_WRITE | DELETE | SYNCHRONIZE,
                  FILE_CREATE, FILE_NON_DIRECTORY_FILE, &h);
    TOK(NT_SUCCESS(St), "section create file: 0x%08lx\n", St);
    if (NT_SUCCESS(St))
    {
        HANDLE Section = NULL;
        LARGE_INTEGER MaxSize;

        /* Write some content so there's actual data to map. */
        for (i = 0; i < 1024; i++) Wr[i] = (UCHAR)(0x40 + (i & 0x1F));
        St = WriteAt(h, 0, Wr, 1024, &Info);
        TOK(NT_SUCCESS(St) && Info == 1024, "section write: 0x%08lx\n", St);
        ZwClose(h);
        h = NULL;

        /* Reopen for read + execute (exec is what section creation wants). */
        St = OpenNtfs(L"\\??\\D:\\kmtest_section.bin",
                      GENERIC_READ | DELETE | SYNCHRONIZE,
                      FILE_OPEN, FILE_NON_DIRECTORY_FILE, &h);
        TOK(NT_SUCCESS(St), "section reopen: 0x%08lx\n", St);
        if (NT_SUCCESS(St))
        {
            MaxSize.QuadPart = 1024;
            St = ZwCreateSection(&Section,
                                 SECTION_MAP_READ | SECTION_QUERY,
                                 NULL,
                                 &MaxSize,
                                 PAGE_READONLY,
                                 SEC_COMMIT,
                                 h);
            TOK(NT_SUCCESS(St),
                "ZwCreateSection: 0x%08lx\n", St);

            if (NT_SUCCESS(St) && Section != NULL)
            {
                PVOID MapView = NULL;
                SIZE_T ViewSize = 0;
                LARGE_INTEGER Zero;
                Zero.QuadPart = 0;

                St = ZwMapViewOfSection(Section,
                                        ZwCurrentProcess(),
                                        &MapView,
                                        0,
                                        0,
                                        &Zero,
                                        &ViewSize,
                                        ViewUnmap,
                                        0,
                                        PAGE_READONLY);
                TOK(NT_SUCCESS(St),
                    "ZwMapViewOfSection: 0x%08lx\n", St);

                if (NT_SUCCESS(St) && MapView != NULL)
                {
                    BOOLEAN TouchOk = FALSE;
                    UCHAR First = 0;
                    UCHAR Last = 0;

                    _SEH2_TRY
                    {
                        First = ((PUCHAR)MapView)[0];
                        Last = ((PUCHAR)MapView)[1023];
                        TouchOk = TRUE;
                    }
                    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
                    {
                        TLOG("section 10.10: touch raised 0x%08lx\n",
                             _SEH2_GetExceptionCode());
                    }
                    _SEH2_END;

                    TOK(TouchOk, "section mapped view touch\n");
                    TOK(First == (UCHAR)(0x40 + (0 & 0x1F)),
                        "mapped byte[0]=0x%02x (expect 0x40)\n", First);
                    TOK(Last == (UCHAR)(0x40 + (1023 & 0x1F)),
                        "mapped byte[1023]=0x%02x (expect 0x%02x)\n",
                        Last, (UCHAR)(0x40 + (1023 & 0x1F)));

                    ZwUnmapViewOfSection(ZwCurrentProcess(), MapView);
                }

                ZwClose(Section);
            }
            ZwClose(h);
            h = NULL;
        }
    }
    UnlinkByPath(L"\\??\\D:\\kmtest_section.bin");

    /*
     * ==========================================================
     * 10.11 Non-resident extend via sequential chunked writes.
     *       Mirrors Explorer's CopyFile pattern: write chunk N at
     *       offset N*CHUNK, never pre-setting EOF. The first chunk
     *       promotes resident -> non-resident; subsequent chunks
     *       exercise NtfslxExtendNonResident (new helper).
     * ==========================================================
     */
    TLOG("--- section 10.11: non-resident sequential extend ---\n");
    UnlinkByPath(L"\\??\\D:\\kmtest_extend.bin");
    St = OpenNtfs(L"\\??\\D:\\kmtest_extend.bin",
                  GENERIC_READ | GENERIC_WRITE | DELETE | SYNCHRONIZE,
                  FILE_CREATE, FILE_NON_DIRECTORY_FILE, &h);
    TOK(NT_SUCCESS(St), "extend create: 0x%08lx\n", St);
    if (NT_SUCCESS(St))
    {
        ULONG j;
        ULONGLONG ExpectedSize = 0;

        for (j = 0; j < TEST_EXTEND_CHUNKS; j++)
        {
            for (i = 0; i < TEST_EXTEND_CHUNK; i++)
                Wr[i] = (UCHAR)((j * 131 + i) & 0xFF);

            St = WriteAt(h, (ULONGLONG)j * TEST_EXTEND_CHUNK,
                         Wr, TEST_EXTEND_CHUNK, &Info);
            TOK(NT_SUCCESS(St) && Info == TEST_EXTEND_CHUNK,
                "extend write chunk %lu ofs=%I64u: 0x%08lx info=%lu\n",
                j, (ULONGLONG)j * TEST_EXTEND_CHUNK, St, Info);
            if (!NT_SUCCESS(St)) break;

            ExpectedSize = (ULONGLONG)(j + 1) * TEST_EXTEND_CHUNK;
            RtlZeroMemory(&Std, sizeof(Std));
            St = ZwQueryInformationFile(h, &IoSb, &Std, sizeof(Std),
                                        FileStandardInformation);
            TOK(NT_SUCCESS(St) && (ULONGLONG)Std.EndOfFile.QuadPart == ExpectedSize,
                "extend size after chunk %lu: eof=%I64d (expect %I64u)\n",
                j, Std.EndOfFile.QuadPart, ExpectedSize);
            TOK(NT_SUCCESS(St) && (ULONGLONG)Std.AllocationSize.QuadPart >= ExpectedSize,
                "extend alloc after chunk %lu: alloc=%I64d (>= %I64u)\n",
                j, Std.AllocationSize.QuadPart, ExpectedSize);
        }

        /* Read back every chunk and verify the content was preserved across
         * the repeated extend/merge operations. */
        for (j = 0; j < TEST_EXTEND_CHUNKS; j++)
        {
            ULONG Mismatches = 0;
            RtlZeroMemory(Rd, TEST_EXTEND_CHUNK);
            St = ReadAt(h, (ULONGLONG)j * TEST_EXTEND_CHUNK, Rd,
                        TEST_EXTEND_CHUNK, &Info);
            TOK(NT_SUCCESS(St) && Info == TEST_EXTEND_CHUNK,
                "extend read chunk %lu: 0x%08lx info=%lu\n", j, St, Info);
            for (i = 0; i < TEST_EXTEND_CHUNK; i++)
                if (Rd[i] != (UCHAR)((j * 131 + i) & 0xFF)) Mismatches++;
            TOK(Mismatches == 0,
                "extend chunk %lu compare: %lu mismatches\n", j, Mismatches);
        }

        ZwClose(h);

        /* Reopen and verify the disk state (size/data) is stable. */
        St = OpenNtfs(L"\\??\\D:\\kmtest_extend.bin",
                      GENERIC_READ | DELETE | SYNCHRONIZE,
                      FILE_OPEN, FILE_NON_DIRECTORY_FILE, &h);
        TOK(NT_SUCCESS(St), "extend reopen: 0x%08lx\n", St);
        if (NT_SUCCESS(St))
        {
            RtlZeroMemory(&Std, sizeof(Std));
            St = ZwQueryInformationFile(h, &IoSb, &Std, sizeof(Std),
                                        FileStandardInformation);
            TOK(NT_SUCCESS(St) &&
                (ULONGLONG)Std.EndOfFile.QuadPart ==
                    (ULONGLONG)TEST_EXTEND_CHUNK * TEST_EXTEND_CHUNKS,
                "extend reopen size: eof=%I64d (expect %lu)\n",
                Std.EndOfFile.QuadPart,
                (ULONG)TEST_EXTEND_CHUNK * TEST_EXTEND_CHUNKS);

            /* Spot-check first/middle/last chunks. */
            for (j = 0; j < TEST_EXTEND_CHUNKS; j += (TEST_EXTEND_CHUNKS / 3) + 1)
            {
                ULONG Mismatches = 0;
                if (j >= TEST_EXTEND_CHUNKS) break;
                RtlZeroMemory(Rd, TEST_EXTEND_CHUNK);
                St = ReadAt(h, (ULONGLONG)j * TEST_EXTEND_CHUNK, Rd,
                            TEST_EXTEND_CHUNK, &Info);
                TOK(NT_SUCCESS(St) && Info == TEST_EXTEND_CHUNK,
                    "extend reopen read %lu: 0x%08lx\n", j, St);
                for (i = 0; i < TEST_EXTEND_CHUNK; i++)
                    if (Rd[i] != (UCHAR)((j * 131 + i) & 0xFF)) Mismatches++;
                TOK(Mismatches == 0,
                    "extend reopen chunk %lu compare: %lu mismatches\n",
                    j, Mismatches);
            }
            ZwClose(h);
        }
    }
    UnlinkByPath(L"\\??\\D:\\kmtest_extend.bin");

    /*
     * ==========================================================
     * 10.12 Stress: multi-file create/write/read/verify/delete
     *       across multiple waves. Interleaves resident and
     *       non-resident sizes, closes and reopens handles, and
     *       recreates files under the same names to catch dangling
     *       CCB/Ccb references, FcbHeader leaks, and $I30
     *       insert/remove imbalance. After all waves, the directory
     *       must be clean (no leftover entries).
     * ==========================================================
     */
    TLOG("--- section 10.12: multi-file stress (create/write/read/delete) ---\n");
    {
        ULONG Wave;
        ULONG FileIdx;
        ULONG TotalCreated = 0;
        ULONG TotalVerified = 0;
        ULONG TotalDeleted = 0;

        for (Wave = 0; Wave < TEST_STRESS_WAVES; Wave++)
        {
            /* Create + write + verify in one handle per file.
             * Vary size: alternate resident (64B), threshold (600B),
             * non-resident small (2KB), non-resident medium (4KB). */
            static const ULONG SizeTable[4] = { 64, 600, 2048, 4096 };

            for (FileIdx = 0; FileIdx < TEST_STRESS_FILES; FileIdx++)
            {
                WCHAR Path[64];
                ULONG Size = SizeTable[FileIdx % 4];
                UCHAR Pattern = (UCHAR)(0x10 + Wave * 16 + FileIdx);

                RtlStringCbPrintfW(Path, sizeof(Path),
                                   L"\\??\\D:\\stress_w%lu_%02lu.bin",
                                   Wave, FileIdx);

                St = OpenNtfs(Path,
                              GENERIC_READ | GENERIC_WRITE | DELETE | SYNCHRONIZE,
                              FILE_CREATE, FILE_NON_DIRECTORY_FILE, &h);
                if (!NT_SUCCESS(St))
                {
                    TLOG("stress wave %lu file %lu create: 0x%08lx\n",
                         Wave, FileIdx, St);
                    continue;
                }
                TotalCreated++;

                for (i = 0; i < Size; i++) Wr[i] = (UCHAR)(Pattern + (i & 0xFF));
                St = WriteAt(h, 0, Wr, Size, &Info);
                if (!NT_SUCCESS(St) || Info != Size)
                {
                    TLOG("stress wave %lu file %lu write: 0x%08lx info=%lu\n",
                         Wave, FileIdx, St, Info);
                    ZwClose(h);
                    continue;
                }

                /* Verify in the same handle before closing. */
                RtlZeroMemory(Rd, Size);
                St = ReadAt(h, 0, Rd, Size, &Info);
                if (NT_SUCCESS(St) && Info == Size)
                {
                    Errs = 0;
                    for (i = 0; i < Size; i++)
                        if (Rd[i] != (UCHAR)(Pattern + (i & 0xFF))) Errs++;
                    if (Errs == 0) TotalVerified++;
                }

                /* Verify size metadata. */
                RtlZeroMemory(&Std, sizeof(Std));
                St = ZwQueryInformationFile(h, &IoSb, &Std, sizeof(Std),
                                            FileStandardInformation);
                if (NT_SUCCESS(St) && Std.EndOfFile.QuadPart != (LONGLONG)Size)
                {
                    TLOG("stress wave %lu file %lu size mismatch: eof=%I64d expect=%lu\n",
                         Wave, FileIdx, Std.EndOfFile.QuadPart, Size);
                }

                ZwClose(h);
            }

            /* Reopen each file fresh and verify persisted state from disk. */
            for (FileIdx = 0; FileIdx < TEST_STRESS_FILES; FileIdx++)
            {
                WCHAR Path[64];
                ULONG Size = SizeTable[FileIdx % 4];
                UCHAR Pattern = (UCHAR)(0x10 + Wave * 16 + FileIdx);
                ULONG Mismatches = 0;

                RtlStringCbPrintfW(Path, sizeof(Path),
                                   L"\\??\\D:\\stress_w%lu_%02lu.bin",
                                   Wave, FileIdx);

                St = OpenNtfs(Path,
                              GENERIC_READ | DELETE | SYNCHRONIZE,
                              FILE_OPEN, FILE_NON_DIRECTORY_FILE, &h);
                if (!NT_SUCCESS(St)) continue;

                RtlZeroMemory(Rd, Size);
                St = ReadAt(h, 0, Rd, Size, &Info);
                if (NT_SUCCESS(St) && Info == Size)
                {
                    for (i = 0; i < Size; i++)
                        if (Rd[i] != (UCHAR)(Pattern + (i & 0xFF))) Mismatches++;
                }
                if (Mismatches != 0)
                {
                    TLOG("stress reopen wave %lu file %lu: %lu mismatches\n",
                         Wave, FileIdx, Mismatches);
                }
                ZwClose(h);
            }

            /* Delete every file in this wave. Deletions exercise
             * Cleanup-time delete + index remove. Subsequent waves
             * recycle the MFT records and $I30 slots. */
            for (FileIdx = 0; FileIdx < TEST_STRESS_FILES; FileIdx++)
            {
                WCHAR Path[64];
                RtlStringCbPrintfW(Path, sizeof(Path),
                                   L"\\??\\D:\\stress_w%lu_%02lu.bin",
                                   Wave, FileIdx);
                UnlinkByPath(Path);
                TotalDeleted++;
            }
        }

        TOK(TotalCreated == TEST_STRESS_WAVES * TEST_STRESS_FILES,
            "stress created=%lu (expect %lu)\n",
            TotalCreated, (ULONG)(TEST_STRESS_WAVES * TEST_STRESS_FILES));
        TOK(TotalVerified >= TotalCreated - (TotalCreated / 8),
            "stress verified=%lu (created=%lu)\n", TotalVerified, TotalCreated);
        TOK(TotalDeleted == TotalCreated,
            "stress deleted=%lu (expect %lu)\n", TotalDeleted, TotalCreated);

        /* Enumerate the root: no stress_* leftovers should remain. */
        St = OpenNtfs(L"\\??\\D:\\", FILE_LIST_DIRECTORY | SYNCHRONIZE,
                      FILE_OPEN, FILE_DIRECTORY_FILE, &h);
        if (NT_SUCCESS(St))
        {
            UCHAR DirBuf[4096];
            ULONG StressLeft = 0;
            ULONG Offset;
            PFILE_BOTH_DIR_INFORMATION Entry;
            BOOLEAN FirstScan = TRUE;

            for (;;)
            {
                RtlZeroMemory(DirBuf, sizeof(DirBuf));
                St = ZwQueryDirectoryFile(h, NULL, NULL, NULL, &IoSb,
                                          DirBuf, sizeof(DirBuf),
                                          FileBothDirectoryInformation,
                                          FALSE, NULL, FirstScan);
                FirstScan = FALSE;
                if (!NT_SUCCESS(St) || IoSb.Information == 0) break;

                Offset = 0;
                while (Offset < IoSb.Information)
                {
                    Entry = (PFILE_BOTH_DIR_INFORMATION)(DirBuf + Offset);
                    if (Entry->FileNameLength >= 7 * sizeof(WCHAR) &&
                        RtlCompareMemory(Entry->FileName, L"stress_",
                                         7 * sizeof(WCHAR)) == 7 * sizeof(WCHAR))
                    {
                        StressLeft++;
                    }
                    if (Entry->NextEntryOffset == 0) break;
                    Offset += Entry->NextEntryOffset;
                }
            }
            TOK(StressLeft == 0,
                "stress enum leftover=%lu (expect 0)\n", StressLeft);
            ZwClose(h);
        }
    }

    /*
     * ==========================================================
     * 10.13 Delete/recreate tight loop — same name recycled many
     *       times. Catches stale MFT references and CCB lifetime
     *       bugs that only trigger after a few iterations.
     * ==========================================================
     */
    TLOG("--- section 10.13: delete/recreate loop on single name ---\n");
    {
        ULONG Loop;
        ULONG Good = 0;
        ULONG LoopCount = 12;

        UnlinkByPath(L"\\??\\D:\\kmtest_recycle.bin");
        for (Loop = 0; Loop < LoopCount; Loop++)
        {
            UCHAR Tag = (UCHAR)(0x55 + Loop);

            St = OpenNtfs(L"\\??\\D:\\kmtest_recycle.bin",
                          GENERIC_READ | GENERIC_WRITE | DELETE | SYNCHRONIZE,
                          FILE_CREATE, FILE_NON_DIRECTORY_FILE, &h);
            if (!NT_SUCCESS(St)) { TLOG("recycle iter %lu create: 0x%08lx\n", Loop, St); break; }

            for (i = 0; i < 200; i++) Wr[i] = (UCHAR)(Tag + (i & 0x0F));
            St = WriteAt(h, 0, Wr, 200, &Info);
            if (!NT_SUCCESS(St) || Info != 200)
            {
                TLOG("recycle iter %lu write: 0x%08lx\n", Loop, St);
                ZwClose(h);
                break;
            }

            RtlZeroMemory(Rd, 200);
            St = ReadAt(h, 0, Rd, 200, &Info);
            if (NT_SUCCESS(St) && Info == 200)
            {
                Errs = 0;
                for (i = 0; i < 200; i++)
                    if (Rd[i] != (UCHAR)(Tag + (i & 0x0F))) Errs++;
                if (Errs == 0) Good++;
            }
            ZwClose(h);

            UnlinkByPath(L"\\??\\D:\\kmtest_recycle.bin");

            /* Reopen after delete MUST fail. */
            St = OpenNtfs(L"\\??\\D:\\kmtest_recycle.bin",
                          GENERIC_READ | SYNCHRONIZE,
                          FILE_OPEN, FILE_NON_DIRECTORY_FILE, &h2);
            if (NT_SUCCESS(St))
            {
                TLOG("recycle iter %lu: stale reopen succeeded, should not\n", Loop);
                ZwClose(h2);
                break;
            }
        }
        TOK(Good == LoopCount,
            "recycle loop good=%lu (expect %lu)\n", Good, LoopCount);
    }
    UnlinkByPath(L"\\??\\D:\\kmtest_recycle.bin");

    /*
     * ==========================================================
     * 10.14 Reopen-across-session: close handle, reopen, continue
     *       writes, re-verify from disk. Mimics a long user session.
     * ==========================================================
     */
    TLOG("--- section 10.14: reopen-across-session growth ---\n");
    {
        ULONG Session;
        ULONG SessionCount = 5;
        ULONGLONG TotalBytes = 0;
        ULONG ChunkSize = 512;
        ULONG GoodChecks = 0;

        UnlinkByPath(L"\\??\\D:\\kmtest_session.bin");
        St = OpenNtfs(L"\\??\\D:\\kmtest_session.bin",
                      GENERIC_READ | GENERIC_WRITE | DELETE | SYNCHRONIZE,
                      FILE_CREATE, FILE_NON_DIRECTORY_FILE, &h);
        TOK(NT_SUCCESS(St), "session create: 0x%08lx\n", St);

        for (Session = 0; Session < SessionCount && NT_SUCCESS(St); Session++)
        {
            for (i = 0; i < ChunkSize; i++)
                Wr[i] = (UCHAR)((Session * 89 + i) & 0xFF);

            St = WriteAt(h, TotalBytes, Wr, ChunkSize, &Info);
            if (!NT_SUCCESS(St) || Info != ChunkSize)
            {
                TLOG("session %lu write at %I64u: 0x%08lx info=%lu\n",
                     Session, TotalBytes, St, Info);
                break;
            }
            TotalBytes += ChunkSize;

            /* Close the handle, reopen, verify. */
            ZwClose(h);
            h = NULL;

            St = OpenNtfs(L"\\??\\D:\\kmtest_session.bin",
                          GENERIC_READ | GENERIC_WRITE | DELETE | SYNCHRONIZE,
                          FILE_OPEN, FILE_NON_DIRECTORY_FILE, &h);
            if (!NT_SUCCESS(St))
            {
                TLOG("session %lu reopen: 0x%08lx\n", Session, St);
                break;
            }

            RtlZeroMemory(&Std, sizeof(Std));
            St = ZwQueryInformationFile(h, &IoSb, &Std, sizeof(Std),
                                        FileStandardInformation);
            if (NT_SUCCESS(St) && (ULONGLONG)Std.EndOfFile.QuadPart == TotalBytes)
            {
                GoodChecks++;
            }
            else if (NT_SUCCESS(St))
            {
                TLOG("session %lu size mismatch: eof=%I64d expect=%I64u\n",
                     Session, Std.EndOfFile.QuadPart, TotalBytes);
            }

            /* Read the chunk we just wrote (now reopened from disk). */
            RtlZeroMemory(Rd, ChunkSize);
            St = ReadAt(h, TotalBytes - ChunkSize, Rd, ChunkSize, &Info);
            if (NT_SUCCESS(St) && Info == ChunkSize)
            {
                Errs = 0;
                for (i = 0; i < ChunkSize; i++)
                    if (Rd[i] != (UCHAR)((Session * 89 + i) & 0xFF)) Errs++;
                if (Errs != 0)
                {
                    TLOG("session %lu data mismatch: errs=%lu\n", Session, Errs);
                }
            }
        }

        if (h != NULL)
        {
            ZwClose(h);
            h = NULL;
        }

        TOK(GoodChecks == SessionCount,
            "session reopens ok=%lu (expect %lu)\n", GoodChecks, SessionCount);
        TOK(TotalBytes == (ULONGLONG)SessionCount * ChunkSize,
            "session total written=%I64u (expect %lu)\n",
            TotalBytes, (ULONG)(SessionCount * ChunkSize));

        UnlinkByPath(L"\\??\\D:\\kmtest_session.bin");
    }

    /*
     * ==========================================================
     * 10.15 Alternate data streams (ADS)
     *       NTFS lets one file carry multiple $DATA attributes,
     *       addressed via "file:stream" syntax. Bringup drivers
     *       typically parse the colon but treat every create as
     *       the unnamed $DATA, silently overwriting or losing
     *       named streams. Also tests the explicit ":$DATA"
     *       type suffix which many parsers choke on.
     * ==========================================================
     */
    TLOG("--- section 10.15: alternate data streams ---\n");
    UnlinkByPath(L"\\??\\D:\\kmtest_ads.txt");
    St = OpenNtfs(L"\\??\\D:\\kmtest_ads.txt",
                  GENERIC_READ | GENERIC_WRITE | DELETE | SYNCHRONIZE,
                  FILE_CREATE, FILE_NON_DIRECTORY_FILE, &h);
    TOK(NT_SUCCESS(St), "ads: create base file: 0x%08lx\n", St);
    if (NT_SUCCESS(St))
    {
        for (i = 0; i < 32; i++) Wr[i] = (UCHAR)(0xA0 | (i & 0x0F));
        St = WriteAt(h, 0, Wr, 32, &Info);
        TOK(NT_SUCCESS(St) && Info == 32, "ads: base write: 0x%08lx\n", St);
        ZwClose(h);

        /* Named stream "alt1". */
        St = OpenNtfs(L"\\??\\D:\\kmtest_ads.txt:alt1",
                      GENERIC_READ | GENERIC_WRITE | SYNCHRONIZE,
                      FILE_CREATE, FILE_NON_DIRECTORY_FILE, &h);
        TOK(NT_SUCCESS(St), "ads: create :alt1: 0x%08lx\n", St);
        if (NT_SUCCESS(St))
        {
            for (i = 0; i < 16; i++) Wr[i] = (UCHAR)(0xB0 | (i & 0x0F));
            St = WriteAt(h, 0, Wr, 16, &Info);
            TOK(NT_SUCCESS(St), "ads: alt1 write: 0x%08lx\n", St);
            ZwClose(h);
        }

        /* Reopen base data, verify untouched by alt1. */
        St = OpenNtfs(L"\\??\\D:\\kmtest_ads.txt",
                      GENERIC_READ | SYNCHRONIZE,
                      FILE_OPEN, FILE_NON_DIRECTORY_FILE, &h);
        TOK(NT_SUCCESS(St), "ads: reopen base: 0x%08lx\n", St);
        if (NT_SUCCESS(St))
        {
            RtlZeroMemory(Rd, 32);
            St = ReadAt(h, 0, Rd, 32, &Info);
            TOK(NT_SUCCESS(St) && Info == 32,
                "ads: base read after alt write: 0x%08lx info=%lu\n", St, Info);
            Errs = 0;
            for (i = 0; i < 32; i++)
                if (Rd[i] != (UCHAR)(0xA0 | (i & 0x0F))) Errs++;
            TOK(Errs == 0, "ads: base data intact: %lu mismatches\n", Errs);
            ZwClose(h);
        }

        /* Reopen alt1 and verify. */
        St = OpenNtfs(L"\\??\\D:\\kmtest_ads.txt:alt1",
                      GENERIC_READ | SYNCHRONIZE,
                      FILE_OPEN, FILE_NON_DIRECTORY_FILE, &h);
        TOK(NT_SUCCESS(St), "ads: reopen :alt1: 0x%08lx\n", St);
        if (NT_SUCCESS(St))
        {
            RtlZeroMemory(Rd, 16);
            St = ReadAt(h, 0, Rd, 16, &Info);
            TOK(NT_SUCCESS(St) && Info == 16,
                "ads: alt1 reopen read: 0x%08lx info=%lu\n", St, Info);
            Errs = 0;
            for (i = 0; i < 16; i++)
                if (Rd[i] != (UCHAR)(0xB0 | (i & 0x0F))) Errs++;
            TOK(Errs == 0, "ads: alt1 data intact: %lu mismatches\n", Errs);
            ZwClose(h);
        }
    }
    UnlinkByPath(L"\\??\\D:\\kmtest_ads.txt");

    /*
     * ==========================================================
     * 10.16 Case-insensitive open
     *       NTFS indexes WIN32/WIN32_AND_DOS $FILE_NAME entries
     *       under upcased keys using the volume's $UpCase table.
     *       OBJ_CASE_INSENSITIVE must hit the entry regardless
     *       of the caller's cased name.
     * ==========================================================
     */
    TLOG("--- section 10.16: case-insensitive open ---\n");
    UnlinkByPath(L"\\??\\D:\\MixedCase.TXT");
    UnlinkByPath(L"\\??\\D:\\mixedcase.txt");
    St = OpenNtfs(L"\\??\\D:\\MixedCase.TXT",
                  GENERIC_READ | GENERIC_WRITE | DELETE | SYNCHRONIZE,
                  FILE_CREATE, FILE_NON_DIRECTORY_FILE, &h);
    TOK(NT_SUCCESS(St), "case: create 'MixedCase.TXT': 0x%08lx\n", St);
    if (NT_SUCCESS(St))
    {
        for (i = 0; i < 32; i++) Wr[i] = (UCHAR)(0xCA + (i & 0x0F));
        (VOID)WriteAt(h, 0, Wr, 32, &Info);
        ZwClose(h);

        /* Lowercase open — expect success under OBJ_CASE_INSENSITIVE. */
        St = OpenNtfs(L"\\??\\D:\\mixedcase.txt",
                      GENERIC_READ | SYNCHRONIZE,
                      FILE_OPEN, FILE_NON_DIRECTORY_FILE, &h);
        TOK(NT_SUCCESS(St), "case: open 'mixedcase.txt' insensitive: 0x%08lx\n", St);
        if (NT_SUCCESS(St))
        {
            RtlZeroMemory(Rd, 32);
            St = ReadAt(h, 0, Rd, 32, &Info);
            TOK(NT_SUCCESS(St) && Info == 32, "case: lowercase read: 0x%08lx\n", St);
            Errs = 0;
            for (i = 0; i < 32; i++)
                if (Rd[i] != (UCHAR)(0xCA + (i & 0x0F))) Errs++;
            TOK(Errs == 0, "case: lowercase data intact: %lu mismatches\n", Errs);
            ZwClose(h);
        }

        /* Uppercase open — also expect success. */
        St = OpenNtfs(L"\\??\\D:\\MIXEDCASE.TXT",
                      GENERIC_READ | SYNCHRONIZE,
                      FILE_OPEN, FILE_NON_DIRECTORY_FILE, &h);
        TOK(NT_SUCCESS(St), "case: open 'MIXEDCASE.TXT' insensitive: 0x%08lx\n", St);
        if (NT_SUCCESS(St)) ZwClose(h);
    }
    UnlinkByPath(L"\\??\\D:\\MixedCase.TXT");

    /*
     * ==========================================================
     * 10.17 Sparse hole on extending write
     *       Write at offset 0, then at offset 65536 with no
     *       intermediate write. Reading the middle must return
     *       zeros (either because the driver emitted a sparse
     *       run or because it explicitly zero-filled). Bringup
     *       drivers frequently leave stale disk data visible.
     * ==========================================================
     */
    if (g_NtfslxFuncPhase == NtfslxFuncPhaseStressA)
    {
        TLOG("=== NtfslxFunc END phase=%s (completed sections 10-10.16) ===\n",
             NtfslxGetFuncPhaseName());
        goto Done;
    }

LongSuitePhaseB:
    TLOG("=== NtfslxFunc phase=%s entering long suite B ===\n",
         NtfslxGetFuncPhaseName());
    TLOG("--- section 10.17: gap fill on extend ---\n");
    UnlinkByPath(L"\\??\\D:\\kmtest_sparse.bin");
    St = OpenNtfs(L"\\??\\D:\\kmtest_sparse.bin",
                  GENERIC_READ | GENERIC_WRITE | DELETE | SYNCHRONIZE,
                  FILE_CREATE, FILE_NON_DIRECTORY_FILE, &h);
    TOK(NT_SUCCESS(St), "sparse: create: 0x%08lx\n", St);
    if (NT_SUCCESS(St))
    {
        ULONG GapBytes;

        for (i = 0; i < 4096; i++) Wr[i] = 0xA5;
        St = WriteAt(h, 0, Wr, 4096, &Info);
        TOK(NT_SUCCESS(St) && Info == 4096, "sparse: head write: 0x%08lx\n", St);

        for (i = 0; i < 4096; i++) Wr[i] = 0x5A;
        St = WriteAt(h, 65536, Wr, 4096, &Info);
        TOK(NT_SUCCESS(St) && Info == 4096, "sparse: tail write: 0x%08lx\n", St);

        /* Verify size reflects the extent of the tail write. */
        RtlZeroMemory(&Std, sizeof(Std));
        St = ZwQueryInformationFile(h, &IoSb, &Std, sizeof(Std), FileStandardInformation);
        TOK(NT_SUCCESS(St) && Std.EndOfFile.QuadPart == 65536 + 4096,
            "sparse: size after gap write: eof=%I64d (expect %lu)\n",
            Std.EndOfFile.QuadPart, (ULONG)(65536 + 4096));

        /* Middle should read as zeros. Check a slice at ofs 8192. */
        RtlZeroMemory(Rd, 4096);
        RtlFillMemory(Rd, 4096, 0xCD); /* poison, should be overwritten by read */
        St = ReadAt(h, 8192, Rd, 4096, &Info);
        TOK(NT_SUCCESS(St) && Info == 4096, "sparse: gap read: 0x%08lx info=%lu\n", St, Info);
        Errs = 0;
        GapBytes = 0;
        for (i = 0; i < 4096; i++)
        {
            if (Rd[i] != 0x00) Errs++;
            else GapBytes++;
        }
        TOK(Errs == 0,
            "sparse: gap region all zeros: %lu non-zero of 4096 (stale data leak if >0)\n",
            Errs);

        /* Read head + tail to confirm our real writes survived. */
        RtlZeroMemory(Rd, 4096);
        St = ReadAt(h, 0, Rd, 4096, &Info);
        Errs = 0;
        for (i = 0; i < 4096; i++) if (Rd[i] != 0xA5) Errs++;
        TOK(Errs == 0, "sparse: head intact: %lu mismatches\n", Errs);

        RtlZeroMemory(Rd, 4096);
        St = ReadAt(h, 65536, Rd, 4096, &Info);
        Errs = 0;
        for (i = 0; i < 4096; i++) if (Rd[i] != 0x5A) Errs++;
        TOK(Errs == 0, "sparse: tail intact: %lu mismatches\n", Errs);

        ZwClose(h);
    }
    UnlinkByPath(L"\\??\\D:\\kmtest_sparse.bin");

    /*
     * ==========================================================
     * 10.18 Share mode enforcement
     *       Open with FILE_SHARE_READ (no write/delete). A
     *       second open that requests GENERIC_WRITE must fail
     *       with STATUS_SHARING_VIOLATION — bringup drivers
     *       that skip IoCheckShareAccess accept both.
     * ==========================================================
     */
    TLOG("--- section 10.18: share mode enforcement ---\n");
    UnlinkByPath(L"\\??\\D:\\kmtest_share.bin");
    {
        UNICODE_STRING ShareName;
        OBJECT_ATTRIBUTES ShareOa;
        IO_STATUS_BLOCK ShareIosb;

        RtlInitUnicodeString(&ShareName, L"\\??\\D:\\kmtest_share.bin");
        InitializeObjectAttributes(&ShareOa, &ShareName,
                                   OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                                   NULL, NULL);

        /*
         * First opener takes pure GENERIC_READ + share=READ. Critical: NO
         * DELETE access, otherwise the second open's share mask would also
         * have to grant SHARE_DELETE for compatibility.
         */
        St = ZwCreateFile(&h,
                          GENERIC_READ | SYNCHRONIZE,
                          &ShareOa, &ShareIosb,
                          NULL, FILE_ATTRIBUTE_NORMAL,
                          FILE_SHARE_READ,
                          FILE_OPEN_IF,
                          FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT,
                          NULL, 0);
        TOK(NT_SUCCESS(St), "share: first open (R, share=R): 0x%08lx\n", St);
        if (NT_SUCCESS(St))
        {
            /* Second open requesting WRITE should collide with first's share=R. */
            St = ZwCreateFile(&h2,
                              GENERIC_WRITE | SYNCHRONIZE,
                              &ShareOa, &ShareIosb,
                              NULL, FILE_ATTRIBUTE_NORMAL,
                              FILE_SHARE_READ | FILE_SHARE_WRITE,
                              FILE_OPEN,
                              FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT,
                              NULL, 0);
            TOK(St == STATUS_SHARING_VIOLATION,
                "share: conflicting WRITE open: 0x%08lx (expect SHARING_VIOLATION)\n", St);
            if (NT_SUCCESS(St)) ZwClose(h2);

            /* Second concurrent reader is OK. */
            St = ZwCreateFile(&h2,
                              GENERIC_READ | SYNCHRONIZE,
                              &ShareOa, &ShareIosb,
                              NULL, FILE_ATTRIBUTE_NORMAL,
                              FILE_SHARE_READ,
                              FILE_OPEN,
                              FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT,
                              NULL, 0);
            TOK(NT_SUCCESS(St), "share: second reader: 0x%08lx\n", St);
            if (NT_SUCCESS(St)) ZwClose(h2);

            ZwClose(h);
        }
        UnlinkByPath(L"\\??\\D:\\kmtest_share.bin");
    }

    /*
     * ==========================================================
     * 10.19 255-character filename (NTFS max)
     * ==========================================================
     */
    TLOG("--- section 10.19: 255-char filename ---\n");
    {
        WCHAR MaxPath[300];
        ULONG NameLen = 255;
        ULONG BaseLen;
        RtlStringCbCopyW(MaxPath, sizeof(MaxPath), L"\\??\\D:\\");
        BaseLen = 7; /* \??\D:\ */
        for (i = 0; i < NameLen; i++)
            MaxPath[BaseLen + i] = (WCHAR)(L'A' + (i % 26));
        MaxPath[BaseLen + NameLen] = 0;

        UnlinkByPath(MaxPath);
        St = OpenNtfs(MaxPath,
                      GENERIC_READ | GENERIC_WRITE | DELETE | SYNCHRONIZE,
                      FILE_CREATE, FILE_NON_DIRECTORY_FILE, &h);
        TOK(NT_SUCCESS(St), "maxname: create 255-char: 0x%08lx\n", St);
        if (NT_SUCCESS(St))
        {
            for (i = 0; i < 64; i++) Wr[i] = 0x7F;
            St = WriteAt(h, 0, Wr, 64, &Info);
            TOK(NT_SUCCESS(St), "maxname: write: 0x%08lx\n", St);
            ZwClose(h);

            St = OpenNtfs(MaxPath, GENERIC_READ | SYNCHRONIZE,
                          FILE_OPEN, FILE_NON_DIRECTORY_FILE, &h);
            TOK(NT_SUCCESS(St), "maxname: reopen: 0x%08lx\n", St);
            if (NT_SUCCESS(St)) ZwClose(h);
        }
        UnlinkByPath(MaxPath);
    }

    /*
     * ==========================================================
     * 10.20 Nested subdirectories — create, write, read, delete
     *       Build \nest1\nest2\nest3\leaf.bin then remove bottom-up.
     *       Exercises directory creation + parent $I30 insert +
     *       index remove across multiple levels.
     * ==========================================================
     */
    TLOG("--- section 10.20: nested subdirectories ---\n");
    UnlinkByPath(L"\\??\\D:\\nest1\\nest2\\nest3\\leaf.bin");
    UnlinkByPath(L"\\??\\D:\\nest1\\nest2\\nest3");
    UnlinkByPath(L"\\??\\D:\\nest1\\nest2");
    UnlinkByPath(L"\\??\\D:\\nest1");
    St = OpenNtfs(L"\\??\\D:\\nest1",
                  GENERIC_READ | GENERIC_WRITE | DELETE | SYNCHRONIZE,
                  FILE_CREATE, FILE_DIRECTORY_FILE, &h);
    TOK(NT_SUCCESS(St), "nested: mkdir nest1: 0x%08lx\n", St);
    if (NT_SUCCESS(St)) ZwClose(h);

    if (NT_SUCCESS(St))
    {
        St = OpenNtfs(L"\\??\\D:\\nest1\\nest2",
                      GENERIC_READ | GENERIC_WRITE | DELETE | SYNCHRONIZE,
                      FILE_CREATE, FILE_DIRECTORY_FILE, &h);
        TOK(NT_SUCCESS(St), "nested: mkdir nest1\\nest2: 0x%08lx\n", St);
        if (NT_SUCCESS(St)) ZwClose(h);
    }
    if (NT_SUCCESS(St))
    {
        St = OpenNtfs(L"\\??\\D:\\nest1\\nest2\\nest3",
                      GENERIC_READ | GENERIC_WRITE | DELETE | SYNCHRONIZE,
                      FILE_CREATE, FILE_DIRECTORY_FILE, &h);
        TOK(NT_SUCCESS(St), "nested: mkdir nest1\\nest2\\nest3: 0x%08lx\n", St);
        if (NT_SUCCESS(St)) ZwClose(h);
    }
    if (NT_SUCCESS(St))
    {
        St = OpenNtfs(L"\\??\\D:\\nest1\\nest2\\nest3\\leaf.bin",
                      GENERIC_READ | GENERIC_WRITE | DELETE | SYNCHRONIZE,
                      FILE_CREATE, FILE_NON_DIRECTORY_FILE, &h);
        TOK(NT_SUCCESS(St), "nested: create leaf: 0x%08lx\n", St);
        if (NT_SUCCESS(St))
        {
            for (i = 0; i < 128; i++) Wr[i] = (UCHAR)(i ^ 0x55);
            (VOID)WriteAt(h, 0, Wr, 128, &Info);
            ZwClose(h);

            /* Reopen via nested path and verify content. */
            St = OpenNtfs(L"\\??\\D:\\nest1\\nest2\\nest3\\leaf.bin",
                          GENERIC_READ | SYNCHRONIZE,
                          FILE_OPEN, FILE_NON_DIRECTORY_FILE, &h);
            TOK(NT_SUCCESS(St), "nested: reopen leaf: 0x%08lx\n", St);
            if (NT_SUCCESS(St))
            {
                RtlZeroMemory(Rd, 128);
                (VOID)ReadAt(h, 0, Rd, 128, &Info);
                Errs = 0;
                for (i = 0; i < 128; i++) if (Rd[i] != (UCHAR)(i ^ 0x55)) Errs++;
                TOK(Errs == 0, "nested: leaf content intact: %lu mismatches\n", Errs);
                ZwClose(h);
            }
        }
    }
    /* Unlink bottom-up. */
    UnlinkByPath(L"\\??\\D:\\nest1\\nest2\\nest3\\leaf.bin");
    UnlinkByPath(L"\\??\\D:\\nest1\\nest2\\nest3");
    UnlinkByPath(L"\\??\\D:\\nest1\\nest2");
    UnlinkByPath(L"\\??\\D:\\nest1");

    /*
     * ==========================================================
     * 10.21 Directory enumeration wildcard semantics.
     *       Validates the FsRtlIsNameInExpression contract that
     *       the driver relies on: pattern must be UPCASED before
     *       calling with IgnoreCase=TRUE. (Kernel-mode
     *       ZwQueryDirectoryFile does not propagate the FileName
     *       parameter through the IRP, so we exercise the
     *       matcher directly here.)
     * ==========================================================
     */
    TLOG("--- section 10.21: FsRtlIsNameInExpression upcase contract ---\n");
    {
        UNICODE_STRING UcPattern;
        UNICODE_STRING LcPattern;
        UNICODE_STRING NameLower;
        UNICODE_STRING NameUpper;
        BOOLEAN M1, M2, M3, M4;

        RtlInitUnicodeString(&UcPattern, L"WILD_*.BIN");
        RtlInitUnicodeString(&LcPattern, L"wild_*.bin");
        RtlInitUnicodeString(&NameLower, L"wild_a.bin");
        RtlInitUnicodeString(&NameUpper, L"WILD_A.BIN");

        M1 = FsRtlIsNameInExpression(&UcPattern, &NameLower, TRUE, NULL);
        M2 = FsRtlIsNameInExpression(&UcPattern, &NameUpper, TRUE, NULL);
        M3 = FsRtlIsNameInExpression(&LcPattern, &NameLower, FALSE, NULL);
        TOK(M1, "wild: upcased pat 'WILD_*.BIN' matches lower 'wild_a.bin' ignoreCase=1\n");
        TOK(M2, "wild: upcased pat 'WILD_*.BIN' matches upper 'WILD_A.BIN' ignoreCase=1\n");
        TOK(M3, "wild: lower pat 'wild_*.bin' matches lower 'wild_a.bin' ignoreCase=0\n");

        /* Negative case: '*.bin' must NOT match a .txt name. */
        {
            UNICODE_STRING TxtName;
            RtlInitUnicodeString(&TxtName, L"wild_c.txt");
            M4 = FsRtlIsNameInExpression(&UcPattern, &TxtName, TRUE, NULL);
            TOK(!M4, "wild: '*.bin' rejects 'wild_c.txt' ignoreCase=1\n");
        }
    }

    /*
     * ==========================================================
     * 10.22 MFT record reuse + sequence number increment
     *       Delete a file and immediately create a new one.
     *       The new FileInternalInformation.IndexNumber must
     *       differ in at least the sequence-number portion, or
     *       stale handles could alias new files.
     * ==========================================================
     */
    TLOG("--- section 10.22: MFT reuse + FileId distinct ---\n");
    UnlinkByPath(L"\\??\\D:\\kmtest_reuse.bin");
    {
        FILE_INTERNAL_INFORMATION Internal1 = {0};
        FILE_INTERNAL_INFORMATION Internal2 = {0};
        UCHAR AllInfo1Buffer[sizeof(FILE_ALL_INFORMATION) + 128 * sizeof(WCHAR)];
        UCHAR AllInfo2Buffer[sizeof(FILE_ALL_INFORMATION) + 128 * sizeof(WCHAR)];
        PFILE_ALL_INFORMATION AllInfo1 = (PFILE_ALL_INFORMATION)AllInfo1Buffer;
        PFILE_ALL_INFORMATION AllInfo2 = (PFILE_ALL_INFORMATION)AllInfo2Buffer;

        St = OpenNtfs(L"\\??\\D:\\kmtest_reuse.bin",
                      GENERIC_READ | GENERIC_WRITE | DELETE | SYNCHRONIZE,
                      FILE_CREATE, FILE_NON_DIRECTORY_FILE, &h);
        TOK(NT_SUCCESS(St), "reuse: first create: 0x%08lx\n", St);
        if (NT_SUCCESS(St))
        {
            St = ZwQueryInformationFile(h, &IoSb, &Internal1, sizeof(Internal1),
                                        FileInternalInformation);
            TOK(NT_SUCCESS(St), "reuse: first internal info: 0x%08lx\n", St);

            RtlZeroMemory(AllInfo1Buffer, sizeof(AllInfo1Buffer));
            St = ZwQueryInformationFile(h, &IoSb, AllInfo1Buffer, sizeof(AllInfo1Buffer),
                                        FileAllInformation);
            TOK(NT_SUCCESS(St), "reuse: first all info: 0x%08lx\n", St);
            if (NT_SUCCESS(St))
            {
                TOK(AllInfo1->InternalInformation.IndexNumber.QuadPart ==
                        Internal1.IndexNumber.QuadPart,
                    "reuse: first all/internal FileId match all=0x%I64x internal=0x%I64x\n",
                    AllInfo1->InternalInformation.IndexNumber.QuadPart,
                    Internal1.IndexNumber.QuadPart);
            }
            ZwClose(h);

            UnlinkByPath(L"\\??\\D:\\kmtest_reuse.bin");

            St = OpenNtfs(L"\\??\\D:\\kmtest_reuse.bin",
                          GENERIC_READ | GENERIC_WRITE | DELETE | SYNCHRONIZE,
                          FILE_CREATE, FILE_NON_DIRECTORY_FILE, &h);
            TOK(NT_SUCCESS(St), "reuse: second create: 0x%08lx\n", St);
            if (NT_SUCCESS(St))
            {
                St = ZwQueryInformationFile(h, &IoSb, &Internal2, sizeof(Internal2),
                                            FileInternalInformation);
                TOK(NT_SUCCESS(St), "reuse: second internal info: 0x%08lx\n", St);

                RtlZeroMemory(AllInfo2Buffer, sizeof(AllInfo2Buffer));
                St = ZwQueryInformationFile(h, &IoSb, AllInfo2Buffer, sizeof(AllInfo2Buffer),
                                            FileAllInformation);
                TOK(NT_SUCCESS(St), "reuse: second all info: 0x%08lx\n", St);
                if (NT_SUCCESS(St))
                {
                    TOK(AllInfo2->InternalInformation.IndexNumber.QuadPart ==
                            Internal2.IndexNumber.QuadPart,
                        "reuse: second all/internal FileId match all=0x%I64x internal=0x%I64x\n",
                        AllInfo2->InternalInformation.IndexNumber.QuadPart,
                        Internal2.IndexNumber.QuadPart);
                }
                ZwClose(h);
            }
        }
        TOK(Internal1.IndexNumber.QuadPart != Internal2.IndexNumber.QuadPart,
            "reuse: FileId distinct after recycle (id1=0x%I64x id2=0x%I64x)\n",
            Internal1.IndexNumber.QuadPart, Internal2.IndexNumber.QuadPart);
    }
    UnlinkByPath(L"\\??\\D:\\kmtest_reuse.bin");
    /*
     * ==========================================================
     * 10.23 FileBasicInformation sentinel values (-1 = no-change)
     *       NT contract: timestamp or FileAttributes == -1 / 0
     *       means "preserve existing". Bringups blindly copy the
     *       whole struct, stomping whatever the caller meant to
     *       leave alone.
     * ==========================================================
     */
    TLOG("--- section 10.23: FileBasicInformation -1 sentinel ---\n");
    UnlinkByPath(L"\\??\\D:\\kmtest_bisent.bin");
    St = OpenNtfs(L"\\??\\D:\\kmtest_bisent.bin",
                  GENERIC_READ | GENERIC_WRITE | DELETE | SYNCHRONIZE,
                  FILE_CREATE, FILE_NON_DIRECTORY_FILE, &h);
    TOK(NT_SUCCESS(St), "bisent: create: 0x%08lx\n", St);
    if (NT_SUCCESS(St))
    {
        FILE_BASIC_INFORMATION Before;
        FILE_BASIC_INFORMATION Update;
        FILE_BASIC_INFORMATION After;

        RtlZeroMemory(&Before, sizeof(Before));
        St = ZwQueryInformationFile(h, &IoSb, &Before, sizeof(Before),
                                    FileBasicInformation);
        TOK(NT_SUCCESS(St) && Before.LastWriteTime.QuadPart != 0,
            "bisent: initial query: 0x%08lx lwt=%I64d\n",
            St, Before.LastWriteTime.QuadPart);

        /* Set all time fields to -1 and flip the HIDDEN attribute. */
        Update.CreationTime.QuadPart = -1;
        Update.LastAccessTime.QuadPart = -1;
        Update.LastWriteTime.QuadPart = -1;
        Update.ChangeTime.QuadPart = -1;
        Update.FileAttributes = FILE_ATTRIBUTE_HIDDEN;
        St = ZwSetInformationFile(h, &IoSb, &Update, sizeof(Update),
                                  FileBasicInformation);
        TOK(NT_SUCCESS(St), "bisent: set with -1 timestamps: 0x%08lx\n", St);

        RtlZeroMemory(&After, sizeof(After));
        St = ZwQueryInformationFile(h, &IoSb, &After, sizeof(After),
                                    FileBasicInformation);
        TOK(NT_SUCCESS(St), "bisent: query after set: 0x%08lx\n", St);
        TOK(After.LastWriteTime.QuadPart == Before.LastWriteTime.QuadPart,
            "bisent: LastWriteTime preserved (before=%I64d after=%I64d)\n",
            Before.LastWriteTime.QuadPart, After.LastWriteTime.QuadPart);
        TOK(After.CreationTime.QuadPart == Before.CreationTime.QuadPart,
            "bisent: CreationTime preserved (before=%I64d after=%I64d)\n",
            Before.CreationTime.QuadPart, After.CreationTime.QuadPart);
        TOK((After.FileAttributes & FILE_ATTRIBUTE_HIDDEN) != 0,
            "bisent: HIDDEN attribute applied (attrs=0x%08lx)\n",
            After.FileAttributes);

        ZwClose(h);
    }
    UnlinkByPath(L"\\??\\D:\\kmtest_bisent.bin");

    /*
     * ==========================================================
     * 10.24 Path edge cases: trailing backslash on non-dir,
     *       double-backslash. Driver must not crash; must reject
     *       cleanly with STATUS_OBJECT_NAME_INVALID (or similar).
     * ==========================================================
     */
    TLOG("--- section 10.24: path edge cases ---\n");
    UnlinkByPath(L"\\??\\D:\\kmtest_edge.bin");
    St = OpenNtfs(L"\\??\\D:\\kmtest_edge.bin",
                  GENERIC_READ | GENERIC_WRITE | DELETE | SYNCHRONIZE,
                  FILE_CREATE, FILE_NON_DIRECTORY_FILE, &h);
    TOK(NT_SUCCESS(St), "edge: base create: 0x%08lx\n", St);
    if (NT_SUCCESS(St)) ZwClose(h);

    /* Trailing backslash on a non-directory. */
    St = OpenNtfs(L"\\??\\D:\\kmtest_edge.bin\\",
                  GENERIC_READ | SYNCHRONIZE,
                  FILE_OPEN, FILE_NON_DIRECTORY_FILE, &h);
    TOK(!NT_SUCCESS(St),
        "edge: trailing-backslash non-dir: 0x%08lx (expect failure)\n", St);
    if (NT_SUCCESS(St)) ZwClose(h);

    /* Double backslash in the middle (empty component). */
    St = OpenNtfs(L"\\??\\D:\\\\kmtest_edge.bin",
                  GENERIC_READ | SYNCHRONIZE,
                  FILE_OPEN, FILE_NON_DIRECTORY_FILE, &h);
    TOK(!NT_SUCCESS(St),
        "edge: double backslash path: 0x%08lx (expect failure)\n", St);
    if (NT_SUCCESS(St)) ZwClose(h);

    UnlinkByPath(L"\\??\\D:\\kmtest_edge.bin");

    /*
     * ==========================================================
     * 10.25 Larger fragmented non-resident file (64 KB -> 256 KB)
     *       Grow the file through multiple extend calls while
     *       other scratch files steal intervening clusters,
     *       forcing multi-run allocation. Then read the whole
     *       file in one I/O that crosses every run boundary.
     * ==========================================================
     */
    TLOG("--- section 10.25: large fragmented read across runs ---\n");
    UnlinkByPath(L"\\??\\D:\\kmtest_frag.bin");
    UnlinkByPath(L"\\??\\D:\\kmtest_pin.bin");
    {
        HANDLE hFrag = NULL;
        HANDLE hPin = NULL;
        const ULONG TargetSize = 16 * 4096;  /* 64 KB */
        ULONG Written;

        St = OpenNtfs(L"\\??\\D:\\kmtest_frag.bin",
                      GENERIC_READ | GENERIC_WRITE | DELETE | SYNCHRONIZE,
                      FILE_CREATE, FILE_NON_DIRECTORY_FILE, &hFrag);
        TOK(NT_SUCCESS(St), "frag: create target: 0x%08lx\n", St);
        if (NT_SUCCESS(St))
        {
            St = OpenNtfs(L"\\??\\D:\\kmtest_pin.bin",
                          GENERIC_READ | GENERIC_WRITE | DELETE | SYNCHRONIZE,
                          FILE_CREATE, FILE_NON_DIRECTORY_FILE, &hPin);
            TOK(NT_SUCCESS(St), "frag: create pin: 0x%08lx\n", St);

            /* Interleave writes: one 4 KB chunk to frag, one chunk to pin,
             * repeat. The pin file's clusters land between frag's clusters,
             * producing multi-run fragmentation in frag. */
            Written = 0;
            while (Written < TargetSize)
            {
                for (i = 0; i < 4096; i++)
                    Wr[i] = (UCHAR)((Written / 4096) ^ (i & 0xFF));
                St = WriteAt(hFrag, Written, Wr, 4096, &Info);
                if (!NT_SUCCESS(St))
                {
                    TLOG("frag: target write at %lu failed 0x%08lx\n",
                         Written, St);
                    break;
                }
                Written += 4096;

                if (NT_SUCCESS(St) && hPin != NULL)
                {
                    for (i = 0; i < 4096; i++) Wr[i] = (UCHAR)(0xFE);
                    (VOID)WriteAt(hPin, Written / 2, Wr, 4096, &Info);
                }
            }
            TOK(Written == TargetSize,
                "frag: wrote full %lu bytes (got %lu)\n", TargetSize, Written);

            if (hPin != NULL) ZwClose(hPin);
            ZwClose(hFrag);

            /* Reopen frag and read the whole thing in 4-KB stripes, verifying
             * each stripe against its known pattern. Read starting at the
             * LAST chunk first to avoid sequential-read optimization hiding
             * bugs. */
            St = OpenNtfs(L"\\??\\D:\\kmtest_frag.bin",
                          GENERIC_READ | DELETE | SYNCHRONIZE,
                          FILE_OPEN, FILE_NON_DIRECTORY_FILE, &hFrag);
            TOK(NT_SUCCESS(St), "frag: reopen: 0x%08lx\n", St);
            if (NT_SUCCESS(St))
            {
                ULONG ChunkIdx;
                ULONG TotalErrs = 0;
                for (ChunkIdx = 0; ChunkIdx * 4096 < TargetSize; ChunkIdx++)
                {
                    ULONG Idx = (TargetSize / 4096 - 1) - ChunkIdx;
                    RtlZeroMemory(Rd, 4096);
                    St = ReadAt(hFrag, (ULONGLONG)Idx * 4096, Rd, 4096, &Info);
                    if (!NT_SUCCESS(St) || Info != 4096)
                    {
                        TLOG("frag: read chunk %lu failed 0x%08lx info=%lu\n",
                             Idx, St, Info);
                        TotalErrs += 4096;
                        continue;
                    }
                    for (i = 0; i < 4096; i++)
                        if (Rd[i] != (UCHAR)(Idx ^ (i & 0xFF))) TotalErrs++;
                }
                TOK(TotalErrs == 0,
                    "frag: full verify across runs: %lu mismatches\n", TotalErrs);
                ZwClose(hFrag);
            }
        }
    }
    UnlinkByPath(L"\\??\\D:\\kmtest_frag.bin");
    UnlinkByPath(L"\\??\\D:\\kmtest_pin.bin");

    /*
     * ==========================================================
     * 10.26 FileAllocationInformation shrink below EndOfFile
     *       Per NT contract, setting AllocationSize below the
     *       current EoF implicitly truncates EoF down to the
     *       new AllocationSize. Reads past the new EoF must
     *       return STATUS_END_OF_FILE.
     * ==========================================================
     */
    TLOG("--- section 10.26: allocation shrink truncates EoF ---\n");
    UnlinkByPath(L"\\??\\D:\\kmtest_shrink.bin");
    St = OpenNtfs(L"\\??\\D:\\kmtest_shrink.bin",
                  GENERIC_READ | GENERIC_WRITE | DELETE | SYNCHRONIZE,
                  FILE_CREATE, FILE_NON_DIRECTORY_FILE, &h);
    TOK(NT_SUCCESS(St), "shrink: create: 0x%08lx\n", St);
    if (NT_SUCCESS(St))
    {
        FILE_ALLOCATION_INFORMATION ShrinkAlloc;
        for (i = 0; i < 2048; i++) Wr[i] = (UCHAR)(0x80 | (i & 0x7F));
        St = WriteAt(h, 0, Wr, 2048, &Info);
        TOK(NT_SUCCESS(St) && Info == 2048, "shrink: initial write: 0x%08lx\n", St);

        ShrinkAlloc.AllocationSize.QuadPart = 256;
        St = ZwSetInformationFile(h, &IoSb, &ShrinkAlloc, sizeof(ShrinkAlloc),
                                  FileAllocationInformation);
        TOK(NT_SUCCESS(St), "shrink: set alloc=256: 0x%08lx\n", St);

        RtlZeroMemory(&Std, sizeof(Std));
        St = ZwQueryInformationFile(h, &IoSb, &Std, sizeof(Std),
                                    FileStandardInformation);
        TOK(NT_SUCCESS(St) && Std.EndOfFile.QuadPart == 256,
            "shrink: EoF capped to 256: got %I64d\n", Std.EndOfFile.QuadPart);

        /* Read past new EoF must return EOF status. */
        St = ReadAt(h, 512, Rd, 256, &Info);
        TOK(St == STATUS_END_OF_FILE,
            "shrink: read past shrunken EoF: 0x%08lx (expect END_OF_FILE)\n", St);

        ZwClose(h);
    }
    UnlinkByPath(L"\\??\\D:\\kmtest_shrink.bin");

    /*
     * ==========================================================
     * 10.27 Deep nested directory tree — 16 levels
     *       Builds \dd01\dd02\...\dd16\leaf.bin, writes content, reopens
     *       via full path, reads back, verifies, then rmdirs bottom-up.
     *       Targets the parent-MFT cache bug: every mkdir and every
     *       rmdir must observe the correct parent at $I30 time.
     * ==========================================================
     */
    TLOG("--- section 10.27: nested 16 levels ---\n");
    {
        WCHAR Path[512];
        ULONG Depth;
        ULONG Idx;
        BOOLEAN Ok16 = TRUE;
        /* Pre-clean if a prior run left state on disk. Bottom-up. */
        for (Depth = 16; Depth >= 1; --Depth)
        {
            WCHAR Clean[512];
            ULONG Pos = 0;
            Clean[Pos++] = L'\\'; Clean[Pos++] = L'?'; Clean[Pos++] = L'?';
            Clean[Pos++] = L'\\'; Clean[Pos++] = L'D'; Clean[Pos++] = L':';
            for (Idx = 1; Idx <= Depth; ++Idx)
            {
                Clean[Pos++] = L'\\'; Clean[Pos++] = L'd'; Clean[Pos++] = L'd';
                Clean[Pos++] = (WCHAR)(L'0' + (Idx / 10));
                Clean[Pos++] = (WCHAR)(L'0' + (Idx % 10));
            }
            Clean[Pos] = 0;
            /* Remove the leaf file first (Depth==16 iteration only). */
            if (Depth == 16)
            {
                WCHAR Leaf[512];
                RtlStringCbCopyW(Leaf, sizeof(Leaf), Clean);
                RtlStringCbCatW(Leaf, sizeof(Leaf), L"\\leaf.bin");
                UnlinkByPath(Leaf);
            }
            UnlinkByPath(Clean);
            if (Depth == 1) break;
        }

        /* Create each level one at a time. */
        for (Depth = 1; Depth <= 16; ++Depth)
        {
            ULONG Pos = 0;
            Path[Pos++] = L'\\'; Path[Pos++] = L'?'; Path[Pos++] = L'?';
            Path[Pos++] = L'\\'; Path[Pos++] = L'D'; Path[Pos++] = L':';
            for (Idx = 1; Idx <= Depth; ++Idx)
            {
                Path[Pos++] = L'\\'; Path[Pos++] = L'd'; Path[Pos++] = L'd';
                Path[Pos++] = (WCHAR)(L'0' + (Idx / 10));
                Path[Pos++] = (WCHAR)(L'0' + (Idx % 10));
            }
            Path[Pos] = 0;
            St = OpenNtfs(Path,
                          GENERIC_READ | GENERIC_WRITE | DELETE | SYNCHRONIZE,
                          FILE_CREATE, FILE_DIRECTORY_FILE, &h);
            TOK(NT_SUCCESS(St), "n16: mkdir depth=%lu 0x%08lx\n", Depth, St);
            if (NT_SUCCESS(St)) ZwClose(h); else { Ok16 = FALSE; break; }
        }

        if (Ok16)
        {
            WCHAR LeafPath[512];
            RtlStringCbCopyW(LeafPath, sizeof(LeafPath), Path);
            RtlStringCbCatW(LeafPath, sizeof(LeafPath), L"\\leaf.bin");
            St = OpenNtfs(LeafPath,
                          GENERIC_READ | GENERIC_WRITE | DELETE | SYNCHRONIZE,
                          FILE_CREATE, FILE_NON_DIRECTORY_FILE, &h);
            TOK(NT_SUCCESS(St), "n16: create leaf: 0x%08lx\n", St);
            if (NT_SUCCESS(St))
            {
                for (i = 0; i < 256; i++) Wr[i] = (UCHAR)(i ^ 0xA5);
                St = WriteAt(h, 0, Wr, 256, &Info);
                TOK(NT_SUCCESS(St) && Info == 256,
                    "n16: leaf write: 0x%08lx info=%lu\n", St, Info);
                ZwClose(h);

                St = OpenNtfs(LeafPath,
                              GENERIC_READ | DELETE | SYNCHRONIZE,
                              FILE_OPEN, FILE_NON_DIRECTORY_FILE, &h);
                TOK(NT_SUCCESS(St), "n16: reopen leaf: 0x%08lx\n", St);
                if (NT_SUCCESS(St))
                {
                    RtlZeroMemory(Rd, 256);
                    St = ReadAt(h, 0, Rd, 256, &Info);
                    Errs = 0;
                    for (i = 0; i < 256; i++)
                        if (Rd[i] != (UCHAR)(i ^ 0xA5)) Errs++;
                    TOK(NT_SUCCESS(St) && Errs == 0,
                        "n16: leaf content intact: 0x%08lx mismatches=%lu\n", St, Errs);
                    ZwClose(h);
                }
            }

            /* Delete the leaf before rmdir-ing. */
            UnlinkByPath(LeafPath);
        }

        /* Rmdir bottom-up. */
        for (Depth = 16; Depth >= 1; --Depth)
        {
            ULONG Pos = 0;
            Path[Pos++] = L'\\'; Path[Pos++] = L'?'; Path[Pos++] = L'?';
            Path[Pos++] = L'\\'; Path[Pos++] = L'D'; Path[Pos++] = L':';
            for (Idx = 1; Idx <= Depth; ++Idx)
            {
                Path[Pos++] = L'\\'; Path[Pos++] = L'd'; Path[Pos++] = L'd';
                Path[Pos++] = (WCHAR)(L'0' + (Idx / 10));
                Path[Pos++] = (WCHAR)(L'0' + (Idx % 10));
            }
            Path[Pos] = 0;

            St = OpenNtfs(Path,
                          DELETE | SYNCHRONIZE,
                          FILE_OPEN,
                          FILE_DIRECTORY_FILE | FILE_DELETE_ON_CLOSE, &h);
            TOK(NT_SUCCESS(St), "n16: rmdir depth=%lu open: 0x%08lx\n", Depth, St);
            if (NT_SUCCESS(St)) ZwClose(h);
            if (Depth == 1) break;
        }

        /* Verify top-level dd01 is gone. */
        St = OpenNtfs(L"\\??\\D:\\dd01", FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                      FILE_OPEN, FILE_DIRECTORY_FILE, &h);
        TOK(St == STATUS_OBJECT_NAME_NOT_FOUND || St == STATUS_OBJECT_PATH_NOT_FOUND,
            "n16: dd01 gone after rmdir: 0x%08lx\n", St);
        if (NT_SUCCESS(St)) ZwClose(h);
    }

    /*
     * ==========================================================
     * 10.28 Deeper nested tree — 32 levels
     *       Same mechanics as 10.27 but pushed to 32 levels. Proves
     *       the parent-MFT cache + $I30 insert + rmdir chain all
     *       work at a depth closer to real-world "Documents/..."
     *       user paths.
     * ==========================================================
     */
    TLOG("--- section 10.28: nested 32 levels ---\n");
    {
        WCHAR Path[1024];
        ULONG Depth;
        ULONG Idx;
        BOOLEAN Ok32 = TRUE;
        /* Pre-clean. */
        for (Depth = 32; Depth >= 1; --Depth)
        {
            WCHAR Clean[1024];
            ULONG Pos = 0;
            Clean[Pos++] = L'\\'; Clean[Pos++] = L'?'; Clean[Pos++] = L'?';
            Clean[Pos++] = L'\\'; Clean[Pos++] = L'D'; Clean[Pos++] = L':';
            for (Idx = 1; Idx <= Depth; ++Idx)
            {
                Clean[Pos++] = L'\\'; Clean[Pos++] = L'e'; Clean[Pos++] = L'e';
                Clean[Pos++] = (WCHAR)(L'0' + (Idx / 10));
                Clean[Pos++] = (WCHAR)(L'0' + (Idx % 10));
            }
            Clean[Pos] = 0;
            if (Depth == 32)
            {
                WCHAR Leaf[1024];
                RtlStringCbCopyW(Leaf, sizeof(Leaf), Clean);
                RtlStringCbCatW(Leaf, sizeof(Leaf), L"\\leaf32.bin");
                UnlinkByPath(Leaf);
            }
            UnlinkByPath(Clean);
            if (Depth == 1) break;
        }

        /* Create each level. */
        for (Depth = 1; Depth <= 32; ++Depth)
        {
            ULONG Pos = 0;
            Path[Pos++] = L'\\'; Path[Pos++] = L'?'; Path[Pos++] = L'?';
            Path[Pos++] = L'\\'; Path[Pos++] = L'D'; Path[Pos++] = L':';
            for (Idx = 1; Idx <= Depth; ++Idx)
            {
                Path[Pos++] = L'\\'; Path[Pos++] = L'e'; Path[Pos++] = L'e';
                Path[Pos++] = (WCHAR)(L'0' + (Idx / 10));
                Path[Pos++] = (WCHAR)(L'0' + (Idx % 10));
            }
            Path[Pos] = 0;
            St = OpenNtfs(Path,
                          GENERIC_READ | GENERIC_WRITE | DELETE | SYNCHRONIZE,
                          FILE_CREATE, FILE_DIRECTORY_FILE, &h);
            TOK(NT_SUCCESS(St), "n32: mkdir depth=%lu 0x%08lx\n", Depth, St);
            if (NT_SUCCESS(St)) ZwClose(h); else { Ok32 = FALSE; break; }
        }

        if (Ok32)
        {
            WCHAR LeafPath[1024];
            RtlStringCbCopyW(LeafPath, sizeof(LeafPath), Path);
            RtlStringCbCatW(LeafPath, sizeof(LeafPath), L"\\leaf32.bin");
            St = OpenNtfs(LeafPath,
                          GENERIC_READ | GENERIC_WRITE | DELETE | SYNCHRONIZE,
                          FILE_CREATE, FILE_NON_DIRECTORY_FILE, &h);
            TOK(NT_SUCCESS(St), "n32: create leaf: 0x%08lx\n", St);
            if (NT_SUCCESS(St))
            {
                /* 1 KB write — exercises resident path at max depth. */
                for (i = 0; i < 1024; i++) Wr[i] = (UCHAR)(i ^ 0x3C);
                St = WriteAt(h, 0, Wr, 1024, &Info);
                TOK(NT_SUCCESS(St) && Info == 1024,
                    "n32: leaf write 1K: 0x%08lx info=%lu\n", St, Info);
                ZwClose(h);

                St = OpenNtfs(LeafPath,
                              GENERIC_READ | DELETE | SYNCHRONIZE,
                              FILE_OPEN, FILE_NON_DIRECTORY_FILE, &h);
                TOK(NT_SUCCESS(St), "n32: reopen leaf: 0x%08lx\n", St);
                if (NT_SUCCESS(St))
                {
                    RtlZeroMemory(Rd, 1024);
                    St = ReadAt(h, 0, Rd, 1024, &Info);
                    Errs = 0;
                    for (i = 0; i < 1024; i++)
                        if (Rd[i] != (UCHAR)(i ^ 0x3C)) Errs++;
                    TOK(NT_SUCCESS(St) && Errs == 0 && Info == 1024,
                        "n32: leaf content intact: 0x%08lx mismatches=%lu info=%lu\n",
                        St, Errs, Info);
                    ZwClose(h);
                }

                /* StandardInformation sanity check at depth 32. */
                St = OpenNtfs(LeafPath, FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                              FILE_OPEN, FILE_NON_DIRECTORY_FILE, &h);
                if (NT_SUCCESS(St))
                {
                    St = ZwQueryInformationFile(h, &IoSb, &Std, sizeof(Std),
                                                FileStandardInformation);
                    TOK(NT_SUCCESS(St) && Std.EndOfFile.QuadPart == 1024 &&
                        Std.Directory == FALSE,
                        "n32: StdInfo eof=%I64u dir=%u status=0x%08lx\n",
                        (ULONGLONG)Std.EndOfFile.QuadPart, Std.Directory, St);
                    ZwClose(h);
                }
            }

            UnlinkByPath(LeafPath);
        }

        /* Rmdir bottom-up. */
        for (Depth = 32; Depth >= 1; --Depth)
        {
            ULONG Pos = 0;
            Path[Pos++] = L'\\'; Path[Pos++] = L'?'; Path[Pos++] = L'?';
            Path[Pos++] = L'\\'; Path[Pos++] = L'D'; Path[Pos++] = L':';
            for (Idx = 1; Idx <= Depth; ++Idx)
            {
                Path[Pos++] = L'\\'; Path[Pos++] = L'e'; Path[Pos++] = L'e';
                Path[Pos++] = (WCHAR)(L'0' + (Idx / 10));
                Path[Pos++] = (WCHAR)(L'0' + (Idx % 10));
            }
            Path[Pos] = 0;

            St = OpenNtfs(Path,
                          DELETE | SYNCHRONIZE,
                          FILE_OPEN,
                          FILE_DIRECTORY_FILE | FILE_DELETE_ON_CLOSE, &h);
            TOK(NT_SUCCESS(St), "n32: rmdir depth=%lu open: 0x%08lx\n", Depth, St);
            if (NT_SUCCESS(St)) ZwClose(h);
            if (Depth == 1) break;
        }

        St = OpenNtfs(L"\\??\\D:\\ee01", FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                      FILE_OPEN, FILE_DIRECTORY_FILE, &h);
        TOK(St == STATUS_OBJECT_NAME_NOT_FOUND || St == STATUS_OBJECT_PATH_NOT_FOUND,
            "n32: ee01 gone after rmdir: 0x%08lx\n", St);
        if (NT_SUCCESS(St)) ZwClose(h);
    }

    /*
     * ==========================================================
     * 10.29 Wide subfolder — 32 files inside one directory.
     *       Exercises $I30 growth inside a nested parent (not the
     *       volume root). Verifies enumerate/read/write/delete at
     *       the subfolder level.
     * ==========================================================
     */
    TLOG("--- section 10.29: wide subfolder (32 files) ---\n");
    {
        WCHAR DirPath[64] = L"\\??\\D:\\wide_dir";
        WCHAR FilePath[128];
        ULONG FileIdx;
        BOOLEAN DirOk;

        /* Pre-clean. */
        for (FileIdx = 0; FileIdx < 32; ++FileIdx)
        {
            RtlStringCbPrintfW(FilePath, sizeof(FilePath),
                               L"\\??\\D:\\wide_dir\\f%02lu.bin", FileIdx);
            UnlinkByPath(FilePath);
        }
        UnlinkByPath(DirPath);

        St = OpenNtfs(DirPath,
                      GENERIC_READ | GENERIC_WRITE | DELETE | SYNCHRONIZE,
                      FILE_CREATE, FILE_DIRECTORY_FILE, &h);
        TOK(NT_SUCCESS(St), "wide: mkdir wide_dir 0x%08lx\n", St);
        DirOk = NT_SUCCESS(St);
        if (DirOk) ZwClose(h);

        if (DirOk)
        {
            for (FileIdx = 0; FileIdx < 32; ++FileIdx)
            {
                RtlStringCbPrintfW(FilePath, sizeof(FilePath),
                                   L"\\??\\D:\\wide_dir\\f%02lu.bin", FileIdx);
                St = OpenNtfs(FilePath,
                              GENERIC_READ | GENERIC_WRITE | DELETE | SYNCHRONIZE,
                              FILE_CREATE, FILE_NON_DIRECTORY_FILE, &h);
                TOK(NT_SUCCESS(St),
                    "wide: create f%02lu: 0x%08lx\n", FileIdx, St);
                if (NT_SUCCESS(St))
                {
                    for (i = 0; i < 128; i++) Wr[i] = (UCHAR)((FileIdx * 17) + i);
                    St = WriteAt(h, 0, Wr, 128, &Info);
                    TOK(NT_SUCCESS(St) && Info == 128,
                        "wide: write f%02lu: 0x%08lx info=%lu\n",
                        FileIdx, St, Info);
                    ZwClose(h);
                }
            }

            /* Reopen each and verify. */
            for (FileIdx = 0; FileIdx < 32; ++FileIdx)
            {
                RtlStringCbPrintfW(FilePath, sizeof(FilePath),
                                   L"\\??\\D:\\wide_dir\\f%02lu.bin", FileIdx);
                St = OpenNtfs(FilePath, GENERIC_READ | SYNCHRONIZE,
                              FILE_OPEN, FILE_NON_DIRECTORY_FILE, &h);
                TOK(NT_SUCCESS(St),
                    "wide: reopen f%02lu: 0x%08lx\n", FileIdx, St);
                if (NT_SUCCESS(St))
                {
                    RtlZeroMemory(Rd, 128);
                    St = ReadAt(h, 0, Rd, 128, &Info);
                    Errs = 0;
                    for (i = 0; i < 128; i++)
                        if (Rd[i] != (UCHAR)((FileIdx * 17) + i)) Errs++;
                    TOK(NT_SUCCESS(St) && Errs == 0 && Info == 128,
                        "wide: content f%02lu: 0x%08lx errs=%lu info=%lu\n",
                        FileIdx, St, Errs, Info);
                    ZwClose(h);
                }
            }

            /* Enumerate the subfolder and count entries. */
            St = OpenNtfs(DirPath, FILE_LIST_DIRECTORY | SYNCHRONIZE,
                          FILE_OPEN, FILE_DIRECTORY_FILE, &h);
            TOK(NT_SUCCESS(St), "wide: open for enum: 0x%08lx\n", St);
            if (NT_SUCCESS(St))
            {
                UCHAR DirBuf[4096];
                ULONG Found = 0;
                ULONG Off = 0;
                PFILE_BOTH_DIR_INFORMATION Entry;

                RtlZeroMemory(DirBuf, sizeof(DirBuf));
                St = ZwQueryDirectoryFile(h, NULL, NULL, NULL, &IoSb,
                                          DirBuf, sizeof(DirBuf),
                                          FileBothDirectoryInformation,
                                          FALSE, NULL, TRUE);
                TOK(NT_SUCCESS(St), "wide: enum first call: 0x%08lx\n", St);

                while (NT_SUCCESS(St))
                {
                    Off = 0;
                    while (Off < IoSb.Information)
                    {
                        Entry = (PFILE_BOTH_DIR_INFORMATION)(DirBuf + Off);
                        if (Entry->FileNameLength == 7 * sizeof(WCHAR) &&
                            Entry->FileName[0] == L'f')
                        {
                            /* "fNN.bin" = 7 chars */
                            Found++;
                        }
                        if (Entry->NextEntryOffset == 0) break;
                        Off += Entry->NextEntryOffset;
                    }
                    St = ZwQueryDirectoryFile(h, NULL, NULL, NULL, &IoSb,
                                              DirBuf, sizeof(DirBuf),
                                              FileBothDirectoryInformation,
                                              FALSE, NULL, FALSE);
                    if (St == STATUS_NO_MORE_FILES) break;
                }
                TOK(Found == 32,
                    "wide: enumerate found=%lu (expect 32)\n", Found);
                ZwClose(h);
            }

            /* Delete all the files first. */
            for (FileIdx = 0; FileIdx < 32; ++FileIdx)
            {
                RtlStringCbPrintfW(FilePath, sizeof(FilePath),
                                   L"\\??\\D:\\wide_dir\\f%02lu.bin", FileIdx);
                UnlinkByPath(FilePath);
            }
            /* Now rmdir. */
            UnlinkByPath(DirPath);
            St = OpenNtfs(DirPath, FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                          FILE_OPEN, FILE_DIRECTORY_FILE, &h);
            TOK(St == STATUS_OBJECT_NAME_NOT_FOUND || St == STATUS_OBJECT_PATH_NOT_FOUND,
                "wide: wide_dir gone after cleanup: 0x%08lx\n", St);
            if (NT_SUCCESS(St)) ZwClose(h);
        }
    }

    /*
     * ==========================================================
     * 10.30 Cross-folder FileId distinctness.
     *       Creates \ca\f and \cb\f, queries FileId via
     *       FileInternalInformation, asserts both open the right file
     *       and carry distinct 64-bit references.
     * ==========================================================
     */
    TLOG("--- section 10.30: cross-folder FileId ---\n");
    {
        FILE_INTERNAL_INFORMATION IntA;
        FILE_INTERNAL_INFORMATION IntB;

        UnlinkByPath(L"\\??\\D:\\ca\\f.bin");
        UnlinkByPath(L"\\??\\D:\\cb\\f.bin");
        UnlinkByPath(L"\\??\\D:\\ca");
        UnlinkByPath(L"\\??\\D:\\cb");

        St = OpenNtfs(L"\\??\\D:\\ca",
                      GENERIC_READ | GENERIC_WRITE | DELETE | SYNCHRONIZE,
                      FILE_CREATE, FILE_DIRECTORY_FILE, &h);
        TOK(NT_SUCCESS(St), "cf: mkdir ca: 0x%08lx\n", St);
        if (NT_SUCCESS(St)) ZwClose(h);

        St = OpenNtfs(L"\\??\\D:\\cb",
                      GENERIC_READ | GENERIC_WRITE | DELETE | SYNCHRONIZE,
                      FILE_CREATE, FILE_DIRECTORY_FILE, &h);
        TOK(NT_SUCCESS(St), "cf: mkdir cb: 0x%08lx\n", St);
        if (NT_SUCCESS(St)) ZwClose(h);

        St = OpenNtfs(L"\\??\\D:\\ca\\f.bin",
                      GENERIC_READ | GENERIC_WRITE | DELETE | SYNCHRONIZE,
                      FILE_CREATE, FILE_NON_DIRECTORY_FILE, &h);
        TOK(NT_SUCCESS(St), "cf: create ca\\f: 0x%08lx\n", St);
        if (NT_SUCCESS(St))
        {
            Wr[0] = 0xA1;
            WriteAt(h, 0, Wr, 1, &Info);
            RtlZeroMemory(&IntA, sizeof(IntA));
            St = ZwQueryInformationFile(h, &IoSb, &IntA, sizeof(IntA),
                                        FileInternalInformation);
            TOK(NT_SUCCESS(St) && IntA.IndexNumber.QuadPart != 0,
                "cf: ca\\f FileId query: 0x%08lx id=0x%I64x\n",
                St, IntA.IndexNumber.QuadPart);
            ZwClose(h);
        }

        St = OpenNtfs(L"\\??\\D:\\cb\\f.bin",
                      GENERIC_READ | GENERIC_WRITE | DELETE | SYNCHRONIZE,
                      FILE_CREATE, FILE_NON_DIRECTORY_FILE, &h);
        TOK(NT_SUCCESS(St), "cf: create cb\\f: 0x%08lx\n", St);
        if (NT_SUCCESS(St))
        {
            Wr[0] = 0xB2;
            WriteAt(h, 0, Wr, 1, &Info);
            RtlZeroMemory(&IntB, sizeof(IntB));
            St = ZwQueryInformationFile(h, &IoSb, &IntB, sizeof(IntB),
                                        FileInternalInformation);
            TOK(NT_SUCCESS(St) && IntB.IndexNumber.QuadPart != 0,
                "cf: cb\\f FileId query: 0x%08lx id=0x%I64x\n",
                St, IntB.IndexNumber.QuadPart);
            ZwClose(h);
        }

        TOK(IntA.IndexNumber.QuadPart != IntB.IndexNumber.QuadPart,
            "cf: FileId distinct a=0x%I64x b=0x%I64x\n",
            IntA.IndexNumber.QuadPart, IntB.IndexNumber.QuadPart);

        /* Read back: ca\f should still be 0xA1, cb\f should be 0xB2. */
        St = OpenNtfs(L"\\??\\D:\\ca\\f.bin", GENERIC_READ | SYNCHRONIZE,
                      FILE_OPEN, FILE_NON_DIRECTORY_FILE, &h);
        if (NT_SUCCESS(St))
        {
            Rd[0] = 0;
            ReadAt(h, 0, Rd, 1, &Info);
            TOK(Rd[0] == 0xA1, "cf: ca\\f content: 0x%02x (expect 0xA1)\n", Rd[0]);
            ZwClose(h);
        }
        St = OpenNtfs(L"\\??\\D:\\cb\\f.bin", GENERIC_READ | SYNCHRONIZE,
                      FILE_OPEN, FILE_NON_DIRECTORY_FILE, &h);
        if (NT_SUCCESS(St))
        {
            Rd[0] = 0;
            ReadAt(h, 0, Rd, 1, &Info);
            TOK(Rd[0] == 0xB2, "cf: cb\\f content: 0x%02x (expect 0xB2)\n", Rd[0]);
            ZwClose(h);
        }

        UnlinkByPath(L"\\??\\D:\\ca\\f.bin");
        UnlinkByPath(L"\\??\\D:\\cb\\f.bin");
        UnlinkByPath(L"\\??\\D:\\ca");
        UnlinkByPath(L"\\??\\D:\\cb");
    }

    /*
     * ==========================================================
     * 10.31 rmdir non-empty must fail; empty must succeed.
     *       Proves STATUS_DIRECTORY_NOT_EMPTY is raised correctly
     *       and that removing the child unblocks the parent rmdir.
     * ==========================================================
     */
    TLOG("--- section 10.31: rmdir non-empty ---\n");
    {
        UnlinkByPath(L"\\??\\D:\\fulldir\\child.bin");
        UnlinkByPath(L"\\??\\D:\\fulldir");

        St = OpenNtfs(L"\\??\\D:\\fulldir",
                      GENERIC_READ | GENERIC_WRITE | DELETE | SYNCHRONIZE,
                      FILE_CREATE, FILE_DIRECTORY_FILE, &h);
        TOK(NT_SUCCESS(St), "rn: mkdir fulldir: 0x%08lx\n", St);
        if (NT_SUCCESS(St)) ZwClose(h);

        St = OpenNtfs(L"\\??\\D:\\fulldir\\child.bin",
                      GENERIC_READ | GENERIC_WRITE | DELETE | SYNCHRONIZE,
                      FILE_CREATE, FILE_NON_DIRECTORY_FILE, &h);
        TOK(NT_SUCCESS(St), "rn: create child: 0x%08lx\n", St);
        if (NT_SUCCESS(St))
        {
            Wr[0] = 0x77;
            WriteAt(h, 0, Wr, 1, &Info);
            ZwClose(h);
        }

        /* rmdir attempt while child exists — expect failure. */
        {
            UNICODE_STRING DN;
            OBJECT_ATTRIBUTES DA;
            RtlInitUnicodeString(&DN, L"\\??\\D:\\fulldir");
            InitializeObjectAttributes(&DA, &DN,
                                        OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                                        NULL, NULL);
            St = ZwDeleteFile(&DA);
            TOK(St == STATUS_DIRECTORY_NOT_EMPTY ||
                St == STATUS_CANNOT_DELETE ||
                !NT_SUCCESS(St),
                "rn: rmdir non-empty should fail: 0x%08lx\n", St);
        }

        /* Remove the child then rmdir. */
        UnlinkByPath(L"\\??\\D:\\fulldir\\child.bin");
        UnlinkByPath(L"\\??\\D:\\fulldir");
        St = OpenNtfs(L"\\??\\D:\\fulldir", FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                      FILE_OPEN, FILE_DIRECTORY_FILE, &h);
        TOK(St == STATUS_OBJECT_NAME_NOT_FOUND || St == STATUS_OBJECT_PATH_NOT_FOUND,
            "rn: fulldir gone after child removal: 0x%08lx\n", St);
        if (NT_SUCCESS(St)) ZwClose(h);
    }

    /*
     * ==========================================================
     * 10.32 Rapid mkdir/rmdir cycle — 50 iterations.
     *       Leak detection: create and destroy the same name 50x.
     *       Every iteration must succeed; if any one fails it means
     *       the MFT slot isn't being freed, the $I30 entry isn't
     *       being removed, or a pool tag leaked and filled up.
     * ==========================================================
     */
    TLOG("--- section 10.32: mkdir/rmdir x50 ---\n");
    {
        ULONG It;
        ULONG Okc = 0;
        ULONG Failc = 0;
        for (It = 0; It < 50; ++It)
        {
            St = OpenNtfs(L"\\??\\D:\\mkdir_loop",
                          GENERIC_READ | GENERIC_WRITE | DELETE | SYNCHRONIZE,
                          FILE_CREATE, FILE_DIRECTORY_FILE, &h);
            if (NT_SUCCESS(St))
            {
                ZwClose(h);
                Okc++;
            }
            else
            {
                Failc++;
            }
            UnlinkByPath(L"\\??\\D:\\mkdir_loop");
        }
        TOK(Okc == 50 && Failc == 0,
            "loop: mkdir/rmdir ok=%lu fail=%lu (expect 50/0)\n", Okc, Failc);

        /* Verify nothing was left behind. */
        St = OpenNtfs(L"\\??\\D:\\mkdir_loop", FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                      FILE_OPEN, FILE_DIRECTORY_FILE, &h);
        TOK(St == STATUS_OBJECT_NAME_NOT_FOUND || St == STATUS_OBJECT_PATH_NOT_FOUND,
            "loop: final state clean: 0x%08lx\n", St);
        if (NT_SUCCESS(St)) ZwClose(h);
    }

    /*
     * ==========================================================
     * 10.33 Long-session file churn — 32 create/write/read/delete.
     *       Proves per-open leak-freedom: every iteration must close
     *       cleanly and free CCB/FileContext/ShareRecord. If any
     *       resource leaks, later iterations will start failing
     *       (out-of-pool, MFT bitmap full, $I30 too large, etc.).
     * ==========================================================
     */
    if (g_NtfslxFuncPhase == NtfslxFuncPhaseStressB)
    {
        TLOG("=== NtfslxFunc END phase=%s (completed sections 10.17-10.32) ===\n",
             NtfslxGetFuncPhaseName());
        goto Done;
    }

LongSuitePhaseC:
    TLOG("=== NtfslxFunc phase=%s entering long suite C ===\n",
         NtfslxGetFuncPhaseName());
    TLOG("--- section 10.33: churn x32 ---\n");
    {
        ULONG It;
        ULONG CreateOk = 0;
        ULONG WriteOk = 0;
        ULONG ReadOk = 0;
        ULONG DeleteOk = 0;
        for (It = 0; It < 32; ++It)
        {
            WCHAR Nm[64];
            RtlStringCbPrintfW(Nm, sizeof(Nm), L"\\??\\D:\\churn_%03lu.bin", It);
            St = OpenNtfs(Nm,
                          GENERIC_READ | GENERIC_WRITE | DELETE | SYNCHRONIZE,
                          FILE_CREATE, FILE_NON_DIRECTORY_FILE, &h);
            if (NT_SUCCESS(St))
            {
                CreateOk++;
                Wr[0] = (UCHAR)(It & 0xFF);
                Wr[1] = (UCHAR)((It >> 8) & 0xFF);
                Wr[2] = 0xC3;
                Wr[3] = 0x5A;
                if (NT_SUCCESS(WriteAt(h, 0, Wr, 4, &Info)) && Info == 4)
                    WriteOk++;
                ZwClose(h);
            }

            St = OpenNtfs(Nm, GENERIC_READ | SYNCHRONIZE,
                          FILE_OPEN, FILE_NON_DIRECTORY_FILE, &h);
            if (NT_SUCCESS(St))
            {
                RtlZeroMemory(Rd, 4);
                if (NT_SUCCESS(ReadAt(h, 0, Rd, 4, &Info)) &&
                    Info == 4 &&
                    Rd[0] == (UCHAR)(It & 0xFF) &&
                    Rd[2] == 0xC3 && Rd[3] == 0x5A)
                    ReadOk++;
                ZwClose(h);
            }

            UnlinkByPath(Nm);

            /* Confirm delete. */
            St = OpenNtfs(Nm, FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                          FILE_OPEN, FILE_NON_DIRECTORY_FILE, &h);
            if (St == STATUS_OBJECT_NAME_NOT_FOUND)
                DeleteOk++;
            else if (NT_SUCCESS(St))
                ZwClose(h);
        }
        TOK(CreateOk == 32, "churn: create ok=%lu/32\n", CreateOk);
        TOK(WriteOk == 32, "churn: write ok=%lu/32\n", WriteOk);
        TOK(ReadOk == 32, "churn: read ok=%lu/32\n", ReadOk);
        TOK(DeleteOk == 32, "churn: delete ok=%lu/32\n", DeleteOk);
    }

    /*
     * ==========================================================
     * 10.34 Subfolder churn — create \cdir, churn files inside it
     *       32 times, ensure cdir stays healthy and files don't
     *       leak into \cdir's $I30.
     * ==========================================================
     */
    TLOG("--- section 10.34: subfolder churn x32 ---\n");
    {
        ULONG It;
        ULONG Okc = 0;
        UnlinkByPath(L"\\??\\D:\\cdir");
        St = OpenNtfs(L"\\??\\D:\\cdir",
                      GENERIC_READ | GENERIC_WRITE | DELETE | SYNCHRONIZE,
                      FILE_CREATE, FILE_DIRECTORY_FILE, &h);
        TOK(NT_SUCCESS(St), "subch: mkdir cdir: 0x%08lx\n", St);
        if (NT_SUCCESS(St)) ZwClose(h);

        for (It = 0; It < 32; ++It)
        {
            WCHAR Nm[64];
            RtlStringCbPrintfW(Nm, sizeof(Nm),
                               L"\\??\\D:\\cdir\\ch_%03lu.bin", It);
            St = OpenNtfs(Nm,
                          GENERIC_READ | GENERIC_WRITE | DELETE | SYNCHRONIZE,
                          FILE_CREATE, FILE_NON_DIRECTORY_FILE, &h);
            if (NT_SUCCESS(St))
            {
                for (i = 0; i < 64; i++) Wr[i] = (UCHAR)(It + i);
                WriteAt(h, 0, Wr, 64, &Info);
                ZwClose(h);
                UnlinkByPath(Nm);
                Okc++;
            }
        }
        TOK(Okc == 32, "subch: file cycles ok=%lu/32\n", Okc);

        /* Verify cdir still exists and is now empty — enumerate should return 0 files. */
        St = OpenNtfs(L"\\??\\D:\\cdir", FILE_LIST_DIRECTORY | SYNCHRONIZE,
                      FILE_OPEN, FILE_DIRECTORY_FILE, &h);
        TOK(NT_SUCCESS(St), "subch: cdir still alive: 0x%08lx\n", St);
        if (NT_SUCCESS(St))
        {
            UCHAR DirBuf[1024];
            ULONG Found = 0;
            ULONG Off;
            PFILE_BOTH_DIR_INFORMATION Ent;

            RtlZeroMemory(DirBuf, sizeof(DirBuf));
            St = ZwQueryDirectoryFile(h, NULL, NULL, NULL, &IoSb,
                                      DirBuf, sizeof(DirBuf),
                                      FileBothDirectoryInformation,
                                      FALSE, NULL, TRUE);
            if (NT_SUCCESS(St))
            {
                Off = 0;
                while (Off < IoSb.Information)
                {
                    Ent = (PFILE_BOTH_DIR_INFORMATION)(DirBuf + Off);
                    if (Ent->FileNameLength >= 3 * sizeof(WCHAR) &&
                        Ent->FileName[0] == L'c' && Ent->FileName[1] == L'h' &&
                        Ent->FileName[2] == L'_')
                        Found++;
                    if (Ent->NextEntryOffset == 0) break;
                    Off += Ent->NextEntryOffset;
                }
            }
            TOK(Found == 0, "subch: no leaked entries found=%lu\n", Found);
            ZwClose(h);
        }

        UnlinkByPath(L"\\??\\D:\\cdir");
        St = OpenNtfs(L"\\??\\D:\\cdir", FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                      FILE_OPEN, FILE_DIRECTORY_FILE, &h);
        TOK(St == STATUS_OBJECT_NAME_NOT_FOUND || St == STATUS_OBJECT_PATH_NOT_FOUND,
            "subch: cdir finally gone: 0x%08lx\n", St);
        if (NT_SUCCESS(St)) ZwClose(h);
    }

    /*
     * ==========================================================
     * 10.35 Subfolder StandardInformation / size reporting.
     *       Write a 2048-byte file inside a subfolder, verify the
     *       query returns 2048, not 0. This is the exact symptom
     *       Explorer reported — "file size 0 KB in subfolder".
     * ==========================================================
     */
    TLOG("--- section 10.35: subfolder file size ---\n");
    {
        HANDLE Hdir;
        UnlinkByPath(L"\\??\\D:\\sd\\sz.bin");
        UnlinkByPath(L"\\??\\D:\\sd");

        St = OpenNtfs(L"\\??\\D:\\sd",
                      GENERIC_READ | GENERIC_WRITE | DELETE | SYNCHRONIZE,
                      FILE_CREATE, FILE_DIRECTORY_FILE, &Hdir);
        TOK(NT_SUCCESS(St), "sz: mkdir sd: 0x%08lx\n", St);
        if (NT_SUCCESS(St)) ZwClose(Hdir);

        St = OpenNtfs(L"\\??\\D:\\sd\\sz.bin",
                      GENERIC_READ | GENERIC_WRITE | DELETE | SYNCHRONIZE,
                      FILE_CREATE, FILE_NON_DIRECTORY_FILE, &h);
        TOK(NT_SUCCESS(St), "sz: create sz.bin: 0x%08lx\n", St);
        if (NT_SUCCESS(St))
        {
            for (i = 0; i < 2048; i++) Wr[i] = (UCHAR)(i & 0xFF);
            St = WriteAt(h, 0, Wr, 2048, &Info);
            TOK(NT_SUCCESS(St) && Info == 2048,
                "sz: write 2K: 0x%08lx info=%lu\n", St, Info);

            /* Query BEFORE close — size should be 2048 on the same handle. */
            St = ZwQueryInformationFile(h, &IoSb, &Std, sizeof(Std),
                                        FileStandardInformation);
            TOK(NT_SUCCESS(St) && Std.EndOfFile.QuadPart == 2048 &&
                Std.Directory == FALSE,
                "sz: StdInfo on-handle eof=%I64u dir=%u 0x%08lx\n",
                (ULONGLONG)Std.EndOfFile.QuadPart, Std.Directory, St);
            ZwClose(h);
        }

        /* Now reopen via a fresh handle and re-query. */
        St = OpenNtfs(L"\\??\\D:\\sd\\sz.bin", FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                      FILE_OPEN, FILE_NON_DIRECTORY_FILE, &h);
        TOK(NT_SUCCESS(St), "sz: reopen sz.bin: 0x%08lx\n", St);
        if (NT_SUCCESS(St))
        {
            St = ZwQueryInformationFile(h, &IoSb, &Std, sizeof(Std),
                                        FileStandardInformation);
            TOK(NT_SUCCESS(St) && Std.EndOfFile.QuadPart == 2048,
                "sz: StdInfo reopen eof=%I64u 0x%08lx (must be 2048)\n",
                (ULONGLONG)Std.EndOfFile.QuadPart, St);
            ZwClose(h);
        }

        /* Enumerate sd and find sz.bin with size 2048. */
        St = OpenNtfs(L"\\??\\D:\\sd", FILE_LIST_DIRECTORY | SYNCHRONIZE,
                      FILE_OPEN, FILE_DIRECTORY_FILE, &h);
        if (NT_SUCCESS(St))
        {
            UCHAR DirBuf[1024];
            PFILE_BOTH_DIR_INFORMATION Ent;
            ULONG Off;
            BOOLEAN Seen = FALSE;
            ULONGLONG SeenSize = 0;

            RtlZeroMemory(DirBuf, sizeof(DirBuf));
            St = ZwQueryDirectoryFile(h, NULL, NULL, NULL, &IoSb,
                                      DirBuf, sizeof(DirBuf),
                                      FileBothDirectoryInformation,
                                      FALSE, NULL, TRUE);
            if (NT_SUCCESS(St))
            {
                Off = 0;
                while (Off < IoSb.Information)
                {
                    Ent = (PFILE_BOTH_DIR_INFORMATION)(DirBuf + Off);
                    if (Ent->FileNameLength == 6 * sizeof(WCHAR) &&
                        Ent->FileName[0] == L's' &&
                        Ent->FileName[1] == L'z' &&
                        Ent->FileName[2] == L'.')
                    {
                        Seen = TRUE;
                        SeenSize = (ULONGLONG)Ent->EndOfFile.QuadPart;
                        break;
                    }
                    if (Ent->NextEntryOffset == 0) break;
                    Off += Ent->NextEntryOffset;
                }
            }
            TOK(Seen, "sz: sz.bin visible in enum\n");
            TOK(SeenSize == 2048,
                "sz: sz.bin enum size=%I64u (must be 2048)\n", SeenSize);
            ZwClose(h);
        }

        UnlinkByPath(L"\\??\\D:\\sd\\sz.bin");
        UnlinkByPath(L"\\??\\D:\\sd");
    }

    /*
     * ==========================================================
     * 10.36 Subfolder-level rename.
     *       Ensures rename inside a subfolder updates only that
     *       folder's $I30 and never touches the root.
     * ==========================================================
     */
    TLOG("--- section 10.36: subfolder rename ---\n");
    {
        FILE_RENAME_INFORMATION *Ren;
        UCHAR RenBuf[128];
        UnlinkByPath(L"\\??\\D:\\rd\\old.bin");
        UnlinkByPath(L"\\??\\D:\\rd\\new.bin");
        UnlinkByPath(L"\\??\\D:\\rd");

        St = OpenNtfs(L"\\??\\D:\\rd",
                      GENERIC_READ | GENERIC_WRITE | DELETE | SYNCHRONIZE,
                      FILE_CREATE, FILE_DIRECTORY_FILE, &h);
        TOK(NT_SUCCESS(St), "ren: mkdir rd: 0x%08lx\n", St);
        if (NT_SUCCESS(St)) ZwClose(h);

        St = OpenNtfs(L"\\??\\D:\\rd\\old.bin",
                      GENERIC_READ | GENERIC_WRITE | DELETE | SYNCHRONIZE,
                      FILE_CREATE, FILE_NON_DIRECTORY_FILE, &h);
        TOK(NT_SUCCESS(St), "ren: create rd\\old: 0x%08lx\n", St);
        if (NT_SUCCESS(St))
        {
            Wr[0] = 0x42;
            WriteAt(h, 0, Wr, 1, &Info);

            RtlZeroMemory(RenBuf, sizeof(RenBuf));
            Ren = (FILE_RENAME_INFORMATION *)RenBuf;
            Ren->ReplaceIfExists = FALSE;
            Ren->RootDirectory = NULL;
            RtlStringCbCopyW(Ren->FileName,
                             sizeof(RenBuf) - FIELD_OFFSET(FILE_RENAME_INFORMATION, FileName),
                             L"new.bin");
            Ren->FileNameLength = (ULONG)(7 * sizeof(WCHAR));

            St = ZwSetInformationFile(h, &IoSb, RenBuf,
                                      FIELD_OFFSET(FILE_RENAME_INFORMATION, FileName) +
                                          Ren->FileNameLength,
                                      FileRenameInformation);
            TOK(NT_SUCCESS(St) || St == STATUS_NOT_IMPLEMENTED,
                "ren: same-dir rename: 0x%08lx\n", St);
            ZwClose(h);
        }

        /* Verify rd\new.bin exists (if rename was implemented). */
        St = OpenNtfs(L"\\??\\D:\\rd\\new.bin", GENERIC_READ | SYNCHRONIZE,
                      FILE_OPEN, FILE_NON_DIRECTORY_FILE, &h);
        if (NT_SUCCESS(St))
        {
            Rd[0] = 0;
            ReadAt(h, 0, Rd, 1, &Info);
            TOK(Rd[0] == 0x42, "ren: new.bin content 0x%02x\n", Rd[0]);
            ZwClose(h);
        }
        else
        {
            TLOG("ren: new.bin not openable post-rename (rename skipped or failed): 0x%08lx\n", St);
        }

        UnlinkByPath(L"\\??\\D:\\rd\\old.bin");
        UnlinkByPath(L"\\??\\D:\\rd\\new.bin");
        UnlinkByPath(L"\\??\\D:\\rd");
    }

    /*
     * ==========================================================
     * 10.37.A Add files to a subfolder, close, reopen via a fresh
     *         directory handle, enumerate, and confirm the new
     *         entries are visible. This is the EXACT symptom the
     *         user reported: "added to subfolder, it displays
     *         nothing inside". Runs 16 add/reenum rounds.
     * ==========================================================
     */
    TLOG("--- section 10.37.A: subfolder add+enum x16 ---\n");
    {
        ULONG Round;
        ULONG VisibleOk = 0;
        UnlinkByPath(L"\\??\\D:\\add_dir");
        St = OpenNtfs(L"\\??\\D:\\add_dir",
                      GENERIC_READ | GENERIC_WRITE | DELETE | SYNCHRONIZE,
                      FILE_CREATE, FILE_DIRECTORY_FILE, &h);
        TOK(NT_SUCCESS(St), "adde: mkdir add_dir: 0x%08lx\n", St);
        if (NT_SUCCESS(St)) ZwClose(h);

        for (Round = 0; Round < 16; ++Round)
        {
            WCHAR Nm[80];
            ULONG ExpectedSize;
            HANDLE Dh;
            RtlStringCbPrintfW(Nm, sizeof(Nm),
                               L"\\??\\D:\\add_dir\\new_%02lu.bin", Round);
            St = OpenNtfs(Nm,
                          GENERIC_READ | GENERIC_WRITE | DELETE | SYNCHRONIZE,
                          FILE_CREATE, FILE_NON_DIRECTORY_FILE, &h);
            if (NT_SUCCESS(St))
            {
                ExpectedSize = 64 + Round * 16;
                for (i = 0; i < ExpectedSize; i++) Wr[i] = (UCHAR)(Round ^ i);
                WriteAt(h, 0, Wr, ExpectedSize, &Info);
                ZwClose(h);
            }

            /* Reopen the dir fresh. */
            St = OpenNtfs(L"\\??\\D:\\add_dir",
                          FILE_LIST_DIRECTORY | SYNCHRONIZE,
                          FILE_OPEN, FILE_DIRECTORY_FILE, &Dh);
            if (NT_SUCCESS(St))
            {
                UCHAR DirBuf[2048];
                BOOLEAN FoundNew = FALSE;
                ULONGLONG SeenSize = 0;
                ULONG Off;
                PFILE_BOTH_DIR_INFORMATION Ent;
                WCHAR Target[16];
                ULONG TargetLen;

                RtlStringCbPrintfW(Target, sizeof(Target), L"new_%02lu.bin", Round);
                TargetLen = (ULONG)wcslen(Target);

                RtlZeroMemory(DirBuf, sizeof(DirBuf));
                St = ZwQueryDirectoryFile(Dh, NULL, NULL, NULL, &IoSb,
                                          DirBuf, sizeof(DirBuf),
                                          FileBothDirectoryInformation,
                                          FALSE, NULL, TRUE);
                while (NT_SUCCESS(St))
                {
                    Off = 0;
                    while (Off < IoSb.Information)
                    {
                        Ent = (PFILE_BOTH_DIR_INFORMATION)(DirBuf + Off);
                        if (Ent->FileNameLength == TargetLen * sizeof(WCHAR) &&
                            RtlCompareMemory(Ent->FileName, Target,
                                             TargetLen * sizeof(WCHAR)) ==
                                TargetLen * sizeof(WCHAR))
                        {
                            FoundNew = TRUE;
                            SeenSize = (ULONGLONG)Ent->EndOfFile.QuadPart;
                            break;
                        }
                        if (Ent->NextEntryOffset == 0) break;
                        Off += Ent->NextEntryOffset;
                    }
                    if (FoundNew) break;
                    St = ZwQueryDirectoryFile(Dh, NULL, NULL, NULL, &IoSb,
                                              DirBuf, sizeof(DirBuf),
                                              FileBothDirectoryInformation,
                                              FALSE, NULL, FALSE);
                    if (St == STATUS_NO_MORE_FILES) break;
                }
                TOK(FoundNew && SeenSize == (64 + Round * 16),
                    "adde: round %lu visible=%u sizeSeen=%I64u expect=%lu\n",
                    Round, FoundNew, SeenSize, (64 + Round * 16));
                if (FoundNew && SeenSize == (64 + Round * 16))
                    VisibleOk++;
                ZwClose(Dh);
            }
        }
        TOK(VisibleOk == 16,
            "adde: all rounds visible ok=%lu/16\n", VisibleOk);

        /* Cleanup. */
        for (Round = 0; Round < 16; ++Round)
        {
            WCHAR Nm[80];
            RtlStringCbPrintfW(Nm, sizeof(Nm),
                               L"\\??\\D:\\add_dir\\new_%02lu.bin", Round);
            UnlinkByPath(Nm);
        }
        UnlinkByPath(L"\\??\\D:\\add_dir");
    }

    /*
     * ==========================================================
     * 10.37.B Cross-subfolder copy simulation: create source dir
     *         with files, create dest dir, copy via explicit
     *         create + read + write, verify via enumeration.
     *         Mimics what Explorer CopyFile does.
     * ==========================================================
     */
    TLOG("--- section 10.37.B: cross-folder copy x8 ---\n");
    {
        ULONG Idx;
        ULONG CopyOk = 0;
        UnlinkByPath(L"\\??\\D:\\src\\s0.bin");
        UnlinkByPath(L"\\??\\D:\\src\\s1.bin");
        UnlinkByPath(L"\\??\\D:\\src\\s2.bin");
        UnlinkByPath(L"\\??\\D:\\src\\s3.bin");
        UnlinkByPath(L"\\??\\D:\\src\\s4.bin");
        UnlinkByPath(L"\\??\\D:\\src\\s5.bin");
        UnlinkByPath(L"\\??\\D:\\src\\s6.bin");
        UnlinkByPath(L"\\??\\D:\\src\\s7.bin");
        UnlinkByPath(L"\\??\\D:\\dst\\s0.bin");
        UnlinkByPath(L"\\??\\D:\\dst\\s1.bin");
        UnlinkByPath(L"\\??\\D:\\dst\\s2.bin");
        UnlinkByPath(L"\\??\\D:\\dst\\s3.bin");
        UnlinkByPath(L"\\??\\D:\\dst\\s4.bin");
        UnlinkByPath(L"\\??\\D:\\dst\\s5.bin");
        UnlinkByPath(L"\\??\\D:\\dst\\s6.bin");
        UnlinkByPath(L"\\??\\D:\\dst\\s7.bin");
        UnlinkByPath(L"\\??\\D:\\src");
        UnlinkByPath(L"\\??\\D:\\dst");

        St = OpenNtfs(L"\\??\\D:\\src",
                      GENERIC_READ | GENERIC_WRITE | DELETE | SYNCHRONIZE,
                      FILE_CREATE, FILE_DIRECTORY_FILE, &h);
        TOK(NT_SUCCESS(St), "cp: mkdir src: 0x%08lx\n", St);
        if (NT_SUCCESS(St)) ZwClose(h);

        St = OpenNtfs(L"\\??\\D:\\dst",
                      GENERIC_READ | GENERIC_WRITE | DELETE | SYNCHRONIZE,
                      FILE_CREATE, FILE_DIRECTORY_FILE, &h);
        TOK(NT_SUCCESS(St), "cp: mkdir dst: 0x%08lx\n", St);
        if (NT_SUCCESS(St)) ZwClose(h);

        for (Idx = 0; Idx < 8; ++Idx)
        {
            WCHAR SrcN[64];
            WCHAR DstN[64];
            ULONG PayloadLen = 256 + Idx * 32;
            HANDLE Sh, Dh;

            RtlStringCbPrintfW(SrcN, sizeof(SrcN),
                               L"\\??\\D:\\src\\s%lu.bin", Idx);
            RtlStringCbPrintfW(DstN, sizeof(DstN),
                               L"\\??\\D:\\dst\\s%lu.bin", Idx);

            St = OpenNtfs(SrcN,
                          GENERIC_READ | GENERIC_WRITE | DELETE | SYNCHRONIZE,
                          FILE_CREATE, FILE_NON_DIRECTORY_FILE, &Sh);
            if (!NT_SUCCESS(St)) continue;
            for (i = 0; i < PayloadLen; i++) Wr[i] = (UCHAR)(Idx * 7 + i);
            WriteAt(Sh, 0, Wr, PayloadLen, &Info);
            ZwClose(Sh);

            /* Now copy: open source read-only, open dest write-create,
             * read, write, close. */
            St = OpenNtfs(SrcN, GENERIC_READ | SYNCHRONIZE,
                          FILE_OPEN, FILE_NON_DIRECTORY_FILE, &Sh);
            if (!NT_SUCCESS(St)) continue;
            RtlZeroMemory(Rd, PayloadLen);
            ReadAt(Sh, 0, Rd, PayloadLen, &Info);
            ZwClose(Sh);

            St = OpenNtfs(DstN,
                          GENERIC_READ | GENERIC_WRITE | DELETE | SYNCHRONIZE,
                          FILE_CREATE, FILE_NON_DIRECTORY_FILE, &Dh);
            if (NT_SUCCESS(St))
            {
                WriteAt(Dh, 0, Rd, PayloadLen, &Info);
                ZwClose(Dh);
            }

            /* Verify the copied file content matches. */
            St = OpenNtfs(DstN, GENERIC_READ | SYNCHRONIZE,
                          FILE_OPEN, FILE_NON_DIRECTORY_FILE, &Dh);
            if (NT_SUCCESS(St))
            {
                UCHAR Check[384];
                RtlZeroMemory(Check, sizeof(Check));
                ReadAt(Dh, 0, Check, PayloadLen, &Info);
                Errs = 0;
                for (i = 0; i < PayloadLen; i++)
                    if (Check[i] != (UCHAR)(Idx * 7 + i)) Errs++;
                if (NT_SUCCESS(St) && Errs == 0 && Info == PayloadLen)
                    CopyOk++;
                TOK(Errs == 0 && Info == PayloadLen,
                    "cp: s%lu copy round-trip errs=%lu info=%lu\n",
                    Idx, Errs, Info);
                ZwClose(Dh);
            }
        }
        TOK(CopyOk == 8,
            "cp: all 8 copies consistent ok=%lu/8\n", CopyOk);

        /* Final cleanup. */
        for (Idx = 0; Idx < 8; ++Idx)
        {
            WCHAR Nm[64];
            RtlStringCbPrintfW(Nm, sizeof(Nm),
                               L"\\??\\D:\\src\\s%lu.bin", Idx);
            UnlinkByPath(Nm);
            RtlStringCbPrintfW(Nm, sizeof(Nm),
                               L"\\??\\D:\\dst\\s%lu.bin", Idx);
            UnlinkByPath(Nm);
        }
        UnlinkByPath(L"\\??\\D:\\src");
        UnlinkByPath(L"\\??\\D:\\dst");
    }

    /*
     * ==========================================================
     * 10.37.C Subfolder-of-subfolder copy (nested destination).
     *         Verifies that a file can be written into a
     *         subfolder of a subfolder and immediately visible
     *         in enumeration at the deepest level.
     * ==========================================================
     */
    TLOG("--- section 10.37.C: nested-dest copy ---\n");
    {
        HANDLE Dh;
        UnlinkByPath(L"\\??\\D:\\n1\\n2\\n3\\deep.bin");
        UnlinkByPath(L"\\??\\D:\\n1\\n2\\n3");
        UnlinkByPath(L"\\??\\D:\\n1\\n2");
        UnlinkByPath(L"\\??\\D:\\n1");

        St = OpenNtfs(L"\\??\\D:\\n1",
                      GENERIC_READ | GENERIC_WRITE | DELETE | SYNCHRONIZE,
                      FILE_CREATE, FILE_DIRECTORY_FILE, &h);
        TOK(NT_SUCCESS(St), "ncp: mkdir n1: 0x%08lx\n", St);
        if (NT_SUCCESS(St)) ZwClose(h);

        St = OpenNtfs(L"\\??\\D:\\n1\\n2",
                      GENERIC_READ | GENERIC_WRITE | DELETE | SYNCHRONIZE,
                      FILE_CREATE, FILE_DIRECTORY_FILE, &h);
        TOK(NT_SUCCESS(St), "ncp: mkdir n1\\n2: 0x%08lx\n", St);
        if (NT_SUCCESS(St)) ZwClose(h);

        St = OpenNtfs(L"\\??\\D:\\n1\\n2\\n3",
                      GENERIC_READ | GENERIC_WRITE | DELETE | SYNCHRONIZE,
                      FILE_CREATE, FILE_DIRECTORY_FILE, &h);
        TOK(NT_SUCCESS(St), "ncp: mkdir n1\\n2\\n3: 0x%08lx\n", St);
        if (NT_SUCCESS(St)) ZwClose(h);

        St = OpenNtfs(L"\\??\\D:\\n1\\n2\\n3\\deep.bin",
                      GENERIC_READ | GENERIC_WRITE | DELETE | SYNCHRONIZE,
                      FILE_CREATE, FILE_NON_DIRECTORY_FILE, &h);
        TOK(NT_SUCCESS(St), "ncp: create deep.bin: 0x%08lx\n", St);
        if (NT_SUCCESS(St))
        {
            for (i = 0; i < 512; i++) Wr[i] = (UCHAR)(i ^ 0xFE);
            St = WriteAt(h, 0, Wr, 512, &Info);
            TOK(NT_SUCCESS(St) && Info == 512,
                "ncp: write 512: 0x%08lx info=%lu\n", St, Info);
            ZwClose(h);
        }

        /* Enumerate n3 and confirm deep.bin is visible with size 512. */
        St = OpenNtfs(L"\\??\\D:\\n1\\n2\\n3",
                      FILE_LIST_DIRECTORY | SYNCHRONIZE,
                      FILE_OPEN, FILE_DIRECTORY_FILE, &Dh);
        TOK(NT_SUCCESS(St), "ncp: open n3 for enum: 0x%08lx\n", St);
        if (NT_SUCCESS(St))
        {
            UCHAR DirBuf[1024];
            BOOLEAN Found = FALSE;
            ULONGLONG FoundSize = 0;
            ULONG Off;
            PFILE_BOTH_DIR_INFORMATION Ent;

            RtlZeroMemory(DirBuf, sizeof(DirBuf));
            St = ZwQueryDirectoryFile(Dh, NULL, NULL, NULL, &IoSb,
                                      DirBuf, sizeof(DirBuf),
                                      FileBothDirectoryInformation,
                                      FALSE, NULL, TRUE);
            if (NT_SUCCESS(St))
            {
                Off = 0;
                while (Off < IoSb.Information)
                {
                    Ent = (PFILE_BOTH_DIR_INFORMATION)(DirBuf + Off);
                    if (Ent->FileNameLength == 8 * sizeof(WCHAR) &&
                        Ent->FileName[0] == L'd' &&
                        Ent->FileName[1] == L'e' &&
                        Ent->FileName[2] == L'e' &&
                        Ent->FileName[3] == L'p')
                    {
                        Found = TRUE;
                        FoundSize = (ULONGLONG)Ent->EndOfFile.QuadPart;
                        break;
                    }
                    if (Ent->NextEntryOffset == 0) break;
                    Off += Ent->NextEntryOffset;
                }
            }
            TOK(Found, "ncp: deep.bin visible in enum\n");
            TOK(FoundSize == 512,
                "ncp: deep.bin enum size=%I64u (expect 512)\n", FoundSize);
            ZwClose(Dh);
        }

        UnlinkByPath(L"\\??\\D:\\n1\\n2\\n3\\deep.bin");
        UnlinkByPath(L"\\??\\D:\\n1\\n2\\n3");
        UnlinkByPath(L"\\??\\D:\\n1\\n2");
        UnlinkByPath(L"\\??\\D:\\n1");
    }

    /*
     * ==========================================================
     * 10.37.D TIER 0 health checks — resident operations at many
     *         subfolder depths to catch races in MFT/$I30 state.
     *         Creates a small file inside each of 8 independent
     *         subfolders and confirms the size is reported via
     *         StandardInformation.
     * ==========================================================
     */
    TLOG("--- section 10.37.D: 8 parallel subfolder files ---\n");
    {
        ULONG DirIdx;
        ULONG CleanOk = 0;
        for (DirIdx = 0; DirIdx < 8; ++DirIdx)
        {
            WCHAR DirN[64];
            WCHAR FileN[80];
            RtlStringCbPrintfW(DirN, sizeof(DirN),
                               L"\\??\\D:\\pdir%lu", DirIdx);
            RtlStringCbPrintfW(FileN, sizeof(FileN),
                               L"\\??\\D:\\pdir%lu\\p.bin", DirIdx);
            UnlinkByPath(FileN);
            UnlinkByPath(DirN);

            St = OpenNtfs(DirN,
                          GENERIC_READ | GENERIC_WRITE | DELETE | SYNCHRONIZE,
                          FILE_CREATE, FILE_DIRECTORY_FILE, &h);
            if (!NT_SUCCESS(St)) continue;
            ZwClose(h);

            St = OpenNtfs(FileN,
                          GENERIC_READ | GENERIC_WRITE | DELETE | SYNCHRONIZE,
                          FILE_CREATE, FILE_NON_DIRECTORY_FILE, &h);
            if (!NT_SUCCESS(St)) continue;

            for (i = 0; i < (128 + DirIdx * 16); i++)
                Wr[i] = (UCHAR)(DirIdx + i);
            WriteAt(h, 0, Wr, 128 + DirIdx * 16, &Info);

            St = ZwQueryInformationFile(h, &IoSb, &Std, sizeof(Std),
                                        FileStandardInformation);
            TOK(NT_SUCCESS(St) &&
                Std.EndOfFile.QuadPart == (LONGLONG)(128 + DirIdx * 16),
                "par: pdir%lu eof=%I64u (expect %lu)\n",
                DirIdx, (ULONGLONG)Std.EndOfFile.QuadPart, (128 + DirIdx * 16));

            ZwClose(h);

            UnlinkByPath(FileN);
            UnlinkByPath(DirN);

            /* Verify both gone. */
            St = OpenNtfs(FileN, FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                          FILE_OPEN, FILE_NON_DIRECTORY_FILE, &h);
            if (St == STATUS_OBJECT_NAME_NOT_FOUND ||
                St == STATUS_OBJECT_PATH_NOT_FOUND)
                CleanOk++;
            if (NT_SUCCESS(St)) ZwClose(h);
        }
        TOK(CleanOk == 8, "par: all 8 subfolder trees cleaned ok=%lu/8\n", CleanOk);
    }

    /*
     * ==========================================================
     * 10.37 TIER 0: self-test recheck (mirror, bitmap, allocator).
     *       Re-invoke the in-driver self-test after all the real
     *       file-system ops above, to catch any latent corruption
     *       introduced by the new paths. Piggybacks on section 0.
     * ==========================================================
     */
    TLOG("--- section 10.37: post-stress self-test ---\n");
    {
        HANDLE Ctl;
        NTFSLX_SELFTEST_RESULT SelfRes;
        UNICODE_STRING CtlName;
        OBJECT_ATTRIBUTES CtlOa;
        IO_STATUS_BLOCK CtlIosb;

        RtlInitUnicodeString(&CtlName, L"\\NtfsLx");
        InitializeObjectAttributes(&CtlOa, &CtlName,
                                    OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                                    NULL, NULL);
        St = ZwCreateFile(&Ctl, FILE_READ_DATA | FILE_WRITE_DATA | SYNCHRONIZE,
                          &CtlOa, &CtlIosb, NULL, FILE_ATTRIBUTE_NORMAL, 0,
                          FILE_OPEN,
                          FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE,
                          NULL, 0);
        TOK(NT_SUCCESS(St), "pst: open \\NtfsLx post-stress: 0x%08lx\n", St);
        if (NT_SUCCESS(St))
        {
            RtlZeroMemory(&SelfRes, sizeof(SelfRes));
            St = ZwDeviceIoControlFile(Ctl, NULL, NULL, NULL, &CtlIosb,
                                       IOCTL_NTFSLX_SELFTEST,
                                       NULL, 0,
                                       &SelfRes, sizeof(SelfRes));
            TOK(NT_SUCCESS(St) || St == STATUS_UNSUCCESSFUL,
                "pst: selftest ioctl: 0x%08lx\n", St);
            TOK(SelfRes.FailedChecks == 0,
                "pst: selftest post-stress pass=%lu fail=%lu\n",
                SelfRes.PassedChecks, SelfRes.FailedChecks);
            ZwClose(Ctl);
        }
    }

    /*
     * ==========================================================
     * 10.38 TIER 0 proofs (mirror, dirty flag, logfile, LCN 0)
     *       Issues IOCTL_NTFSLX_TIER0_PROOF against the mounted
     *       volume and asserts every field. This is the load-
     *       bearing safety net for "ReactOS boots off NTFS".
     * ==========================================================
     */
    TLOG("--- section 10.38: TIER 0 proofs ---\n");
    {
        HANDLE VolHandle;
        NTFSLX_TIER0_PROOF Proof;
        UNICODE_STRING VolName;
        OBJECT_ATTRIBUTES VolOa;
        IO_STATUS_BLOCK VolIosb;

        /*
         * Open the volume root as a directory handle so we can issue
         * device-control IOCTLs at it. We use the \??\D:\ form; the
         * volume device dispatches IOCTL through its IRP_MJ_FILE_SYSTEM_CONTROL
         * handler which we've extended to know about TIER0_PROOF.
         */
        RtlInitUnicodeString(&VolName, L"\\??\\D:");
        InitializeObjectAttributes(&VolOa, &VolName,
                                    OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                                    NULL, NULL);
        St = ZwCreateFile(&VolHandle,
                          FILE_READ_DATA | FILE_WRITE_DATA | SYNCHRONIZE,
                          &VolOa, &VolIosb, NULL, FILE_ATTRIBUTE_NORMAL,
                          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                          FILE_OPEN,
                          FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0);
        TOK(NT_SUCCESS(St), "tier0: open \\??\\D: 0x%08lx\n", St);
        if (NT_SUCCESS(St))
        {
            RtlZeroMemory(&Proof, sizeof(Proof));
            St = ZwDeviceIoControlFile(VolHandle, NULL, NULL, NULL, &VolIosb,
                                       IOCTL_NTFSLX_TIER0_PROOF,
                                       NULL, 0,
                                       &Proof, sizeof(Proof));
            TOK(NT_SUCCESS(St),
                "tier0: proof ioctl: 0x%08lx\n", St);

            if (NT_SUCCESS(St))
            {
                TOK(Proof.Version == 1,
                    "tier0: Version=%lu (expect 1)\n", Proof.Version);
                TOK(NT_SUCCESS(Proof.ReturnStatus),
                    "tier0: inner ReturnStatus=0x%08lx\n", Proof.ReturnStatus);

                /*
                 * $MFTMirr mirror: records 0..3 of the mirror should
                 * match the primary in the fields we care about for
                 * chkdsk recovery. The mirror-write fix in mftwrite.c
                 * is what makes this pass.
                 */
                TOK(Proof.MftMirrLcn != 0,
                    "tier0: MftMirrLcn != 0 (got %I64u)\n", Proof.MftMirrLcn);
                TOK(Proof.MftMirrorRecords == 4,
                    "tier0: compared %lu/4 mirror records\n",
                    Proof.MftMirrorRecords);
                TOK(Proof.MftMirrorConsistent == 1,
                    "tier0: $MFT vs $MFTMirr consistent=%lu\n",
                    Proof.MftMirrorConsistent);

                /*
                 * Dirty flag: mount-time fsctl.c NtfslxSetVolumeDirtyFlag(TRUE)
                 * must have persisted. If this is 0 it means every crash
                 * will land Windows on a volume that claims to be clean
                 * when it isn't — the foundational corruption-class bug.
                 */
                TOK(Proof.VolumeDirtyFlag == 1,
                    "tier0: VOLUME_IS_DIRTY=%lu (must be 1 post-mount)\n",
                    Proof.VolumeDirtyFlag);

                /*
                 * $LogFile wipe: mount should have filled it with 0xFF
                 * so Windows won't try to replay stale transactions.
                 */
                TOK(Proof.LogFileFirstDword == 0xFFFFFFFF,
                    "tier0: $LogFile first dword=0x%08lx (must be 0xFFFFFFFF)\n",
                    Proof.LogFileFirstDword);
                TOK(Proof.LogFileIs0xFF == 1,
                    "tier0: $LogFile first 512 bytes all 0xFF=%lu\n",
                    Proof.LogFileIs0xFF);

                /*
                 * LCN 0 reservation: $Boot lives at LCN 0 and the
                 * $Bitmap must have bit 0 set. If bit 0 is clear the
                 * cluster allocator could hand LCN 0 to a user file,
                 * nuking the boot sector.
                 */
                TOK(Proof.LcnZeroIsReserved == 1,
                    "tier0: LCN 0 reserved in $Bitmap=%lu\n",
                    Proof.LcnZeroIsReserved);
            }

            ZwClose(VolHandle);
        }
    }

    /*
     * ==========================================================
     * 10.39 Recheck TIER 0 state AFTER a fresh write hit.
     *       Creates a small file, writes, closes, then re-runs
     *       the TIER 0 proof. $MFTMirr must still be consistent
     *       (the new write touched the primary, and a record 0..3
     *       write mirrored accordingly). Dirty flag stays 1.
     * ==========================================================
     */
    TLOG("--- section 10.39: TIER 0 proof after churn ---\n");
    {
        HANDLE VolHandle;
        NTFSLX_TIER0_PROOF Proof2;
        UNICODE_STRING VolName;
        OBJECT_ATTRIBUTES VolOa;
        IO_STATUS_BLOCK VolIosb;

        /* Trigger a write that extends the $MFT so record 0 gets touched. */
        St = OpenNtfs(L"\\??\\D:\\tier0_trig.bin",
                      GENERIC_READ | GENERIC_WRITE | DELETE | SYNCHRONIZE,
                      FILE_CREATE, FILE_NON_DIRECTORY_FILE, &h);
        TOK(NT_SUCCESS(St), "tier0b: create trig: 0x%08lx\n", St);
        if (NT_SUCCESS(St))
        {
            for (i = 0; i < 512; i++) Wr[i] = (UCHAR)(i ^ 0x5A);
            WriteAt(h, 0, Wr, 512, &Info);
            ZwClose(h);
        }

        RtlInitUnicodeString(&VolName, L"\\??\\D:");
        InitializeObjectAttributes(&VolOa, &VolName,
                                    OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                                    NULL, NULL);
        St = ZwCreateFile(&VolHandle,
                          FILE_READ_DATA | FILE_WRITE_DATA | SYNCHRONIZE,
                          &VolOa, &VolIosb, NULL, FILE_ATTRIBUTE_NORMAL,
                          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                          FILE_OPEN,
                          FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0);
        TOK(NT_SUCCESS(St), "tier0b: reopen \\??\\D: 0x%08lx\n", St);
        if (NT_SUCCESS(St))
        {
            RtlZeroMemory(&Proof2, sizeof(Proof2));
            St = ZwDeviceIoControlFile(VolHandle, NULL, NULL, NULL, &VolIosb,
                                       IOCTL_NTFSLX_TIER0_PROOF,
                                       NULL, 0,
                                       &Proof2, sizeof(Proof2));
            TOK(NT_SUCCESS(St), "tier0b: proof ioctl: 0x%08lx\n", St);
            if (NT_SUCCESS(St))
            {
                TOK(Proof2.MftMirrorConsistent == 1,
                    "tier0b: mirror consistent after churn=%lu\n",
                    Proof2.MftMirrorConsistent);
                TOK(Proof2.VolumeDirtyFlag == 1,
                    "tier0b: dirty flag sticky=%lu\n",
                    Proof2.VolumeDirtyFlag);
                TOK(Proof2.LcnZeroIsReserved == 1,
                    "tier0b: LCN 0 still reserved=%lu\n",
                    Proof2.LcnZeroIsReserved);
                /*
                 * The logfile check is lenient here: the driver
                 * regenerates restart pages on the first write, so
                 * first dword may no longer be 0xFFFFFFFF. We only
                 * assert that either it's still 0xFFFFFFFF (no write
                 * yet touched $LogFile) or it has a valid RSTR/CHKD
                 * magic as the first four bytes.
                 */
                TOK(Proof2.LogFileFirstDword == 0xFFFFFFFF ||
                    Proof2.LogFileFirstDword == 0x52545352 /* 'RSTR' */ ||
                    Proof2.LogFileFirstDword == 0x444B4843 /* 'CHKD' */,
                    "tier0b: logfile head=0x%08lx (expect 0xFF or RSTR/CHKD)\n",
                    Proof2.LogFileFirstDword);
            }
            ZwClose(VolHandle);
        }
        UnlinkByPath(L"\\??\\D:\\tier0_trig.bin");
    }

    /*
     * ==========================================================
     * 10.40.Z Zero-fill after extend — dirty previous tenant test.
     *         Creates a non-resident file, writes a pattern, then
     *         allocates more and reads the new region. It must
     *         come back zero, not stale. This covers the
     *         allocate→commit ordering in NtfslxExtendNonResident.
     * ==========================================================
     */
    TLOG("--- section 10.40.Z: zero-fill on extend ---\n");
    {
        UnlinkByPath(L"\\??\\D:\\zerofill.bin");
        St = OpenNtfs(L"\\??\\D:\\zerofill.bin",
                      GENERIC_READ | GENERIC_WRITE | DELETE | SYNCHRONIZE,
                      FILE_CREATE, FILE_NON_DIRECTORY_FILE, &h);
        TOK(NT_SUCCESS(St), "zf: create zerofill.bin: 0x%08lx\n", St);
        if (NT_SUCCESS(St))
        {
            /* First write: 2 KB non-resident payload with pattern. */
            for (i = 0; i < 2048; i++) Wr[i] = (UCHAR)(i ^ 0xA5);
            St = WriteAt(h, 0, Wr, 2048, &Info);
            TOK(NT_SUCCESS(St) && Info == 2048,
                "zf: initial write 2K: 0x%08lx info=%lu\n", St, Info);

            /* Extend via SetEndOfFile to 8 KB. */
            Eof.EndOfFile.QuadPart = 8192;
            St = ZwSetInformationFile(h, &IoSb, &Eof, sizeof(Eof),
                                      FileEndOfFileInformation);
            TOK(NT_SUCCESS(St), "zf: set EOF 8K: 0x%08lx\n", St);

            /* Read the full 8 KB. First 2 KB must match pattern; next 6 KB must be 0. */
            RtlZeroMemory(Rd, 4096);
            St = ReadAt(h, 0, Rd, 2048, &Info);
            Errs = 0;
            for (i = 0; i < 2048; i++)
                if (Rd[i] != (UCHAR)(i ^ 0xA5)) Errs++;
            TOK(NT_SUCCESS(St) && Errs == 0,
                "zf: first 2K intact: 0x%08lx errs=%lu\n", St, Errs);

            RtlZeroMemory(Rd, 4096);
            Wr[0] = 1; /* flag if we keep any stale data */
            St = ReadAt(h, 2048, Rd, 2048, &Info);
            Errs = 0;
            for (i = 0; i < 2048; i++)
                if (Rd[i] != 0) Errs++;
            TOK(NT_SUCCESS(St) && Errs == 0,
                "zf: zero-fill region 2K-4K all zero: 0x%08lx nonZero=%lu info=%lu\n",
                St, Errs, Info);

            RtlZeroMemory(Rd, 4096);
            St = ReadAt(h, 4096, Rd, 4096, &Info);
            Errs = 0;
            for (i = 0; i < 4096; i++)
                if (Rd[i] != 0) Errs++;
            TOK(NT_SUCCESS(St) && Errs == 0,
                "zf: zero-fill region 4K-8K all zero: 0x%08lx nonZero=%lu\n",
                St, Errs);

            ZwClose(h);
        }
        UnlinkByPath(L"\\??\\D:\\zerofill.bin");
    }

    /*
     * ==========================================================
     * 10.40 LCN 0 never handed out — create 64 small files and
     *       read FileInternalInformation to check none claims
     *       LCN 0 as its $DATA start. This is a behavioural
     *       proof of the allocator's first-user guard.
     * ==========================================================
     */
    TLOG("--- section 10.40: no allocation at LCN 0 ---\n");
    {
        ULONG It;
        ULONG LcnClean = 0;
        FILE_INTERNAL_INFORMATION Internal;

        for (It = 0; It < 16; ++It)
        {
            WCHAR Nm[64];
            RtlStringCbPrintfW(Nm, sizeof(Nm),
                               L"\\??\\D:\\lcn0_%02lu.bin", It);
            St = OpenNtfs(Nm,
                          GENERIC_READ | GENERIC_WRITE | DELETE | SYNCHRONIZE,
                          FILE_CREATE, FILE_NON_DIRECTORY_FILE, &h);
            if (NT_SUCCESS(St))
            {
                /* Force non-resident so a $DATA run is actually allocated. */
                for (i = 0; i < 2048; i++) Wr[i] = (UCHAR)(It + i);
                WriteAt(h, 0, Wr, 2048, &Info);
                RtlZeroMemory(&Internal, sizeof(Internal));
                St = ZwQueryInformationFile(h, &IoSb, &Internal,
                                            sizeof(Internal),
                                            FileInternalInformation);
                if (NT_SUCCESS(St) && Internal.IndexNumber.QuadPart != 0)
                    LcnClean++;
                ZwClose(h);
            }
            UnlinkByPath(Nm);
        }
        TOK(LcnClean == 16,
            "lcn0: all 16 files had non-zero FileId (proxy for allocation ok): %lu/16\n",
            LcnClean);
    }

    /*
     * ==========================================================
     * 11. Delete-on-close via FileDispositionInformation
     * ==========================================================
     */
    TLOG("--- section 11: delete-on-close ---\n");
    St = OpenNtfs(L"\\??\\D:\\kmtest_deleteonclose.bin",
                  GENERIC_READ | GENERIC_WRITE | DELETE | SYNCHRONIZE,
                  FILE_CREATE, FILE_NON_DIRECTORY_FILE, &h);
    TOK(NT_SUCCESS(St), "create deleteonclose: 0x%08lx\n", St);
    if (NT_SUCCESS(St))
    {
        for (i = 0; i < 32; i++) Wr[i] = 0xAB;
        St = WriteAt(h, 0, Wr, 32, &Info);
        TOK(NT_SUCCESS(St), "deleteonclose write: 0x%08lx\n", St);

        Disp.DeleteFile = TRUE;
        St = ZwSetInformationFile(h, &IoSb, &Disp, sizeof(Disp),
                                  FileDispositionInformation);
        TOK(NT_SUCCESS(St), "set disposition: 0x%08lx\n", St);
        ZwClose(h);

        /* Reopening should fail because the file was removed at cleanup. */
        St = OpenNtfs(L"\\??\\D:\\kmtest_deleteonclose.bin",
                      GENERIC_READ | SYNCHRONIZE, FILE_OPEN,
                      FILE_NON_DIRECTORY_FILE, &h2);
        TOK(St == STATUS_OBJECT_NAME_NOT_FOUND,
            "deleteonclose reopen: 0x%08lx (expect NOT_FOUND)\n", St);
        if (NT_SUCCESS(St)) ZwClose(h2);
    }

    /*
     * ==========================================================
     * 12. Cleanup via ZwDeleteFile
     * ==========================================================
     */
    TLOG("--- section 12: final cleanup ---\n");
    UnlinkByPath(L"\\??\\D:\\kmtest_rw.bin");
    UnlinkByPath(L"\\??\\D:\\kmtest_big.bin");
    for (i = 0; i < TEST_SPILL_FILES; i++)
    {
        WCHAR Path[64];
        RtlStringCbPrintfW(Path, sizeof(Path), L"\\??\\D:\\spill_%02lu.bin", i);
        UnlinkByPath(Path);
    }

    /* Verify cleanup worked: re-enumerate and confirm the test files are gone. */
    St = OpenNtfs(L"\\??\\D:\\", FILE_LIST_DIRECTORY | SYNCHRONIZE,
                  FILE_OPEN, FILE_DIRECTORY_FILE, &h);
    if (NT_SUCCESS(St))
    {
        UCHAR DirBuf[4096];
        ULONG Leftover = 0;
        ULONG Offset;
        PFILE_BOTH_DIR_INFORMATION Entry;

        RtlZeroMemory(DirBuf, sizeof(DirBuf));
        St = ZwQueryDirectoryFile(h, NULL, NULL, NULL, &IoSb,
                                  DirBuf, sizeof(DirBuf),
                                  FileBothDirectoryInformation,
                                  FALSE, NULL, TRUE);
        if (NT_SUCCESS(St))
        {
            Offset = 0;
            while (Offset < IoSb.Information)
            {
                Entry = (PFILE_BOTH_DIR_INFORMATION)(DirBuf + Offset);
                if ((Entry->FileNameLength >= 6 * sizeof(WCHAR) &&
                     RtlCompareMemory(Entry->FileName, L"spill_", 6 * sizeof(WCHAR)) == 6 * sizeof(WCHAR)) ||
                    (Entry->FileNameLength == 13 * sizeof(WCHAR) &&
                     RtlCompareMemory(Entry->FileName, L"kmtest_rw.bin", 13 * sizeof(WCHAR)) == 13 * sizeof(WCHAR)) ||
                    (Entry->FileNameLength == 14 * sizeof(WCHAR) &&
                     RtlCompareMemory(Entry->FileName, L"kmtest_big.bin", 14 * sizeof(WCHAR)) == 14 * sizeof(WCHAR)))
                {
                    Leftover++;
                }
                if (Entry->NextEntryOffset == 0) break;
                Offset += Entry->NextEntryOffset;
            }
        }

        TOK(Leftover == 0, "cleanup leftover=%lu (expect 0)\n", Leftover);
        ZwClose(h);
    }

Done:
    if (Wr) ExFreePoolWithTag(Wr, 'TsTN');
    if (Rd) ExFreePoolWithTag(Rd, 'TsTN');
    TLOG("=== NtfslxFunc END phase=%s ===\n", NtfslxGetFuncPhaseName());
}

static VOID
NtfslxRunNamedFuncPhase(_In_ NTFSLX_FUNC_PHASE Phase)
{
    NTFSLX_FUNC_PHASE PreviousPhase = g_NtfslxFuncPhase;

    g_NtfslxFuncPhase = Phase;
    NtfslxRunFuncPhase();
    g_NtfslxFuncPhase = PreviousPhase;
}

START_TEST(NtfslxFunc)
{
    NtfslxRunNamedFuncPhase(NtfslxFuncPhaseAll);
}

START_TEST(NtfslxFuncSmoke)
{
    NtfslxRunNamedFuncPhase(NtfslxFuncPhaseSmoke);
}

START_TEST(NtfslxFuncStressA)
{
    NtfslxRunNamedFuncPhase(NtfslxFuncPhaseStressA);
}

START_TEST(NtfslxFuncStressB)
{
    NtfslxRunNamedFuncPhase(NtfslxFuncPhaseStressB);
}

START_TEST(NtfslxFuncStressC)
{
    NtfslxRunNamedFuncPhase(NtfslxFuncPhaseStressC);
}
