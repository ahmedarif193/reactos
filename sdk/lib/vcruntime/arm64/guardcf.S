/**
 * This file has no copyright assigned and is placed in the Public Domain.
 * This file is part of the mingw-w64 runtime package.
 * No warranty is given; refer to the file DISCLAIMER.PD within this package.
 *
 * ARM64 Control Flow Guard runtime metadata for PE/COFF images.
 */

#define PTR .8byte
#define ALIGN 16
#define EXPORT_SYM(x) .globl x; x:
#define SYM(x) x

.text
.balign ALIGN

/*
 * Native Windows ARM64 leaves these targets image-local for a non-CFG process
 * and replaces their slots with shared loader targets for a CFG-enabled one.
 * Keep permissive local thunks for early startup.  Clang passes the dispatch
 * target in x16, unlike the x9 convention used by MSVC-built system images.
 */
SYM(__guard_check_icall_dummy):
  ret

SYM(__guard_dispatch_icall_dummy):
  br x16

.section .00cfg, "dr"
.balign ALIGN

EXPORT_SYM(__guard_check_icall_fptr)
  PTR SYM(__guard_check_icall_dummy)

EXPORT_SYM(__guard_dispatch_icall_fptr)
  PTR SYM(__guard_dispatch_icall_dummy)

/* IMAGE_LOAD_CONFIG_DIRECTORY64 as published by the Windows 11 SDK. */
.section .rdata, "dr"
.globl SYM(_load_config_used)
.balign ALIGN
SYM(_load_config_used):
  .4byte SYM(_load_config_used__end) - SYM(_load_config_used) /* Size */
  .4byte 0 /* TimeDateStamp */
  .2byte 0 /* MajorVersion */
  .2byte 0 /* MinorVersion */
  .4byte 0 /* GlobalFlagsClear */
  .4byte 0 /* GlobalFlagsSet */
  .4byte 0 /* CriticalSectionDefaultTimeout */
  PTR 0 /* DeCommitFreeBlockThreshold */
  PTR 0 /* DeCommitTotalFreeThreshold */
  PTR 0 /* LockPrefixTable */
  PTR 0 /* MaximumAllocationSize */
  PTR 0 /* VirtualMemoryThreshold */
  PTR 0 /* ProcessAffinityMask */
  .4byte 0 /* ProcessHeapFlags */
  .2byte 0 /* CSDVersion */
  .2byte 0 /* DependentLoadFlags */
  PTR 0 /* EditList */
  PTR 0 /* SecurityCookie */
  PTR 0 /* SEHandlerTable */
  PTR 0 /* SEHandlerCount */
  PTR SYM(__guard_check_icall_fptr) /* GuardCFCheckFunctionPointer */
  PTR SYM(__guard_dispatch_icall_fptr) /* GuardCFDispatchFunctionPointer */
  PTR SYM(__guard_fids_table) /* GuardCFFunctionTable */
  PTR SYM(__guard_fids_count) /* GuardCFFunctionCount */
  .4byte SYM(__guard_flags) /* GuardFlags */
  .2byte 0 /* CodeIntegrity.Flags */
  .2byte 0 /* CodeIntegrity.Catalog */
  .4byte 0 /* CodeIntegrity.CatalogOffset */
  .4byte 0 /* CodeIntegrity.Reserved */
  PTR SYM(__guard_iat_table) /* GuardAddressTakenIatEntryTable */
  PTR SYM(__guard_iat_count) /* GuardAddressTakenIatEntryCount */
  PTR SYM(__guard_longjmp_table) /* GuardLongJumpTargetTable */
  PTR SYM(__guard_longjmp_count) /* GuardLongJumpTargetCount */
  PTR 0 /* DynamicValueRelocTable */
  PTR 0 /* CHPEMetadataPointer */
  PTR 0 /* GuardRFFailureRoutine */
  PTR 0 /* GuardRFFailureRoutineFunctionPointer */
  .4byte 0 /* DynamicValueRelocTableOffset */
  .2byte 0 /* DynamicValueRelocTableSection */
  .2byte 0 /* Reserved2 */
  PTR 0 /* GuardRFVerifyStackPointerFunctionPointer */
  .4byte 0 /* HotPatchTableOffset */
  .4byte 0 /* Reserved3 */
  PTR 0 /* EnclaveConfigurationPointer */
  PTR 0 /* VolatileMetadataPointer */
  PTR SYM(__guard_eh_cont_table) /* GuardEHContinuationTable */
  PTR SYM(__guard_eh_cont_count) /* GuardEHContinuationCount */
  PTR 0 /* GuardXFGCheckFunctionPointer */
  PTR 0 /* GuardXFGDispatchFunctionPointer */
  PTR 0 /* GuardXFGTableDispatchFunctionPointer */
  PTR 0 /* CastGuardOsDeterminedFailureMode */
  PTR 0 /* GuardMemcpyFunctionPointer */

SYM(_load_config_used__end):
