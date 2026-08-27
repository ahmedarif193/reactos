/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     NT 10+ RTL behavioral compatibility tests
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include <kmt_test.h>

typedef struct _TEST_RTL_AVL_TREE
{
    PRTL_BALANCED_NODE Root;
} TEST_RTL_AVL_TREE, *PTEST_RTL_AVL_TREE;

typedef struct _TEST_RTL_AVL_ENTRY
{
    RTL_BALANCED_NODE Node;
    LONG Key;
} TEST_RTL_AVL_ENTRY, *PTEST_RTL_AVL_ENTRY;

typedef struct _TEST_RTL_MULTI_TIME_PRECISE
{
    ULONGLONG PerformanceCounter;
    ULONGLONG HostPerformanceCounter;
    ULONGLONG SystemTime;
} TEST_RTL_MULTI_TIME_PRECISE, *PTEST_RTL_MULTI_TIME_PRECISE;

VOID NTAPI RtlAvlInsertNodeEx(_Inout_ PTEST_RTL_AVL_TREE Tree, _In_opt_ PRTL_BALANCED_NODE Parent, _In_ BOOLEAN Right, _Inout_ PRTL_BALANCED_NODE Node);
VOID NTAPI RtlAvlRemoveNode(_Inout_ PTEST_RTL_AVL_TREE Tree, _Inout_ PRTL_BALANCED_NODE Node);
PWCHAR NTAPI RtlFindUnicodeSubstring(_In_ PCUNICODE_STRING String, _In_ PCUNICODE_STRING SubString, _In_ BOOLEAN CaseInsensitive);
NTSTATUS NTAPI RtlGetMultiTimePrecise(_Out_ PTEST_RTL_MULTI_TIME_PRECISE TimeValues, _In_ ULONG RequestedValues, _Out_ PULONG ReturnedValues);
NTSTATUS NTAPI RtlConvertHostPerfCounterToPerfCounter(_In_ ULONGLONG HostPerformanceCounter, _In_ ULONGLONG MaximumError, _Out_ PULONGLONG PerformanceCounter);
NTSTATUS NTAPI RtlGenerateClass5Guid(_In_ REFGUID NamespaceGuid, _In_reads_bytes_(BufferSize) PVOID Buffer, _In_ ULONG BufferSize, _Out_ GUID *Guid);
NTSTATUS NTAPI RtlGetSystemGlobalData(_In_ ULONG DataId, _Out_writes_bytes_(Size) PVOID Buffer, _In_ ULONG Size);
NTSTATUS NTAPI RtlGetThreadLangIdByIndex(_In_ ULONG Flags, _In_ ULONG Index, _Out_ PULONG LangId, _Out_opt_ PULONG LangIdCount);
BOOLEAN NTAPI RtlIsStateSeparationEnabled(VOID);
NTSTATUS NTAPI RtlQueryElevationFlags(_Out_ PULONG Flags);
NTSTATUS NTAPI RtlGetAcesBufferSize(_In_ PACL Acl, _Out_ PULONG AcesBufferSize);
NTSTATUS NTAPI RtlQueryPackageIdentity(_In_opt_ PVOID TokenObject, _Out_writes_bytes_to_opt_(*PackageSize, *PackageSize) PWSTR PackageFullName, _Inout_ PSIZE_T PackageSize, _Out_writes_bytes_to_opt_(*AppIdSize, *AppIdSize) PWSTR AppId, _Inout_opt_ PSIZE_T AppIdSize, _Out_opt_ PBOOLEAN Packaged);
VOID NTAPI RtlIntersectBitMaps(_Inout_ PRTL_BITMAP Destination, _In_ PRTL_BITMAP Source);
BOOLEAN NTAPI RtlGetIntegerAtom(_In_ PCWSTR AtomName, _Out_opt_ PRTL_ATOM Atom);
NTSTATUS NTAPI RtlCheckTokenMembership(_In_opt_ HANDLE TokenHandle, _In_ PSID SidToCheck, _Out_ PBOOLEAN IsMember);
ULONG NTAPI RtlGetCurrentServiceSessionId(VOID);
BOOLEAN NTAPI RtlIsZeroMemory(_In_reads_bytes_(Length) PVOID Buffer, _In_ SIZE_T Length);
VOID NTAPI RtlSetActiveConsoleId(_In_ ULONG ActiveConsoleId);
VOID NTAPI RtlSetConsoleSessionForegroundProcessId(_In_ ULONGLONG ProcessId);

#if defined(_M_AMD64) || defined(_M_ARM64)
typedef struct _RTL_BITMAP_EX
{
    ULONGLONG SizeOfBitMap;
    PULONGLONG Buffer;
} RTL_BITMAP_EX, *PRTL_BITMAP_EX;

