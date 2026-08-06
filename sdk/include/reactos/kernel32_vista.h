#ifndef _REACTOS_KERNEL32_VISTA_H_
#define _REACTOS_KERNEL32_VISTA_H_

#pragma once

/* Declarations exported by the ReactOS kernel32_vista compatibility module. */
#if (NTDDI_VERSION < NTDDI_WIN7SP1) && (defined(_AMD64_) || defined(_X86_))
WINBASEAPI DWORD64 WINAPI GetEnabledXStateFeatures(VOID);
#endif

#if (WINVER < _WIN32_WINNT_WIN7)
WINBASEAPI BOOL WINAPI GetLogicalProcessorInformationEx(_In_ LOGICAL_PROCESSOR_RELATIONSHIP RelationshipType, _Out_writes_bytes_to_opt_(*ReturnedLength, *ReturnedLength) PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX Buffer, _Inout_ PDWORD ReturnedLength);
#endif

#endif /* _REACTOS_KERNEL32_VISTA_H_ */
