/*
 * PROJECT:     ReactOS Printing Stack Marshalling Functions
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Marshalling functions
 * COPYRIGHT:   Copyright 2015-2018 Colin Finck (colin@reactos.org)
 */

#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>
#include <string.h>

#include <marshalling/marshalling.h>

#if defined(_WINSPOOL_WOW64_MARSHALLING) && !defined(_WIN64)
static DWORD
Wow64FieldSize(CHAR Kind, BOOL bWide, PDWORD pdwAlign)
{
    switch (Kind)
    {
        case 'P':
            *pdwAlign = bWide ? 8 : 4;
            return bWide ? 8 : 4;
        case 'Q':
            *pdwAlign = 8;
            return 8;
        case 'F':
            *pdwAlign = 4;
            return 8;
        case 'S':
            *pdwAlign = 2;
            return 16;
        case 'W':
            *pdwAlign = 2;
            return 2;
        default:
            *pdwAlign = 4;
            return 4;
    }
}

static DWORD
Wow64StructureSize(PCSTR pszLayout, BOOL bWide)
{
    DWORD dwOffset = 0, dwAlign, dwMaxAlign = 1, cbField;

    for (; *pszLayout; pszLayout++)
    {
        cbField = Wow64FieldSize(*pszLayout, bWide, &dwAlign);
        dwOffset = ((dwOffset + dwAlign - 1) & ~(dwAlign - 1)) + cbField;
        if (dwAlign > dwMaxAlign)
            dwMaxAlign = dwAlign;
    }

    return (dwOffset + dwMaxAlign - 1) & ~(dwMaxAlign - 1);
}

static BOOL
Wow64IsProcess(VOID)
{
    static LONG lState;
    BOOL bWow64 = FALSE;

    if (!lState)
    {
        if (!IsWow64Process(GetCurrentProcess(), &bWow64))
            bWow64 = FALSE;
        InterlockedExchange(&lState, bWow64 ? 2 : 1);
    }

    return lState == 2;
}

static BOOL
Wow64UnmarshallStructures(DWORD cbSize, PBYTE pBuffer, DWORD cElements, const MARSHALLING_INFO* pInfo, DWORD cbStructureSize)
{
    PCSTR pszLayout = CONTAINING_RECORD(pInfo, MARSHALLING, pInfo)->pszWow64Layout;
    DWORD cbElement64, i;
    BYTE Element[256];

    cbElement64 = Wow64StructureSize(pszLayout, TRUE);
    if (cbElement64 > sizeof(Element) || (cElements && cbElement64 > cbSize / cElements))
    {
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }

    for (i = 0; i < cElements; i++)
    {
        PBYTE pSource = pBuffer + i * cbElement64;
        PBYTE pTarget = pBuffer + i * cbStructureSize;
        DWORD cbAvailable = cbSize - i * cbElement64;
        DWORD dwOffset64 = 0, dwOffset32 = 0, dwAlign64, dwAlign32, cbField64, cbField32;
        PCSTR pKind;

        memcpy(Element, pSource, cbElement64);

        for (pKind = pszLayout; *pKind; pKind++)
        {
            cbField64 = Wow64FieldSize(*pKind, TRUE, &dwAlign64);
            cbField32 = Wow64FieldSize(*pKind, FALSE, &dwAlign32);
            dwOffset64 = (dwOffset64 + dwAlign64 - 1) & ~(dwAlign64 - 1);
            dwOffset32 = (dwOffset32 + dwAlign32 - 1) & ~(dwAlign32 - 1);

            if (*pKind == 'P')
            {
                ULONG64 ullRelative;
                ULONG_PTR ulPointer = 0;

                memcpy(&ullRelative, Element + dwOffset64, sizeof(ullRelative));
                if (ullRelative)
                {
                    if (ullRelative >= cbAvailable)
                    {
                        SetLastError(ERROR_INVALID_DATA);
                        return FALSE;
                    }

                    ulPointer = (ULONG_PTR)pSource + (ULONG_PTR)ullRelative;
                }

                memcpy(pTarget + dwOffset32, &ulPointer, sizeof(ulPointer));
            }
            else
            {
                memcpy(pTarget + dwOffset32, Element + dwOffset64, cbField32);
            }

            dwOffset64 += cbField64;
            dwOffset32 += cbField32;
        }
    }

    return TRUE;
}
#endif

