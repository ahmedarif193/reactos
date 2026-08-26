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

START_TEST(ExWddmAvl)
{
    TestModernAvlState();
    TestModernAvlChurn();
}

START_TEST(ExWddmRtl)
{
    TestModernRtlState();
}