VOID NTAPI RtlInitializeBitMapEx(_Out_ PRTL_BITMAP_EX BitMapHeader, _In_opt_ PULONGLONG BitMapBuffer, _In_ ULONGLONG SizeOfBitMap);
VOID NTAPI RtlClearAllBitsEx(_Inout_ PRTL_BITMAP_EX BitMapHeader);
VOID NTAPI RtlClearBitEx(_Inout_ PRTL_BITMAP_EX BitMapHeader, _In_ ULONGLONG BitNumber);
VOID NTAPI RtlSetBitEx(_Inout_ PRTL_BITMAP_EX BitMapHeader, _In_ ULONGLONG BitNumber);
BOOLEAN NTAPI RtlAreBitsClearEx(_In_ PRTL_BITMAP_EX BitMapHeader, _In_ ULONGLONG StartingIndex, _In_ ULONGLONG Length);
ULONGLONG NTAPI RtlFindSetBitsEx(_In_ PRTL_BITMAP_EX BitMapHeader, _In_ ULONGLONG NumberToFind, _In_ ULONGLONG HintIndex);
ULONGLONG NTAPI RtlNumberOfSetBitsInRangeEx(_In_ PRTL_BITMAP_EX BitMapHeader, _In_ ULONGLONG StartingIndex, _In_ ULONGLONG Length);
VOID NTAPI RtlCopyBitMapEx(_In_ PRTL_BITMAP_EX Source, _Inout_ PRTL_BITMAP_EX Destination, _In_ ULONGLONG TargetBit);
VOID NTAPI RtlIntersectBitMapsEx(_Inout_ PRTL_BITMAP_EX Destination, _In_ PRTL_BITMAP_EX Source);
#endif

static
VOID
TestModernBitmaps(VOID)
{
    RTL_BITMAP Destination;
    RTL_BITMAP Source;
#if defined(_M_AMD64) || defined(_M_ARM64)
    RTL_BITMAP_EX DestinationEx;
    RTL_BITMAP_EX SourceEx;
#endif
    ULONG DestinationBits;
    ULONG OverlapBits;
    ULONG SourceBits;
#if defined(_M_AMD64) || defined(_M_ARM64)
    ULONGLONG DestinationBitsEx;
    ULONGLONG SourceBitsEx;
#endif

    SourceBits = 0xB5;
    RtlInitializeBitMap(&Source, &SourceBits, 8);
    ok_eq_ulong(RtlNumberOfSetBitsInRange(&Source, 2, 4), 3);
    ok_eq_ulong(RtlNumberOfSetBitsInRange(&Source, 8, 1), MAXULONG);
    ok_eq_ulong(RtlNumberOfSetBitsInRange(&Source, 0, 0), MAXULONG);
    ok_eq_ulong(RtlNumberOfSetBitsInRange(&Source, 7, 2), MAXULONG);

    SourceBits = 0xD;
    DestinationBits = 0xFFFF;
    RtlInitializeBitMap(&Source, &SourceBits, 4);
    RtlInitializeBitMap(&Destination, &DestinationBits, 16);
    RtlCopyBitMap(&Source, &Destination, 5);
    ok_eq_hex(DestinationBits, 0xFFBF);
    RtlCopyBitMap(&Source, &Destination, 16);
    ok_eq_hex(DestinationBits, 0xFFBF);

    OverlapBits = 0xA5C3;
    RtlInitializeBitMap(&Source, &OverlapBits, 8);
    RtlInitializeBitMap(&Destination, &OverlapBits, 16);
    RtlCopyBitMap(&Source, &Destination, 4);
    ok_eq_hex(OverlapBits, 0xAC33);

    SourceBits = 0xAA;
    DestinationBits = 0xF0F0;
    RtlInitializeBitMap(&Source, &SourceBits, 8);
    RtlInitializeBitMap(&Destination, &DestinationBits, 16);
    RtlIntersectBitMaps(&Destination, &Source);
    ok_eq_hex(DestinationBits, 0xF0A0);

#if defined(_M_AMD64) || defined(_M_ARM64)
    SourceBitsEx = 0xD;
    RtlInitializeBitMapEx(&SourceEx, &SourceBitsEx, 0x100000001ULL);
    ok_eq_ulonglong(SourceEx.SizeOfBitMap, 0x100000001ULL);
    ok_eq_pointer(SourceEx.Buffer, &SourceBitsEx);

    RtlInitializeBitMapEx(&SourceEx, &SourceBitsEx, 4);
    DestinationBitsEx = 0xFFFF;
    RtlInitializeBitMapEx(&DestinationEx, &DestinationBitsEx, 16);
    RtlCopyBitMapEx(&SourceEx, &DestinationEx, 5);
    ok_eq_hex64(DestinationBitsEx, 0xFFBF);
    ok_eq_ulonglong(RtlNumberOfSetBitsInRangeEx(&DestinationEx, 5, 4), 3);
    ok_eq_ulonglong(RtlNumberOfSetBitsInRangeEx(&DestinationEx, 16, 1), MAXULONGLONG);

    SourceBitsEx = 0xAA;
    DestinationBitsEx = 0xF0F0;
    RtlInitializeBitMapEx(&SourceEx, &SourceBitsEx, 8);
    RtlInitializeBitMapEx(&DestinationEx, &DestinationBitsEx, 16);
    RtlIntersectBitMapsEx(&DestinationEx, &SourceEx);
    ok_eq_hex64(DestinationBitsEx, 0xF0A0);

    RtlClearAllBitsEx(&DestinationEx);
    ok_eq_hex64(DestinationBitsEx, 0);
    RtlSetBitEx(&DestinationEx, 6);
    ok_eq_hex64(DestinationBitsEx, 0x40);
    ok_eq_ulonglong(RtlFindSetBitsEx(&DestinationEx, 1, 0), 6);
    ok(RtlAreBitsClearEx(&DestinationEx, 0, 6), "bits 0 through 5 are not clear\n");
    RtlClearBitEx(&DestinationEx, 6);
    ok_eq_hex64(DestinationBitsEx, 0);
#endif
}