/**
 * @name MarshallDownStructure
 *
 * Prepare a structure for marshalling/serialization by replacing absolute pointer addresses in its fields by relative offsets.
 *
 * @param pStructure
 * Pointer to the structure to operate on.
 *
 * @param pInfo
 * Array of MARSHALLING_INFO elements containing information about the fields of the structure as well as how to modify them.
 * See the documentation on MARSHALLING_INFO for more information.
 * You have to indicate the end of the array by setting the dwOffset field to MAXDWORD.
 *
 * @param cbStructureSize
 * Size in bytes of the structure.
 * This parameter is unused in my implementation.
 *
 * @param bSomeBoolean
 * Unknown boolean value, set to TRUE.
 *
 * @return
 * TRUE if the structure was successfully adjusted, FALSE otherwise.
 */
BOOL WINAPI
MarshallDownStructure(PVOID pStructure, const MARSHALLING_INFO* pInfo, DWORD cbStructureSize, BOOL bSomeBoolean)
{
    // Sanity checks
    if (!pStructure || !pInfo)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    // Loop until we reach an element with offset set to MAXDWORD.
    while (pInfo->dwOffset != MAXDWORD)
    {
        PULONG_PTR pCurrentField = (PULONG_PTR)((PBYTE)pStructure + pInfo->dwOffset);

        if (pInfo->bAdjustAddress && *pCurrentField)
        {
            // Make a relative offset out of the absolute pointer address.
            *pCurrentField -= (ULONG_PTR)pStructure;
        }

        // Advance to the next field description.
        pInfo++;
    }

    return TRUE;
}

/**
 * @name MarshallDownStructuresArray
 *
 * Prepare an array of structures for marshalling/serialization by replacing absolute pointer addresses in its fields by relative offsets.
 *
 * @param pStructuresArray
 * Pointer to the array of structures to operate on.
 *
 * @param cElements
 * Number of array elements.
 *
 * @param pInfo
 * Array of MARSHALLING_INFO elements containing information about the fields of the structure as well as how to modify them.
 * See the documentation on MARSHALLING_INFO for more information.
 * You have to indicate the end of the array by setting the dwOffset field to MAXDWORD.
 *
 * @param cbStructureSize
 * Size in bytes of each structure array element.
 *
 * @param bSomeBoolean
 * Unknown boolean value, set to TRUE.
 *
 * @return
 * TRUE if the array was successfully adjusted, FALSE otherwise.
 */
BOOL WINAPI
MarshallDownStructuresArray(PVOID pStructuresArray, DWORD cElements, const MARSHALLING_INFO* pInfo, DWORD cbStructureSize, BOOL bSomeBoolean)
{
    PBYTE pCurrentElement = pStructuresArray;

    // Call MarshallDownStructure on all array elements given by cElements of cbStructureSize.
    while (cElements--)
    {
        if (!MarshallDownStructure(pCurrentElement, pInfo, cbStructureSize, bSomeBoolean))
            return FALSE;

        // Advance to the next array element.
        pCurrentElement += cbStructureSize;
    }

    return TRUE;
}

