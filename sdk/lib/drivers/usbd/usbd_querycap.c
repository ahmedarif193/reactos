#include <ntddk.h>
#include <usbdi.h>
#include <usbdlib.h>

_Must_inspect_result_
NTSTATUS
NTAPI
USBD_QueryUsbCapability(
    _In_ PVOID UsbdHandle,
    _In_ const GUID *CapabilityType,
    _In_ ULONG OutputBufferLength,
    _When_(OutputBufferLength == 0, _Pre_null_)
    _When_(OutputBufferLength != 0 && ResultLength == NULL, _Out_writes_bytes_(OutputBufferLength))
    _When_(OutputBufferLength != 0 && ResultLength != NULL, _Out_writes_bytes_to_opt_(OutputBufferLength, *ResultLength))
        PUCHAR OutputBuffer,
    _Out_opt_
    _When_(ResultLength != NULL, _Deref_out_range_(<=,OutputBufferLength))
        PULONG ResultLength)
{
    UNREFERENCED_PARAMETER(UsbdHandle);
    UNREFERENCED_PARAMETER(CapabilityType);
    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(OutputBuffer);

    if (ResultLength)
        *ResultLength = 0;

    /*
     * The current ReactOS USB stack does not expose Win8+ capabilities
     * (static streams, chained MDLs, etc.) via USBD yet.  Report this
     * explicitly so callers can gracefully fall back, matching Windows
     * behavior when a capability is not implemented on a given stack.
     */
    return STATUS_NOT_SUPPORTED;
}