static
VOID
TestIntegerAtoms(VOID)
{
    RTL_ATOM Atom;

    Atom = 0xDEAD;
    ok(RtlGetIntegerAtom(NULL, &Atom), "NULL integer atom was rejected\n");
    ok_eq_hex(Atom, 0xC000);

    Atom = 0xDEAD;
    ok(RtlGetIntegerAtom((PCWSTR)(ULONG_PTR)1, &Atom), "integer atom 1 was rejected\n");
    ok_eq_hex(Atom, 1);

    Atom = 0xDEAD;
    ok(RtlGetIntegerAtom((PCWSTR)(ULONG_PTR)0xBFFF, &Atom), "integer atom 0xBFFF was rejected\n");
    ok_eq_hex(Atom, 0xBFFF);

    Atom = 0xDEAD;
    ok(!RtlGetIntegerAtom((PCWSTR)(ULONG_PTR)0xC000, &Atom), "integer atom 0xC000 was accepted\n");
    ok_eq_hex(Atom, 0xDEAD);

    Atom = 0xDEAD;
    ok(RtlGetIntegerAtom(L"#1", &Atom), "string atom #1 was rejected\n");
    ok_eq_hex(Atom, 1);

    Atom = 0xDEAD;
    ok(RtlGetIntegerAtom(L"#49152", &Atom), "string atom #49152 was rejected\n");
    ok_eq_hex(Atom, 0xC000);

    Atom = 0xDEAD;
    ok(RtlGetIntegerAtom(L"#0", &Atom), "string atom #0 was rejected with output\n");
    ok_eq_hex(Atom, 0xC000);
    ok(RtlGetIntegerAtom(L"#0", NULL), "string atom #0 was rejected without output\n");
    ok(RtlGetIntegerAtom(L"#49153", NULL), "out-of-range string was rejected without output\n");

    Atom = 0xDEAD;
    ok(RtlGetIntegerAtom(L"#49153", &Atom), "string atom #49153 was rejected\n");
    ok_eq_hex(Atom, 0xC000);

    Atom = 0xDEAD;
    ok(RtlGetIntegerAtom(L"#4294967296", &Atom), "overflowing string atom was rejected\n");
    ok_eq_hex(Atom, 0xC000);

    ok(!RtlGetIntegerAtom(L"#", &Atom), "empty string atom was accepted\n");
    ok(!RtlGetIntegerAtom(L"#12x", &Atom), "malformed string atom was accepted\n");
    ok(!RtlGetIntegerAtom(L"name", &Atom), "named atom was accepted as integer\n");
}

static
LONG
TestAvlNodeIndex(
    _In_opt_ PRTL_BALANCED_NODE Node,
    _In_reads_(3) PRTL_BALANCED_NODE Nodes)
{
    ULONG Index;

    if (Node == NULL)
        return -1;
    for (Index = 0; Index < 3; ++Index)
    {
        if (Node == &Nodes[Index])
            return Index;
    }
    return -2;
}