/**
 * @name MarshallUpStructure
 *
 * Unmarshall/deserialize a structure previuosly marshalled by MarshallDownStructure by replacing relative offsets in its fields
 * by absolute pointer addresses again.
 *
 * @param cbSize
 * Size in bytes of the memory allocated for both the structure and its data.
 * The function will check if all relative offsets are within the bounds given by this size.
 *
 * @param pStructure
 * Pointer to the structure to operate on.
 *
 * @param pInfo
 * Array of MARSHALLING_INFO elements containing information about the fields of the structure as well as how to modify them.
 * See the documentation on MARSHALLING_INFO for more information.
 * You have to indicate the end of the array by setting the dwOffset field to MAXDWORD.
 *
 * @param cbStructureSize
 * Size in bytes of the structure.
 * This parameter is unused in my implementation.
 *
 * @param bSomeBoolean
 * Unknown boolean value, set to TRUE.
 *
 * @return
 * TRUE if the structure was successfully adjusted, FALSE otherwise.
 */
BOOL WINAPI
MarshallUpStructure(DWORD cbSize, PVOID pStructure, const MARSHALLING_INFO* pInfo, DWORD cbStructureSize, BOOL bSomeBoolean)
{
    // Sanity checks
    if (!pStructure || !pInfo)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

#if defined(_WINSPOOL_WOW64_MARSHALLING) && !defined(_WIN64)
    if (Wow64IsProcess())
        return Wow64UnmarshallStructures(cbSize, pStructure, 1, pInfo, cbStructureSize);
#endif

    // Loop until we reach an element with offset set to MAXDWORD.
    while (pInfo->dwOffset != MAXDWORD)
    {
        PULONG_PTR pCurrentField = (PULONG_PTR)((PBYTE)pStructure + pInfo->dwOffset);

        if (pInfo->bAdjustAddress && *pCurrentField)
        {
            // Verify that the offset in the current field is within the bounds given by cbSize.
            if (cbSize <= *pCurrentField)
            {
                SetLastError(ERROR_INVALID_DATA);
                return FALSE;
            }

            // Make an absolute pointer address out of the relative offset.
            *pCurrentField += (ULONG_PTR)pStructure;
        }

        // Advance to the next field description.
        pInfo++;
    }

    return TRUE;
}

/**
 * @name MarshallUpStructuresArray
 *
 * Unmarshall/deserialize an array of structures previuosly marshalled by MarshallDownStructuresArray by replacing relative offsets
 * in its fields by absolute pointer addresses again.
 *
 * @param cbSize
 * Size in bytes of the memory allocated for the entire structure array and its data.
 * The function will check if all relative offsets are within the bounds given by this size.
 *
 * @param pStructuresArray
 * Pointer to the array of structures to operate on.
 *
 * @param cElements
 * Number of array elements.
 *
 * @param pInfo
 * Array of MARSHALLING_INFO elements containing information about the fields of the structure as well as how to modify them.
 * See the documentation on MARSHALLING_INFO for more information.
 * You have to indicate the end of the array by setting the dwOffset field to MAXDWORD.
 *
 * @param cbStructureSize
 * Size in bytes of each structure array element.
 *
 * @param bSomeBoolean
 * Unknown boolean value, set to TRUE.
 *
 * @return
 * TRUE if the array was successfully adjusted, FALSE otherwise.
 */
BOOL WINAPI
MarshallUpStructuresArray(DWORD cbSize, PVOID pStructuresArray, DWORD cElements, const MARSHALLING_INFO* pInfo, DWORD cbStructureSize, BOOL bSomeBoolean)
{
    PBYTE pCurrentElement = pStructuresArray;

#if defined(_WINSPOOL_WOW64_MARSHALLING) && !defined(_WIN64)
    if (pStructuresArray && pInfo && Wow64IsProcess())
        return Wow64UnmarshallStructures(cbSize, pStructuresArray, cElements, pInfo, cbStructureSize);
#endif

    // Call MarshallUpStructure on all array elements given by cElements of cbStructureSize.
    while (cElements--)
    {
        if (!MarshallUpStructure(cbSize, pCurrentElement, pInfo, cbStructureSize, bSomeBoolean))
            return FALSE;

        // Advance to the next array element.
        pCurrentElement += cbStructureSize;
    }

    return TRUE;
}
