/*
 * COPYRIGHT:        See COPYING in the top level directory
 * PROJECT:          ReactOS Win32k subsystem
 * PURPOSE:          Window class menu names
 * FILE:             win32ss/user/ntuser/classmenu.c
 * PROGRAMER:        Thomas Weidenmueller <w3seek@reactos.com>
 */

#include <win32k.h>

DBG_DEFAULT_CHANNEL(UserClass);

VOID
IntFreeClassMenuName(IN OUT PCLS Class)
{
    /* Free the menu name, if it was changed and allocated */
    if (Class->lpszClientUnicodeMenuName != NULL && Class->MenuNameIsString)
    {
        UserHeapFree(Class->lpszClientUnicodeMenuName);
        Class->lpszClientUnicodeMenuName = NULL;
        Class->lpszClientAnsiMenuName = NULL;
    }
}

BOOL
IntSetClassMenuName(IN PCLS Class,
                    IN PUNICODE_STRING MenuName)
{
    BOOL Ret = FALSE;

    /* Change the base class first */
    Class = Class->pclsBase;

    if (MenuName->Length != 0)
    {
        ANSI_STRING AnsiString;
        PWSTR strBufW;

        AnsiString.MaximumLength = (USHORT)RtlUnicodeStringToAnsiSize(MenuName);

        strBufW = UserHeapAlloc(MenuName->Length + sizeof(UNICODE_NULL) +
                                AnsiString.MaximumLength);
        if (strBufW != NULL)
        {
            _SEH2_TRY
            {
                NTSTATUS Status;

                /* Copy the unicode string */
                RtlCopyMemory(strBufW,
                              MenuName->Buffer,
                              MenuName->Length);
                strBufW[MenuName->Length / sizeof(WCHAR)] = UNICODE_NULL;

                /* Create an ANSI copy of the string */
                AnsiString.Buffer = (PSTR)(strBufW + (MenuName->Length / sizeof(WCHAR)) + 1);
                Status = RtlUnicodeStringToAnsiString(&AnsiString,
                                                      MenuName,
                                                      FALSE);
                if (!NT_SUCCESS(Status))
                {
                    SetLastNtError(Status);
                    _SEH2_LEAVE;
                }

                Ret = TRUE;
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                SetLastNtError(_SEH2_GetExceptionCode());
            }
            _SEH2_END;

            if (Ret)
            {
                /* Update the base class */
                IntFreeClassMenuName(Class);
                Class->lpszClientUnicodeMenuName = strBufW;
                Class->lpszClientAnsiMenuName = AnsiString.Buffer;
                Class->MenuNameIsString = TRUE;

                /* Update the clones */
                Class = Class->pclsClone;
                while (Class != NULL)
                {
                    Class->lpszClientUnicodeMenuName = strBufW;
                    Class->lpszClientAnsiMenuName = AnsiString.Buffer;
                    Class->MenuNameIsString = TRUE;

                    Class = Class->pclsNext;
                }
            }
            else
            {
                ERR("Failed to copy class menu name!\n");
                UserHeapFree(strBufW);
            }
        }
        else
            EngSetLastError(ERROR_NOT_ENOUGH_MEMORY);
    }
    else
    {
        ASSERT(IS_INTRESOURCE(MenuName->Buffer));

        /* Update the base class */
        IntFreeClassMenuName(Class);
        Class->lpszClientUnicodeMenuName = MenuName->Buffer;
        Class->lpszClientAnsiMenuName = (PSTR)MenuName->Buffer;
        Class->MenuNameIsString = FALSE;

        /* Update the clones */
        Class = Class->pclsClone;
        while (Class != NULL)
        {
            Class->lpszClientUnicodeMenuName = MenuName->Buffer;
            Class->lpszClientAnsiMenuName = (PSTR)MenuName->Buffer;
            Class->MenuNameIsString = FALSE;

            Class = Class->pclsNext;
        }

        Ret = TRUE;
    }

    return Ret;
}