static
VOID
TraceAvlState(
    _In_ PCSTR Step,
    _In_ PTEST_RTL_AVL_TREE Tree,
    _In_reads_(3) PRTL_BALANCED_NODE Nodes)
{
    trace("AVL %s root=%ld n0=(l%ld r%ld p%ld b%Iu) n1=(l%ld r%ld p%ld b%Iu) n2=(l%ld r%ld p%ld b%Iu)\n", Step, TestAvlNodeIndex(Tree->Root, Nodes), TestAvlNodeIndex(Nodes[0].Left, Nodes), TestAvlNodeIndex(Nodes[0].Right, Nodes), TestAvlNodeIndex(RTL_BALANCED_NODE_GET_PARENT_POINTER(&Nodes[0]), Nodes), Nodes[0].ParentValue & RTL_BALANCED_NODE_RESERVED_PARENT_MASK, TestAvlNodeIndex(Nodes[1].Left, Nodes), TestAvlNodeIndex(Nodes[1].Right, Nodes), TestAvlNodeIndex(RTL_BALANCED_NODE_GET_PARENT_POINTER(&Nodes[1]), Nodes), Nodes[1].ParentValue & RTL_BALANCED_NODE_RESERVED_PARENT_MASK, TestAvlNodeIndex(Nodes[2].Left, Nodes), TestAvlNodeIndex(Nodes[2].Right, Nodes), TestAvlNodeIndex(RTL_BALANCED_NODE_GET_PARENT_POINTER(&Nodes[2]), Nodes), Nodes[2].ParentValue & RTL_BALANCED_NODE_RESERVED_PARENT_MASK);
}

static
LONG
TestValidateAvlNode(
    _In_opt_ PRTL_BALANCED_NODE Node,
    _In_opt_ PRTL_BALANCED_NODE Parent,
    _In_ LONG Minimum,
    _In_ LONG Maximum,
    _Inout_ PULONG Count,
    _Inout_ PBOOLEAN Valid)
{
    PTEST_RTL_AVL_ENTRY Entry;
    LONG LeftHeight;
    LONG RightHeight;
    LONG Balance;
    ULONG EncodedBalance;

    if (Node == NULL)
        return 0;

    Entry = CONTAINING_RECORD(Node, TEST_RTL_AVL_ENTRY, Node);
    if ((Entry->Key <= Minimum) || (Entry->Key >= Maximum))
    {
        trace("AVL key %ld outside (%ld,%ld)\n", Entry->Key, Minimum, Maximum);
        *Valid = FALSE;
    }
    if (RTL_BALANCED_NODE_GET_PARENT_POINTER(Node) != Parent)
    {
        trace("AVL key %ld has parent %p, expected %p\n", Entry->Key, RTL_BALANCED_NODE_GET_PARENT_POINTER(Node), Parent);
        *Valid = FALSE;
    }

    ++*Count;
    LeftHeight = TestValidateAvlNode(Node->Left, Node, Minimum, Entry->Key, Count, Valid);
    RightHeight = TestValidateAvlNode(Node->Right, Node, Entry->Key, Maximum, Count, Valid);
    Balance = RightHeight - LeftHeight;
    EncodedBalance = Balance < 0 ? 3 : (ULONG)Balance;
    if ((Balance < -1) || (Balance > 1) || ((Node->ParentValue & RTL_BALANCED_NODE_RESERVED_PARENT_MASK) != EncodedBalance))
    {
        trace("AVL key %ld balance %Iu, expected %ld\n", Entry->Key, Node->ParentValue & RTL_BALANCED_NODE_RESERVED_PARENT_MASK, Balance);
        *Valid = FALSE;
    }
    return max(LeftHeight, RightHeight) + 1;
}

static
VOID
TestValidateAvlTree(
    _In_ PTEST_RTL_AVL_TREE Tree,
    _In_ ULONG ExpectedCount,
    _In_ PCSTR Operation,
    _In_ ULONG Step)
{
    BOOLEAN Valid = TRUE;
    ULONG Count = 0;

    TestValidateAvlNode(Tree->Root, NULL, MINLONG, MAXLONG, &Count, &Valid);
    ok(Valid, "AVL invariants failed after %s step %lu\n", Operation, Step);
    ok_eq_ulong(Count, ExpectedCount);
}

static
VOID
TestModernAvlChurn(VOID)
{
    TEST_RTL_AVL_ENTRY Entries[31];
    TEST_RTL_AVL_TREE Tree;
    PRTL_BALANCED_NODE Current;
    PRTL_BALANCED_NODE Parent;
    ULONG Index;
    ULONG Key;
    BOOLEAN Right;

    RtlZeroMemory(Entries, sizeof(Entries));
    Tree.Root = NULL;
    for (Index = 0; Index < RTL_NUMBER_OF(Entries); ++Index)
        Entries[Index].Key = Index;

    for (Index = 0; Index < RTL_NUMBER_OF(Entries); ++Index)
    {
        Key = (Index * 17) % RTL_NUMBER_OF(Entries);
        Parent = NULL;
        Current = Tree.Root;
        Right = FALSE;
        while (Current != NULL)
        {
            Parent = Current;
            Right = Entries[Key].Key > CONTAINING_RECORD(Current, TEST_RTL_AVL_ENTRY, Node)->Key;
            Current = Current->Children[Right != FALSE];
        }
        RtlAvlInsertNodeEx(&Tree, Parent, Right, &Entries[Key].Node);
        TestValidateAvlTree(&Tree, Index + 1, "insert", Index);
    }

    for (Index = 0; Index < RTL_NUMBER_OF(Entries); ++Index)
    {
        Key = ((Index * 13) + 7) % RTL_NUMBER_OF(Entries);
        RtlAvlRemoveNode(&Tree, &Entries[Key].Node);
        TestValidateAvlTree(&Tree, RTL_NUMBER_OF(Entries) - Index - 1, "remove", Index);
    }
    ok_eq_pointer(Tree.Root, NULL);
}

