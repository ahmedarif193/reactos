#pragma once

#if DBG && defined(_M_AMD64)

#define NTUSER_SMPTRACE_COM1_THR ((PUCHAR)(ULONG_PTR)0x3F8)
#define NTUSER_SMPTRACE_COM1_LSR ((PUCHAR)(ULONG_PTR)0x3FD)

FORCEINLINE UCHAR
NtUserSmpTraceHex(IN UCHAR Value)
{
    Value &= 0xF;
    return (Value < 10) ? (UCHAR)('0' + Value) : (UCHAR)('A' + (Value - 10));
}

FORCEINLINE VOID
NtUserSmpTracePut(IN UCHAR Byte)
{
    ULONG Spins = 0x1000;

    while (!(READ_PORT_UCHAR(NTUSER_SMPTRACE_COM1_LSR) & 0x20) && --Spins)
        YieldProcessor();

    WRITE_PORT_UCHAR(NTUSER_SMPTRACE_COM1_THR, Byte);
}

/*
 * Emit a compact raw serial marker as "@<tag><cpu><phase-hi><phase-lo>".
 * This avoids the normal DPRINT formatting path while keeping the markers
 * searchable in the serial log.
 */
FORCEINLINE VOID
NtUserSmpTraceEvent(IN UCHAR Tag,
                    IN UCHAR Phase)
{
    UCHAR Cpu = (UCHAR)(KeGetCurrentProcessorNumber() & 0xF);

    NtUserSmpTracePut('@');
    NtUserSmpTracePut(Tag);
    NtUserSmpTracePut(NtUserSmpTraceHex(Cpu));
    NtUserSmpTracePut(NtUserSmpTraceHex(Phase >> 4));
    NtUserSmpTracePut(NtUserSmpTraceHex(Phase));
}

FORCEINLINE VOID
NtUserSmpTraceStatus(IN UCHAR Tag,
                     IN NTSTATUS Status)
{
    NtUserSmpTraceEvent(Tag, (UCHAR)Status);
}

#else

#define NtUserSmpTraceEvent(Tag, Phase) ((void)0)
#define NtUserSmpTraceStatus(Tag, Status) ((void)0)

#endif
