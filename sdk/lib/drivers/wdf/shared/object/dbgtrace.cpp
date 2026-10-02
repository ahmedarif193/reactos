/*++

Copyright (c) Microsoft Corporation

ModuleName:

    DbgTrace.cpp

Abstract:

    Temporary file to be used until ETW can be used
    for UM

Author:



Revision History:



--*/

#include "fxobjectpch.hpp"

#if FX_CORE_MODE==FX_CORE_USER_MODE
#include "strsafe.h"
#endif

#if !defined(EVENT_TRACING)

#if DBG && FX_CORE_MODE==FX_CORE_KERNEL_MODE
static
VOID
FxTranslateTraceFormat(
    __in PCSTR Source,
    __out_ecount(Size) PSTR Destination,
    __in size_t Size
    )
{
    PCSTR end, replacement;
    size_t used = 0, length;

    while (*Source != '\0' && used + 1 < Size) {
        end = (Source[0] == '%' && Source[1] == '!') ? strchr(Source + 2, '!') : NULL;
        if (end == NULL) {
            Destination[used++] = *Source++;
            continue;
        }

        length = end - (Source + 2);
        if (length == 4 && _strnicmp(Source + 2, "FUNC", 4) == 0) {
            replacement = "";
        }
        else if (length == 2 && _strnicmp(Source + 2, "wZ", 2) == 0) {
            replacement = "%wZ";
        }
        else if (length == 4 && _strnicmp(Source + 2, "GUID", 4) == 0) {
            replacement = "%p";
        }
        else {
            replacement = "0x%x";
        }

        length = strlen(replacement);
        if (used + length >= Size) {
            break;
        }

        RtlCopyMemory(Destination + used, replacement, length);
        used += length;
        Source = end + 1;
    }

    Destination[used] = '\0';
}
#endif

VOID
__cdecl
DoTraceLevelMessage(
    __in PVOID FxDriverGlobals,
    __in ULONG   DebugPrintLevel,
    __in ULONG   DebugPrintFlag,
    __drv_formatString(FormatMessage)
    __in PCSTR   DebugMessage,
    ...
    )

/*++

Routine Description:

    Print the trace message to debugger.

Arguments:

    TraceEventsLevel - print level between 0 and 3, with 3 the most verbose

Return Value:

    None.

 --*/
 {
#if DBG
    UNREFERENCED_PARAMETER(FxDriverGlobals);

#define     TEMP_BUFFER_SIZE        1024
    va_list    list;
    CHAR       debugMessageBuffer[TEMP_BUFFER_SIZE];
    NTSTATUS   status;
#if FX_CORE_MODE==FX_CORE_KERNEL_MODE
    CHAR       format[TEMP_BUFFER_SIZE];

    if (DebugPrintLevel > DebugLevel ||
        ((DebugPrintFlag & DebugFlag) != DebugPrintFlag)) {
        return;
    }
#endif

    va_start(list, DebugMessage);

    if (DebugMessage) {

        //
        // Using new safe string functions instead of _vsnprintf.
        // This function takes care of NULL terminating if the message
        // is longer than the buffer.
        //
#if FX_CORE_MODE==FX_CORE_KERNEL_MODE
        FxTranslateTraceFormat(DebugMessage, format, sizeof(format));
        status = RtlStringCbVPrintfA( debugMessageBuffer,
                                      sizeof(debugMessageBuffer),
                                      format,
                                      list );
#else
        HRESULT hr;
        hr = StringCbVPrintfA( debugMessageBuffer,
                                      sizeof(debugMessageBuffer),
                                      DebugMessage,
                                      list );


        if (HRESULT_FACILITY(hr) == FACILITY_WIN32)
        {
            status = WinErrorToNtStatus(HRESULT_CODE(hr));
        }
        else
        {
            status = SUCCEEDED(hr) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
        }
#endif
        if(!NT_SUCCESS(status)) {

#if FX_CORE_MODE==FX_CORE_KERNEL_MODE
            DbgPrint ("WDFTrace: RtlStringCbVPrintfA failed 0x%x\n", status);
#else
            OutputDebugString("WDFTrace: Unable to expand: ");
            OutputDebugString(DebugMessage);
#endif
            return;
        }
        if (DebugPrintLevel <= DebugLevel &&
            ((DebugPrintFlag & DebugFlag) == DebugPrintFlag)) {
#if FX_CORE_MODE==FX_CORE_KERNEL_MODE
            DbgPrint("WDFTrace: %s\n", debugMessageBuffer);
#else
            OutputDebugString("WDFTrace: ");
            OutputDebugString(DebugMessage);
#endif
        }
    }
    va_end(list);

    return;
#else
    UNREFERENCED_PARAMETER(FxDriverGlobals);
    UNREFERENCED_PARAMETER(DebugPrintLevel);
    UNREFERENCED_PARAMETER(DebugPrintFlag);
    UNREFERENCED_PARAMETER(DebugMessage);
#endif
}

#endif