static
VOID
TestModernAvlState(VOID)
{
    RTL_BALANCED_NODE Nodes[3];
    TEST_RTL_AVL_TREE Tree;

    RtlZeroMemory(Nodes, sizeof(Nodes));
    Tree.Root = NULL;

    RtlAvlInsertNodeEx(&Tree, NULL, FALSE, &Nodes[0]);
    TraceAvlState("insert0", &Tree, Nodes);
    ok_eq_pointer(Tree.Root, &Nodes[0]);
    ok_eq_pointer(Nodes[0].Left, NULL);
    ok_eq_pointer(Nodes[0].Right, NULL);
    ok_eq_pointer(RTL_BALANCED_NODE_GET_PARENT_POINTER(&Nodes[0]), NULL);
    ok_eq_ulong(Nodes[0].ParentValue & RTL_BALANCED_NODE_RESERVED_PARENT_MASK, 0);
    RtlAvlInsertNodeEx(&Tree, &Nodes[0], TRUE, &Nodes[1]);
    TraceAvlState("insert1", &Tree, Nodes);
    ok_eq_pointer(Tree.Root, &Nodes[0]);
    ok_eq_pointer(Nodes[0].Right, &Nodes[1]);
    ok_eq_pointer(RTL_BALANCED_NODE_GET_PARENT_POINTER(&Nodes[1]), &Nodes[0]);
    ok_eq_ulong(Nodes[0].ParentValue & RTL_BALANCED_NODE_RESERVED_PARENT_MASK, 1);
    RtlAvlInsertNodeEx(&Tree, &Nodes[1], TRUE, &Nodes[2]);
    TraceAvlState("insert2", &Tree, Nodes);
    ok_eq_pointer(Tree.Root, &Nodes[1]);
    ok_eq_pointer(Nodes[1].Left, &Nodes[0]);
    ok_eq_pointer(Nodes[1].Right, &Nodes[2]);
    ok_eq_pointer(RTL_BALANCED_NODE_GET_PARENT_POINTER(&Nodes[0]), &Nodes[1]);
    ok_eq_pointer(RTL_BALANCED_NODE_GET_PARENT_POINTER(&Nodes[1]), NULL);
    ok_eq_pointer(RTL_BALANCED_NODE_GET_PARENT_POINTER(&Nodes[2]), &Nodes[1]);
    ok_eq_ulong(Nodes[0].ParentValue & RTL_BALANCED_NODE_RESERVED_PARENT_MASK, 0);
    ok_eq_ulong(Nodes[1].ParentValue & RTL_BALANCED_NODE_RESERVED_PARENT_MASK, 0);
    ok_eq_ulong(Nodes[2].ParentValue & RTL_BALANCED_NODE_RESERVED_PARENT_MASK, 0);

    RtlAvlRemoveNode(&Tree, &Nodes[1]);
    TraceAvlState("remove1", &Tree, Nodes);
    ok_eq_pointer(Tree.Root, &Nodes[2]);
    ok_eq_pointer(Nodes[2].Left, &Nodes[0]);
    ok_eq_pointer(Nodes[2].Right, NULL);
    ok_eq_pointer(RTL_BALANCED_NODE_GET_PARENT_POINTER(&Nodes[0]), &Nodes[2]);
    ok_eq_pointer(RTL_BALANCED_NODE_GET_PARENT_POINTER(&Nodes[2]), NULL);
    ok_eq_ulong(Nodes[2].ParentValue & RTL_BALANCED_NODE_RESERVED_PARENT_MASK, 3);
    ok_eq_pointer(Nodes[1].Left, &Nodes[0]);
    ok_eq_pointer(Nodes[1].Right, &Nodes[2]);
    RtlAvlRemoveNode(&Tree, &Nodes[0]);
    TraceAvlState("remove0", &Tree, Nodes);
    ok_eq_pointer(Tree.Root, &Nodes[2]);
    ok_eq_pointer(Nodes[2].Left, NULL);
    ok_eq_ulong(Nodes[2].ParentValue & RTL_BALANCED_NODE_RESERVED_PARENT_MASK, 0);
    RtlAvlRemoveNode(&Tree, &Nodes[2]);
    TraceAvlState("remove2", &Tree, Nodes);
    ok_eq_pointer(Tree.Root, NULL);
}

