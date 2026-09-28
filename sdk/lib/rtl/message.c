/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS system libraries
 * FILE:            lib/rtl/message.c
 * PURPOSE:         Message table functions
 * PROGRAMMERS:     Eric Kohl
 */

/* INCLUDES *****************************************************************/

#include <rtl.h>

#define NDEBUG
#include <debug.h>

/* FUNCTIONS *****************************************************************/

/*
 * @implemented
 */
NTSTATUS
NTAPI
RtlFindMessage(
    IN PVOID BaseAddress,
    IN ULONG Type,
    IN ULONG Language,
    IN ULONG MessageId,
    OUT PMESSAGE_RESOURCE_ENTRY* MessageResourceEntry)
{
    LDR_RESOURCE_INFO ResourceInfo;
    PIMAGE_RESOURCE_DATA_ENTRY ResourceDataEntry;
    PMESSAGE_RESOURCE_DATA MessageTable;
    NTSTATUS Status;
    ULONG EntryOffset = 0, IdOffset = 0;
    PMESSAGE_RESOURCE_ENTRY MessageEntry;
    ULONG i;

    DPRINT("RtlFindMessage()\n");

    ResourceInfo.Type = Type;
    ResourceInfo.Name = 1;
    ResourceInfo.Language = Language;

    Status = LdrFindResource_U(BaseAddress,
                               &ResourceInfo,
                               RESOURCE_DATA_LEVEL,
                               &ResourceDataEntry);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    DPRINT("ResourceDataEntry: %p\n", ResourceDataEntry);

    Status = LdrAccessResource(BaseAddress,
                               ResourceDataEntry,
                               (PVOID*)&MessageTable,
                               NULL);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    DPRINT("MessageTable: %p\n", MessageTable);

    DPRINT("NumberOfBlocks %lu\n", MessageTable->NumberOfBlocks);
    for (i = 0; i < MessageTable->NumberOfBlocks; i++)
    {
        DPRINT("LoId 0x%08lx  HiId 0x%08lx  Offset 0x%08lx\n",
               MessageTable->Blocks[i].LowId,
               MessageTable->Blocks[i].HighId,
               MessageTable->Blocks[i].OffsetToEntries);
    }

    for (i = 0; i < MessageTable->NumberOfBlocks; i++)
    {
        if ((MessageId >= MessageTable->Blocks[i].LowId) &&
            (MessageId <= MessageTable->Blocks[i].HighId))
        {
            EntryOffset = MessageTable->Blocks[i].OffsetToEntries;
            IdOffset = MessageId - MessageTable->Blocks[i].LowId;
            break;
        }

        if (MessageId < MessageTable->Blocks[i].LowId)
        {
            return STATUS_MESSAGE_NOT_FOUND;
        }
    }

    if (MessageTable->NumberOfBlocks <= i)
    {
        return STATUS_MESSAGE_NOT_FOUND;
    }

    MessageEntry = (PMESSAGE_RESOURCE_ENTRY)
        ((PUCHAR)MessageTable + MessageTable->Blocks[i].OffsetToEntries);

    DPRINT("EntryOffset 0x%08lx\n", EntryOffset);
    DPRINT("IdOffset 0x%08lx\n", IdOffset);

    DPRINT("MessageEntry: %p\n", MessageEntry);
    for (i = 0; i < IdOffset; i++)
    {
        MessageEntry = (PMESSAGE_RESOURCE_ENTRY)
            ((PUCHAR)MessageEntry + (ULONG)MessageEntry->Length);
    }

    if (MessageEntry->Flags == 0)
    {
        DPRINT("AnsiText: %s\n", MessageEntry->Text);
    }
    else
    {
        DPRINT("UnicodeText: %S\n", (PWSTR)MessageEntry->Text);
    }

    if (MessageResourceEntry != NULL)
    {
        *MessageResourceEntry = MessageEntry;
    }

    return STATUS_SUCCESS;
}

typedef struct _RTLP_FORMAT_MESSAGE_ARGS
{
    INT Last;
    ULONG_PTR *Array;
    va_list *List;
    ULONG64 ArgList[102];
} RTLP_FORMAT_MESSAGE_ARGS, *PRTLP_FORMAT_MESSAGE_ARGS;

static
NTSTATUS
RtlpFormatAddChars(
    _Inout_ PWSTR *Buffer,
    _In_ PWSTR End,
    _In_ PCWSTR String,
    _In_ ULONG Length)
{
    if (Length > (ULONG)(End - *Buffer))
        return STATUS_BUFFER_OVERFLOW;
    RtlCopyMemory(*Buffer, String, Length * sizeof(WCHAR));
    *Buffer += Length;
    return STATUS_SUCCESS;
}

