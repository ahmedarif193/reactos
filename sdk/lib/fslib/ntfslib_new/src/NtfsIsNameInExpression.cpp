#include <ntfslib_new.h>
#include <ntfslib_new_internal.h>

static WCHAR
UpcaseCharacter(_In_ WCHAR Character,
                _In_opt_ PWCHAR UpcaseTable)
{
    return UpcaseTable ? UpcaseTable[Character] : RtlUpcaseUnicodeChar(Character);
}

BOOLEAN
NtfsIsNameInExpressionFallback(_In_     PUNICODE_STRING Expression,
                               _In_     PUNICODE_STRING Name,
                               _In_     BOOLEAN IgnoreCase,
                               _In_opt_ PWCHAR UpcaseTable)
{
    PUCHAR Current, Next, Swap;
    ULONG ExpressionLength, NameLength;
    WCHAR ExpressionCharacter, NameCharacter;
    BOOLEAN HasLaterDot, Result;

    if (!Expression || !Name ||
        (Expression->Length % sizeof(WCHAR)) != 0 ||
        (Name->Length % sizeof(WCHAR)) != 0 ||
        (Expression->Length && !Expression->Buffer) ||
        (Name->Length && !Name->Buffer))
    {
        return FALSE;
    }

    ExpressionLength = Expression->Length / sizeof(WCHAR);
    NameLength = Name->Length / sizeof(WCHAR);

    /* Each row represents all matches for one expression suffix. Keeping
     * only the current and next rows makes wildcard matching O(name length)
     * in memory rather than allocating a full expression-by-name matrix.
     */
    Current = new(PagedPool, TAG_NTFS) UCHAR[NameLength + 1];
    Next = new(PagedPool, TAG_NTFS) UCHAR[NameLength + 1];
    if (!Current || !Next)
    {
        delete[] Current;
        delete[] Next;
        return FALSE;
    }

    RtlZeroMemory(Next, NameLength + 1);
    Next[NameLength] = TRUE;

    for (LONG ExpressionIndex = ExpressionLength - 1;
         ExpressionIndex >= 0;
         ExpressionIndex--)
    {
        ExpressionCharacter = Expression->Buffer[ExpressionIndex];
        HasLaterDot = FALSE;

        for (LONG NameIndex = NameLength; NameIndex >= 0; NameIndex--)
        {
            switch (ExpressionCharacter)
            {
                case L'*':
                    Current[NameIndex] =
                        Next[NameIndex] ||
                        (NameIndex < (LONG)NameLength && Current[NameIndex + 1]);
                    break;

                case DOS_STAR:
                    /* DOS_STAR consumes characters through the final dot,
                     * but not that final dot itself.
                     */
                    Current[NameIndex] =
                        Next[NameIndex] ||
                        (NameIndex < (LONG)NameLength &&
                         (Name->Buffer[NameIndex] != L'.' || HasLaterDot) &&
                         Current[NameIndex + 1]);
                    break;

                case DOS_QM:
                    /* At a dot or the end, consecutive DOS_QM characters
                     * may match zero characters.
                     */
                    Current[NameIndex] =
                        (NameIndex == (LONG)NameLength ||
                         Name->Buffer[NameIndex] == L'.')
                        ? Next[NameIndex]
                        : Next[NameIndex + 1];
                    break;

                case DOS_DOT:
                    Current[NameIndex] =
                        (NameIndex == (LONG)NameLength)
                        ? Next[NameIndex]
                        : (Name->Buffer[NameIndex] == L'.' &&
                           Next[NameIndex + 1]);
                    break;

                case L'?':
                    Current[NameIndex] =
                        NameIndex < (LONG)NameLength &&
                        Next[NameIndex + 1];
                    break;

                default:
                    if (NameIndex == (LONG)NameLength)
                    {
                        Current[NameIndex] = FALSE;
                        break;
                    }

                    NameCharacter = Name->Buffer[NameIndex];
                    if (IgnoreCase)
                    {
                        ExpressionCharacter =
                            UpcaseCharacter(ExpressionCharacter, UpcaseTable);
                        NameCharacter =
                            UpcaseCharacter(NameCharacter, UpcaseTable);
                    }

                    Current[NameIndex] =
                        ExpressionCharacter == NameCharacter &&
                        Next[NameIndex + 1];
                    break;
            }

            if (NameIndex < (LONG)NameLength &&
                Name->Buffer[NameIndex] == L'.')
            {
                HasLaterDot = TRUE;
            }
        }

        Swap = Next;
        Next = Current;
        Current = Swap;
    }

    Result = Next[0];
    delete[] Current;
    delete[] Next;
    return Result;
}