static
VOID
TestModernRtlState(VOID)
{
    static const GUID DnsNamespace = {0x6ba7b810, 0x9dad, 0x11d1, {0x80, 0xb4, 0x00, 0xc0, 0x4f, 0xd4, 0x30, 0xc8}};
    static const GUID ExpectedGuid = {0x21f7f8de, 0x8051, 0x5b89, {0x86, 0x80, 0x01, 0x95, 0xef, 0x79, 0x8b, 0x6a}};
    static const CHAR GuidName[] = "www.widgets.com";
    UNICODE_STRING String = RTL_CONSTANT_STRING(L"AlphaBetaGamma");
    UNICODE_STRING LowerSubstring = RTL_CONSTANT_STRING(L"beta");
    UNICODE_STRING ExactSubstring = RTL_CONSTANT_STRING(L"Beta");
    TEST_RTL_MULTI_TIME_PRECISE TimeValues;
    RTL_OSVERSIONINFOW Version;
    ACL Acl;
    PACCESS_TOKEN Token;
    GUID Guid;
    ULONGLONG ConvertedCounter;
    SIZE_T PackageSize;
    SIZE_T AppIdSize;
    ULONG ReturnedValues;
    ULONG LangId;
    ULONG LangIdCount;
    ULONG ElevationFlags;
    ULONG AcesBufferSize;
    ULONG Value;
    BOOLEAN Packaged;
    NTSTATUS Status;
    PWCHAR Match;

    Match = RtlFindUnicodeSubstring(&String, &ExactSubstring, FALSE);
    ok_eq_pointer(Match, &String.Buffer[5]);
    Match = RtlFindUnicodeSubstring(&String, &LowerSubstring, FALSE);
    ok_eq_pointer(Match, NULL);
    Match = RtlFindUnicodeSubstring(&String, &LowerSubstring, TRUE);
    ok_eq_pointer(Match, &String.Buffer[5]);

    Status = RtlGenerateClass5Guid(&DnsNamespace, (PVOID)GuidName, sizeof(GuidName) - 1, &Guid);
    trace("RtlGenerateClass5Guid returned 0x%08lx\n", Status);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(RtlEqualMemory(&Guid, &ExpectedGuid, sizeof(Guid)), "unexpected class-5 GUID\n");

    Version.dwOSVersionInfoSize = sizeof(Version);
    Status = RtlGetVersion(&Version);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Value = 0;
    Status = RtlGetSystemGlobalData(7, &Value, sizeof(Value));
    trace("RtlGetSystemGlobalData(major) returned 0x%08lx, value %lu\n", Status, Value);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Value, Version.dwMajorVersion);
    Value = 0;
    Status = RtlGetSystemGlobalData(8, &Value, sizeof(Value));
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Value, Version.dwMinorVersion);

    RtlFillMemory(&TimeValues, sizeof(TimeValues), 0xA5);
    ReturnedValues = 0;
    Status = RtlGetMultiTimePrecise(&TimeValues, 7, &ReturnedValues);
    trace("RtlGetMultiTimePrecise returned 0x%08lx, mask 0x%lx, qpc %I64u, host %I64u, system %I64u\n", Status, ReturnedValues, TimeValues.PerformanceCounter, TimeValues.HostPerformanceCounter, TimeValues.SystemTime);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(ReturnedValues, 5);
    ok(TimeValues.PerformanceCounter != 0xA5A5A5A5A5A5A5A5ULL, "performance counter was not written\n");
    ok_eq_ulonglong(TimeValues.HostPerformanceCounter, 0xA5A5A5A5A5A5A5A5ULL);
    ok(TimeValues.SystemTime > 116444736000000000ULL, "invalid system time %I64u\n", TimeValues.SystemTime);

    ConvertedCounter = 0xA5A5A5A5A5A5A5A5ULL;
    Status = RtlConvertHostPerfCounterToPerfCounter(TimeValues.HostPerformanceCounter, 0, &ConvertedCounter);
    trace("RtlConvertHostPerfCounterToPerfCounter returned 0x%08lx, value %I64u\n", Status, ConvertedCounter);
    ok_eq_hex(Status, STATUS_UNSUCCESSFUL);
    ok_eq_ulonglong(ConvertedCounter, 0xA5A5A5A5A5A5A5A5ULL);

    LangId = MAXULONG;
    LangIdCount = MAXULONG;
    Status = RtlGetThreadLangIdByIndex(0, 0, &LangId, &LangIdCount);
    trace("RtlGetThreadLangIdByIndex returned 0x%08lx, lang 0x%lx, count %lu\n", Status, LangId, LangIdCount);
    ok_eq_hex(Status, STATUS_NOT_FOUND);
    ok_eq_ulong(LangId, 0);
    ok_eq_ulong(LangIdCount, 0);

    trace("RtlIsStateSeparationEnabled returned %u\n", RtlIsStateSeparationEnabled());
    ok_eq_bool(RtlIsStateSeparationEnabled(), SharedUserData->DbgStateSeparationEnabled != 0);
    ElevationFlags = MAXULONG;
    Status = RtlQueryElevationFlags(&ElevationFlags);
    trace("RtlQueryElevationFlags returned 0x%08lx, flags 0x%lx, shared flags 0x%lx\n", Status, ElevationFlags, SharedUserData->SharedDataFlags);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(ElevationFlags, ((SharedUserData->SharedDataFlags >> 1) & 7) | 8);

    Status = RtlCreateAcl(&Acl, sizeof(Acl), ACL_REVISION);
    ok_eq_hex(Status, STATUS_SUCCESS);
    AcesBufferSize = MAXULONG;
    Status = RtlGetAcesBufferSize(&Acl, &AcesBufferSize);
    trace("RtlGetAcesBufferSize returned 0x%08lx, size %lu\n", Status, AcesBufferSize);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(AcesBufferSize, 0);

    Token = PsReferencePrimaryToken(PsGetCurrentProcess());
    PackageSize = (SIZE_T)-1;
    AppIdSize = (SIZE_T)-1;
    Packaged = TRUE;
    Status = RtlQueryPackageIdentity(Token, NULL, &PackageSize, NULL, &AppIdSize, &Packaged);
    PsDereferencePrimaryToken(Token);
    trace("RtlQueryPackageIdentity returned 0x%08lx, package %Iu, app %Iu, packaged %u\n", Status, PackageSize, AppIdSize, Packaged);
    ok_eq_hex(Status, STATUS_NOT_FOUND);
    ok_eq_size(PackageSize, (SIZE_T)-1);
    ok_eq_size(AppIdSize, (SIZE_T)-1);
    ok_eq_bool(Packaged, TRUE);
}