static
ULONG64
RtlpFormatGetArg(
    _In_ INT Number,
    _Inout_ PRTLP_FORMAT_MESSAGE_ARGS Args,
    _In_ BOOLEAN Is64)
{
    if (Number == -1)
        Number = Args->Last + 1;
    while (Number > Args->Last)
    {
        Args->ArgList[Args->Last++] = Is64 ? va_arg(*Args->List, ULONG64)
                                           : va_arg(*Args->List, ULONG_PTR);
    }
    return Args->ArgList[Number - 1];
}

static
NTSTATUS
RtlpFormatAddInsert(
    _Inout_ PWSTR *Buffer,
    _In_ PWSTR End,
    _Inout_ PCWSTR *Source,
    _In_ INT Insert,
    _In_ BOOLEAN Ansi,
    _Inout_ PRTLP_FORMAT_MESSAGE_ARGS Args)
{
    PCWSTR Format = *Source;
    WCHAR *p, Fmt[32];
    ULONG_PTR Values[5] = { 0 };
    BOOLEAN Is64 = FALSE;
    ULONG64 Value;
    INT Length, Stars = 0, Count = 0;

    p = Fmt;
    *p++ = L'%';

    if (*Format++ == L'!')
    {
        PCWSTR FormatEnd = wcschr(Format, L'!');

        if (!FormatEnd || FormatEnd - Format > (LONG_PTR)(RTL_NUMBER_OF(Fmt) - 2))
            return STATUS_INVALID_PARAMETER;
        *Source = FormatEnd + 1;

        while (*Format && wcschr(L"0123456789 +-*#.", *Format))
        {
            if (*Format == L'*') Stars++;
            *p++ = *Format++;
        }
        if (Stars > 2)
            return STATUS_INVALID_PARAMETER;

        switch (*Format)
        {
            case L'c': case L'C':
            case L's': case L'S':
                if (Ansi) *p++ = *Format++ ^ (L's' - L'S');
                break;
            case L'I':
                if (sizeof(PVOID) == sizeof(INT) && Format[1] == L'6' && Format[2] == L'4')
                    Is64 = TRUE;
                break;
        }
        while (Format != FormatEnd) *p++ = *Format++;
    }
    else
    {
        *p++ = Ansi ? L'S' : L's';
    }

    *p = 0;
    if (Args->List)
    {
        RtlpFormatGetArg(Insert - 1, Args, Is64);
        while (Stars--)
        {
            Values[Count++] = (ULONG_PTR)RtlpFormatGetArg(Insert, Args, FALSE);
            Insert = -1;
        }
        if (Insert == -1) Args->Last--;
        Value = RtlpFormatGetArg(Insert, Args, Is64);
        Values[Count++] = (ULONG_PTR)Value;
        Values[Count] = (ULONG_PTR)(Value >> 32);
    }
    else if (Args->Array)
    {
        Values[Count++] = Args->Array[Insert - 1];
        if (Args->Last < Insert) Args->Last = Insert;
        if (Is64) Count++;
        while (Stars--) Values[Count++] = Args->Array[Args->Last++];
    }
    else
    {
        return STATUS_INVALID_PARAMETER;
    }

    Length = _snwprintf_s(*Buffer, End - *Buffer, End - *Buffer - 1, Fmt,
                          Values[0], Values[1], Values[2], Values[3], Values[4]);
    if (Length == -1)
        return STATUS_BUFFER_OVERFLOW;
    *Buffer += Length;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
RtlFormatMessageEx(
    IN PWSTR Message,
    IN ULONG MaxWidth OPTIONAL,
    IN BOOLEAN IgnoreInserts,
    IN BOOLEAN ArgumentsAreAnsi,
    IN BOOLEAN ArgumentsAreAnArray,
    IN va_list* Arguments,
    OUT PWSTR Buffer,
    IN ULONG BufferSize,
    OUT PULONG ReturnLength OPTIONAL,
    IN ULONG Flags)
{
    static const WCHAR Space = L' ';
    static const WCHAR Cr = L'\r';
    static const WCHAR Tab = L'\t';
    static const WCHAR CrLf[] = { L'\r', L'\n' };
    RTLP_FORMAT_MESSAGE_ARGS Args;
    NTSTATUS Status = STATUS_SUCCESS;
    PCWSTR Source = Message;
    PWSTR Start = Buffer;
    PWSTR End = Buffer + BufferSize / sizeof(WCHAR);
    PWSTR Line = Buffer;
    PWSTR LastSpace = NULL;

    Args.Last = 0;
    Args.Array = ArgumentsAreAnArray ? (ULONG_PTR *)Arguments : NULL;
    Args.List = ArgumentsAreAnArray ? NULL : Arguments;

    for (; *Source; Source++)
    {
        switch (*Source)
        {
            case L'\r':
                if (Source[1] == L'\n') Source++;
            case L'\n':
                if (!MaxWidth)
                {
                    Status = RtlpFormatAddChars(&Buffer, End, CrLf, 2);
                    Line = Buffer;
                    LastSpace = NULL;
                    break;
                }
            case L' ':
                LastSpace = Buffer;
                Status = RtlpFormatAddChars(&Buffer, End, &Space, 1);
                break;
            case L'\t':
                if (LastSpace == Buffer - 1) LastSpace = Buffer;
                Status = RtlpFormatAddChars(&Buffer, End, &Tab, 1);
                break;
            case L'%':
                Source++;
                switch (*Source)
                {
                    case 0:
                        return STATUS_INVALID_PARAMETER;
                    case L't':
                        if (!MaxWidth)
                        {
                            Status = RtlpFormatAddChars(&Buffer, End, &Tab, 1);
                            break;
                        }
                    case L'n':
                        Status = RtlpFormatAddChars(&Buffer, End, CrLf, 2);
                        Line = Buffer;
                        LastSpace = NULL;
                        break;
                    case L'r':
                        Status = RtlpFormatAddChars(&Buffer, End, &Cr, 1);
                        Line = Buffer;
                        LastSpace = NULL;
                        break;
                    case L'0':
                        while (Source[1]) Source++;
                        break;
                    case L'1': case L'2': case L'3': case L'4': case L'5':
                    case L'6': case L'7': case L'8': case L'9':
                        if (!IgnoreInserts)
                        {
                            INT Number = *Source++ - L'0';

                            if (*Source >= L'0' && *Source <= L'9')
                                Number = Number * 10 + *Source++ - L'0';
                            Status = RtlpFormatAddInsert(&Buffer, End, &Source, Number,
                                                         ArgumentsAreAnsi, &Args);
                            Source--;
                            break;
                        }
                    default:
                        if (IgnoreInserts)
                            Status = RtlpFormatAddChars(&Buffer, End, Source - 1, 2);
                        else
                            Status = RtlpFormatAddChars(&Buffer, End, Source, 1);
                        break;
                }
                break;
            default:
                Status = RtlpFormatAddChars(&Buffer, End, Source, 1);
                break;
        }

        if (!NT_SUCCESS(Status))
            return Status;

        if (MaxWidth && (ULONG)(Buffer - Line) >= MaxWidth)
        {
            LONG_PTR Diff = 2;
            PWSTR Next;

            if (LastSpace)
            {
                Next = LastSpace + 1;
                while (LastSpace > Line && (LastSpace[-1] == L' ' || LastSpace[-1] == L'\t'))
                    LastSpace--;
                Diff -= Next - LastSpace;
            }
            else
            {
                LastSpace = Next = Buffer;
            }

            if (Diff > 0 && End - Buffer < Diff)
                return STATUS_BUFFER_OVERFLOW;
            RtlMoveMemory(LastSpace + 2, Next, (Buffer - Next) * sizeof(WCHAR));
            Buffer += Diff;
            RtlCopyMemory(LastSpace, CrLf, sizeof(CrLf));
            Line = LastSpace + 2;
            LastSpace = NULL;
        }
    }

    Status = RtlpFormatAddChars(&Buffer, End, L"", 1);
    if (!NT_SUCCESS(Status))
        return Status;

    if (ReturnLength)
        *ReturnLength = (ULONG)((Buffer - Start) * sizeof(WCHAR));
    return STATUS_SUCCESS;
}

/**********************************************************************
 *  RtlFormatMessage  (NTDLL.@)
 *
 * Formats a message (similar to sprintf).
 *
 * PARAMS
 *   Message             [I] Message to format.
 *   MaxWidth            [I] Maximum width in characters of each output line (optional).
 *   IgnoreInserts       [I] Whether to copy the message without processing inserts.
 *   ArgumentsAreAnsi    [I] Whether Arguments may have ANSI strings.
 *   ArgumentsAreAnArray [I] Whether Arguments is actually an array rather than a va_list *.
 *   Arguments           [I]
 *   Buffer              [O] Buffer to store processed message in.
 *   BufferSize          [I] Size of Buffer (in bytes).
 *   ReturnLength        [O] Size of the formatted message (in bytes; optional).
 *
 * RETURNS
 *      NTSTATUS code.
 *
 * @implemented
 */
NTSTATUS
NTAPI
RtlFormatMessage(
    IN PWSTR Message,
    IN ULONG MaxWidth OPTIONAL,
    IN BOOLEAN IgnoreInserts,
    IN BOOLEAN ArgumentsAreAnsi,
    IN BOOLEAN ArgumentsAreAnArray,
    IN va_list* Arguments,
    OUT PWSTR Buffer,
    IN ULONG BufferSize,
    OUT PULONG ReturnLength OPTIONAL)
{
    /* Call the extended API */
    return RtlFormatMessageEx(Message,
                              MaxWidth,
                              IgnoreInserts,
                              ArgumentsAreAnsi,
                              ArgumentsAreAnArray,
                              Arguments,
                              Buffer,
                              BufferSize,
                              ReturnLength,
                              0);
}

/* EOF */
