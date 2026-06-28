/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     ARM64 data-export definitions for Windows export-table parity.
 *
 * Windows 11 ARM64 ntoskrnl.exe exports a number of data symbols (object-type
 * pointers, debugger flags, build identifiers) that ReactOS does not implement
 * yet. They are defined here as zero-initialised placeholders so the matching
 * "@ extern -arch=arm64 <name>" lines in ntoskrnl.spec resolve; a consumer sees
 * a NULL object type (and fails cleanly) rather than an unresolved import.
 *
 * Intentionally header-independent (raw C types only) so a definition here never
 * clashes with a future typed declaration and never needs the kernel PCH.
 */

#ifdef _M_ARM64

/*
 * Object-type pointers and pointer-sized data exports.
 *
 * CmKeyObjectType, ExTimerObjectType and IoCompletionObjectType are the public
 * export names for object types ReactOS already creates internally; they are
 * defined and wired to the live object in their owning subsystems (config/,
 * ex/, io/). The remaining symbols below back features ReactOS does not
 * implement yet and stay as zero placeholders.
 */
void *ExActivationObjectType = 0;
void *ExCompositionObjectType = 0;
void *ExCoreMessagingObjectType = 0;
void *ExRawInputManagerObjectType = 0;
void *PsPartitionType = 0;
void *PsSiloContextNonPagedType = 0;
void *PsSiloContextPagedType = 0;
void *TmEnlistmentObjectType = 0;
void *TmResourceManagerObjectType = 0;
void *TmTransactionManagerObjectType = 0;
void *TmTransactionObjectType = 0;
void *POGOBuffer = 0;
void *SeILSigningPolicyPtr = 0;
void *psMUITest = 0;

/*
 * MmBadPointer is a guaranteed-invalid address sentinel: the WDK MM_BAD_POINTER
 * macro reads this cell to obtain an address that always faults on access, so it
 * must NOT be NULL. A full implementation points it at a reserved no-access page;
 * until then use a non-canonical address, which faults on any dereference.
 */
void *MmBadPointer = (void *)~(unsigned long long)0;

/* Debugger port cells. */
void *KdComPortInUse = 0;
void *KdHvComPortInUse = 0;

/* BOOLEAN flags. */
unsigned char KdEventLoggingEnabled = 0;
unsigned char KeDynamicPartitioningSupported = 0;
unsigned char PsUILanguageComitted = 0;

/* Build GUID (16 bytes). */
unsigned char NtBuildGUID[16] = { 0 };

#endif /* _M_ARM64 */