typedef struct _TEST_RUN_ONCE_CONTEXT
{
    LONG Calls;
    PVOID Result;
} TEST_RUN_ONCE_CONTEXT, *PTEST_RUN_ONCE_CONTEXT;

static
ULONG
NTAPI
TestRunOnceCallback(
    _Inout_ PRTL_RUN_ONCE RunOnce,
    _Inout_opt_ PVOID Parameter,
    _Inout_opt_ PVOID *Context)
{
    PTEST_RUN_ONCE_CONTEXT TestContext = Parameter;

    UNREFERENCED_PARAMETER(RunOnce);

    InterlockedIncrement(&TestContext->Calls);
    *Context = TestContext->Result;
    trace("RtlRunOnce callback %ld returned context %p\n", TestContext->Calls, *Context);
    return TRUE;
}

static
VOID
TestModernKernelExports(VOID)
{
    SID_IDENTIFIER_AUTHORITY Authority = SECURITY_NT_AUTHORITY;
    UCHAR SidBuffer[SECURITY_MAX_SID_SIZE];
    TEST_RUN_ONCE_CONTEXT RunContext;
    RTL_RUN_ONCE RunOnce = RTL_RUN_ONCE_INIT;
    ULONGLONG ForegroundProcessId;
    ULONGLONG TestForegroundProcessId;
    ULONG ActiveConsoleId;
    ULONG TestActiveConsoleId;
    ULONG ProductType;
    ULONG ServiceSessionId;
    PVOID Context;
    BOOLEAN Member;
    BOOLEAN Result;
    NTSTATUS Status;

    trace("RTL shared state: suite=0x%lx console=%lu foreground=%I64u multisession=%u root=%S\n",
          RtlGetSuiteMask(), RtlGetActiveConsoleId(), RtlGetConsoleSessionForegroundProcessId(),
          RtlIsMultiSessionSku(), RtlGetNtSystemRoot());
    ok_eq_ulong(RtlGetSuiteMask(), SharedUserData->SuiteMask);
    ok_eq_ulong(RtlGetActiveConsoleId(), SharedUserData->ActiveConsoleId);
    ok_eq_ulonglong(RtlGetConsoleSessionForegroundProcessId(), (ULONGLONG)SharedUserData->ConsoleSessionForegroundProcessId);
    ok_eq_bool(RtlIsMultiSessionSku(), SharedUserData->DbgMultiSessionSku != 0);
    ok(RtlGetNtSystemRoot() != NULL, "RtlGetNtSystemRoot returned NULL\n");
    if (RtlGetNtSystemRoot() != NULL)
        ok_eq_wstr(RtlGetNtSystemRoot(), SharedUserData->NtSystemRoot);

    ServiceSessionId = RtlGetCurrentServiceSessionId();
    trace("RtlGetCurrentServiceSessionId returned %lu\n", ServiceSessionId);
    ok_eq_ulong(ServiceSessionId, 0);

    ActiveConsoleId = RtlGetActiveConsoleId();
    TestActiveConsoleId = ActiveConsoleId ^ 0x5A5A5A5A;
    RtlSetActiveConsoleId(TestActiveConsoleId);
    ok_eq_ulong(RtlGetActiveConsoleId(), TestActiveConsoleId);
    RtlSetActiveConsoleId(ActiveConsoleId);
    ok_eq_ulong(RtlGetActiveConsoleId(), ActiveConsoleId);

    ForegroundProcessId = RtlGetConsoleSessionForegroundProcessId();
    TestForegroundProcessId = ForegroundProcessId ^ 0x5A5A5A5AA5A5A5A5ULL;
    RtlSetConsoleSessionForegroundProcessId(TestForegroundProcessId);
    ok_eq_ulonglong(RtlGetConsoleSessionForegroundProcessId(), TestForegroundProcessId);
    RtlSetConsoleSessionForegroundProcessId(ForegroundProcessId);
    ok_eq_ulonglong(RtlGetConsoleSessionForegroundProcessId(), ForegroundProcessId);

    RtlZeroMemory(SidBuffer, sizeof(SidBuffer));
    Status = RtlInitializeSidEx((PSID)SidBuffer, &Authority, 3, SECURITY_NT_NON_UNIQUE, 42UL, 84UL);
    trace("RtlInitializeSidEx returned 0x%08lx, sid length %lu\n", Status,
          NT_SUCCESS(Status) ? RtlLengthSid((PSID)SidBuffer) : 0);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(RtlValidSid((PSID)SidBuffer), "RtlInitializeSidEx produced an invalid SID\n");
    ok_eq_ulong(*RtlSubAuthoritySid((PSID)SidBuffer, 0), SECURITY_NT_NON_UNIQUE);
    ok_eq_ulong(*RtlSubAuthoritySid((PSID)SidBuffer, 1), 42);
    ok_eq_ulong(*RtlSubAuthoritySid((PSID)SidBuffer, 2), 84);
    Status = RtlInitializeSidEx((PSID)SidBuffer, &Authority, SID_MAX_SUB_AUTHORITIES + 1);
    trace("RtlInitializeSidEx excessive-count status 0x%08lx\n", Status);
    ok_eq_hex(Status, STATUS_INVALID_SID);

    Member = FALSE;
    Status = RtlCheckTokenMembership(NULL, SeExports->SeWorldSid, &Member);
    trace("RtlCheckTokenMembership(world) returned 0x%08lx, member %u\n", Status, Member);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_bool(Member, TRUE);

    RtlZeroMemory(SidBuffer, sizeof(SidBuffer));
    Result = RtlIsZeroMemory(SidBuffer, sizeof(SidBuffer));
    trace("RtlIsZeroMemory(zero) returned %u\n", Result);
    ok_eq_bool(Result, TRUE);
    SidBuffer[RTL_NUMBER_OF(SidBuffer) / 2] = 1;
    ok_eq_bool(RtlIsZeroMemory(SidBuffer, sizeof(SidBuffer)), FALSE);
    ok_eq_bool(RtlIsZeroMemory(SidBuffer, 0), TRUE);

    RunContext.Calls = 0;
    RunContext.Result = (PVOID)(ULONG_PTR)0x1000;
    Context = NULL;
    Status = RtlRunOnceExecuteOnce(&RunOnce, TestRunOnceCallback, &RunContext, &Context);
    trace("first RtlRunOnceExecuteOnce returned 0x%08lx, calls %ld, context %p\n", Status, RunContext.Calls, Context);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_long(RunContext.Calls, 1);
    ok_eq_pointer(Context, RunContext.Result);
    Context = NULL;
    Status = RtlRunOnceExecuteOnce(&RunOnce, TestRunOnceCallback, &RunContext, &Context);
    trace("second RtlRunOnceExecuteOnce returned 0x%08lx, calls %ld, context %p\n", Status, RunContext.Calls, Context);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_long(RunContext.Calls, 1);
    ok_eq_pointer(Context, RunContext.Result);

    ProductType = PRODUCT_UNDEFINED;
    Result = RtlGetProductInfo(SharedUserData->NtMajorVersion, SharedUserData->NtMinorVersion, 0, 0, &ProductType);
    trace("RtlGetProductInfo returned %u, product 0x%lx\n", Result, ProductType);
    ok_eq_bool(Result, TRUE);
    ok(ProductType != PRODUCT_UNDEFINED, "RtlGetProductInfo returned PRODUCT_UNDEFINED\n");
}

START_TEST(ExWddmAvl)
{
    TestModernAvlState();
    TestModernAvlChurn();
}

START_TEST(ExWddmRtl)
{
    TestModernBitmaps();
    TestIntegerAtoms();
    TestModernRtlState();
    TestModernKernelExports();
}
