/*++

Copyright (c) 1989-2000 Microsoft Corporation

Module Name:

    Create.c

Abstract:

    This module implements the File Create routine for Cdfs called by the
    Fsd/Fsp dispatch routines.

    HARDENED AND OPTIMIZED VERSION

--*/

#include "cdprocs.h"

//
//  The Bug check file id for this module
//

#define BugCheckFileId                   (CDFS_BUG_CHECK_CREATE)

//
//  Local support routines
//

_When_(RelatedTypeOfOpen != UnopenedFileObject, _At_(RelatedCcb, _In_))
_When_(RelatedTypeOfOpen == UnopenedFileObject, _At_(RelatedCcb, _In_opt_))
_When_(RelatedTypeOfOpen != UnopenedFileObject, _At_(RelatedFileName, _In_))
_When_(RelatedTypeOfOpen == UnopenedFileObject, _At_(RelatedFileName, _In_opt_))
NTSTATUS
CdNormalizeFileNames (
    _Inout_ PIRP_CONTEXT IrpContext,
    _In_ PVCB Vcb,
    _In_ BOOLEAN OpenByFileId,
    _In_ BOOLEAN IgnoreCase,
    _In_ TYPE_OF_OPEN RelatedTypeOfOpen,
    _In_opt_ PCCB RelatedCcb,
    _In_opt_ PUNICODE_STRING RelatedFileName,
    _Inout_ PUNICODE_STRING FileName,
    _Inout_ PCD_NAME RemainingName
    );

_Requires_lock_held_(_Global_critical_region_)
_Acquires_exclusive_lock_((*CurrentFcb)->FcbNonpaged->FcbResource)
NTSTATUS
CdOpenByFileId (
    _In_ PIRP_CONTEXT IrpContext,
    _In_ PIO_STACK_LOCATION IrpSp,
    _In_ PVCB Vcb,
    _Inout_ PFCB *CurrentFcb
    );

_Requires_lock_held_(_Global_critical_region_)
NTSTATUS
CdOpenExistingFcb (
    _In_ PIRP_CONTEXT IrpContext,
    _In_ PIO_STACK_LOCATION IrpSp,
    _Inout_ PFCB *CurrentFcb,
    _In_ TYPE_OF_OPEN TypeOfOpen,
    _In_ BOOLEAN IgnoreCase,
    _In_opt_ PCCB RelatedCcb
    );

_Requires_lock_held_(_Global_critical_region_)
_Acquires_lock_((*CurrentFcb)->FcbNonpaged->FcbResource)
NTSTATUS
CdOpenDirectoryFromPathEntry (
    _In_ PIRP_CONTEXT IrpContext,
    _In_ PIO_STACK_LOCATION IrpSp,
    _In_ PVCB Vcb,
    _Inout_ PFCB *CurrentFcb,
    _In_ PCD_NAME DirName,
    _In_ BOOLEAN IgnoreCase,
    _In_ BOOLEAN ShortNameMatch,
    _In_ PPATH_ENTRY PathEntry,
    _In_ BOOLEAN PerformUserOpen,
    _In_opt_ PCCB RelatedCcb
    );

_Requires_lock_held_(_Global_critical_region_)
NTSTATUS
CdOpenFileFromFileContext (
    _In_ PIRP_CONTEXT IrpContext,
    _In_ PIO_STACK_LOCATION IrpSp,
    _In_ PVCB Vcb,
    _Inout_ PFCB *CurrentFcb,
    _In_ PCD_NAME FileName,
    _In_ BOOLEAN IgnoreCase,
    _In_ BOOLEAN ShortNameMatch,
    _In_ PFILE_ENUM_CONTEXT FileContext,
    _In_opt_ PCCB RelatedCcb
    );

_Requires_lock_held_(_Global_critical_region_)
NTSTATUS
CdCompleteFcbOpen (
    _In_ PIRP_CONTEXT IrpContext,
    _In_ PIO_STACK_LOCATION IrpSp,
    _In_ PVCB Vcb,
    _Inout_ PFCB *CurrentFcb,
    _In_ TYPE_OF_OPEN TypeOfOpen,
    _In_ ULONG UserCcbFlags,
    _In_ ACCESS_MASK DesiredAccess
    );

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, CdCommonCreate)
#pragma alloc_text(PAGE, CdCompleteFcbOpen)
#pragma alloc_text(PAGE, CdNormalizeFileNames)
#pragma alloc_text(PAGE, CdOpenByFileId)
#pragma alloc_text(PAGE, CdOpenDirectoryFromPathEntry)
#pragma alloc_text(PAGE, CdOpenExistingFcb)
#pragma alloc_text(PAGE, CdOpenFileFromFileContext)
#endif


_Requires_lock_held_(_Global_critical_region_)
NTSTATUS
#ifdef _MSC_VER
#pragma prefast(suppress:26165, "Esp:1153")
#endif
CdCommonCreate (
    _Inout_ PIRP_CONTEXT IrpContext,
    _Inout_ PIRP Irp
    )

/*++

Routine Description:

    This is the common routine for opening a file called by both the
    Fsp and Fsd threads.

Arguments:

    IrpContext - Context for the request.
    Irp - Supplies the Irp to process

Return Value:

    NTSTATUS - This is the status from this open operation.

--*/

{
    NTSTATUS Status = STATUS_SUCCESS;
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation( Irp );

    PFILE_OBJECT FileObject;

    COMPOUND_PATH_ENTRY CompoundPathEntry = {{0}};
    BOOLEAN CleanupCompoundPathEntry = FALSE;

    FILE_ENUM_CONTEXT FileContext = {0};
    BOOLEAN CleanupFileContext = FALSE;
    BOOLEAN FoundEntry;

    PVCB Vcb;

    BOOLEAN OpenByFileId;
    BOOLEAN IgnoreCase;
    ULONG CreateDisposition;

    BOOLEAN ShortNameMatch;
    ULONG ShortNameDirentOffset;

    BOOLEAN VolumeOpen = FALSE;

    TYPE_OF_OPEN RelatedTypeOfOpen = UnopenedFileObject;
    PFILE_OBJECT RelatedFileObject;
    PCCB RelatedCcb = NULL;

    PFCB NextFcb;
    PFCB CurrentFcb = NULL;

    PUNICODE_STRING FileName;
    PUNICODE_STRING RelatedFileName = NULL;

    CD_NAME RemainingName = {{0}};
    CD_NAME FinalName;
    PCD_NAME MatchingName = NULL;

    PAGED_CODE();

    //
    //  HARDENING: Validate input pointers
    //
    if (IrpContext == NULL || Irp == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    //
    //  If we were called with our file system device object instead of a
    //  volume device object, just complete this request with STATUS_SUCCESS.
    //

    if (IrpContext->Vcb == NULL) {

        CdCompleteRequest( IrpContext, Irp, STATUS_SUCCESS );
        return STATUS_SUCCESS;
    }

    //
    //  Get create parameters from the Irp.
    //

    OpenByFileId = BooleanFlagOn( IrpSp->Parameters.Create.Options, FILE_OPEN_BY_FILE_ID );
    IgnoreCase = !BooleanFlagOn( IrpSp->Flags, SL_CASE_SENSITIVE );
    CreateDisposition = (IrpSp->Parameters.Create.Options >> 24) & 0x000000ff;

    //
    //  Do some preliminary checks to make sure the operation is supported.
    //

    if (FlagOn( IrpSp->Flags, SL_OPEN_PAGING_FILE | SL_OPEN_TARGET_DIRECTORY) ||
        (IrpSp->Parameters.Create.EaLength != 0) ||
        (CreateDisposition == FILE_CREATE)) {

        CdCompleteRequest( IrpContext, Irp, STATUS_ACCESS_DENIED );
        return STATUS_ACCESS_DENIED;
    }

#if (NTDDI_VERSION >= NTDDI_WIN7)
    //
    //  CDFS does not support FILE_OPEN_REQUIRING_OPLOCK
    //

    if (FlagOn( IrpSp->Parameters.Create.Options, FILE_OPEN_REQUIRING_OPLOCK )) {

        CdCompleteRequest( IrpContext, Irp, STATUS_INVALID_PARAMETER );
        return STATUS_INVALID_PARAMETER;
    }
#endif

    //
    //  Copy the Vcb to a local.  Assume the starting directory is the root.
    //

    Vcb = IrpContext->Vcb;
    NextFcb = Vcb->RootIndexFcb;

    //
    //  Reference our input parameters to make things easier
    //

    FileObject = IrpSp->FileObject;
    RelatedFileObject = NULL;

    // HARDENING: Check FileObject validity
    if (FileObject == NULL) {
        CdCompleteRequest( IrpContext, Irp, STATUS_INVALID_PARAMETER );
        return STATUS_INVALID_PARAMETER;
    }

    FileName = &FileObject->FileName;

    //
    //  Set up the file object's Vpb pointer in case anything happens.
    //

    if ((FileObject->RelatedFileObject != NULL) && !OpenByFileId) {

        RelatedFileObject = FileObject->RelatedFileObject;
        FileObject->Vpb = RelatedFileObject->Vpb;

        RelatedTypeOfOpen = CdDecodeFileObject( IrpContext, RelatedFileObject, &NextFcb, &RelatedCcb );

        //
        //  Fail the request if this is not a user file object.
        //

        if (RelatedTypeOfOpen < UserVolumeOpen) {

            CdCompleteRequest( IrpContext, Irp, STATUS_INVALID_PARAMETER );
            return STATUS_INVALID_PARAMETER;
        }

        //
        //  Remember the name in the related file object.
        //

        RelatedFileName = &RelatedFileObject->FileName;
    }

    //
    //  Normalize and Validate File Names
    //

    Status = CdNormalizeFileNames( IrpContext,
                                   Vcb,
                                   OpenByFileId,
                                   IgnoreCase,
                                   RelatedTypeOfOpen,
                                   RelatedCcb,
                                   RelatedFileName,
                                   FileName,
                                   &RemainingName );

    if (!NT_SUCCESS( Status )) {

        CdCompleteRequest( IrpContext, Irp, Status );
        return Status;
    }

    //
    //  We want to acquire the Vcb.  Exclusively for a volume open, shared otherwise.
    //

    if ((FileName->Length == 0) &&
        (RelatedTypeOfOpen <= UserVolumeOpen) &&
        !OpenByFileId) {

        VolumeOpen = TRUE;
        CdAcquireVcbExclusive( IrpContext, Vcb, FALSE );

    } else {

        CdAcquireVcbShared( IrpContext, Vcb, FALSE );
    }

    //
    //  Use a try-finally to facilitate cleanup.
    //

    _SEH2_TRY {

        //
        //  Verify that the Vcb is not in an unusable condition.
        //

        CdVerifyVcb( IrpContext, Vcb );

        //
        //  If the Vcb is locked then we cannot open another file
        //

        if (FlagOn( Vcb->VcbState, VCB_STATE_LOCKED )) {

            try_return( Status = STATUS_ACCESS_DENIED );
        }

        //
        //  If we are opening this file by FileId then process this immediately
        //

        if (OpenByFileId) {

            if (FlagOn( Vcb->VcbState, VCB_STATE_AUDIO_DISK )) {

                try_return( Status = STATUS_INVALID_DEVICE_REQUEST );
            }

            if ((CreateDisposition != FILE_OPEN) &&
                (CreateDisposition != FILE_OPEN_IF)) {

                try_return( Status = STATUS_ACCESS_DENIED );
            }

            if (!FlagOn( IrpContext->Flags, IRP_CONTEXT_FLAG_WAIT )) {

                CdRaiseStatus( IrpContext, STATUS_CANT_WAIT );
            }

            try_return( Status = CdOpenByFileId( IrpContext,
                                                 IrpSp,
                                                 Vcb,
                                                 &CurrentFcb ));
        }

        //
        //  If we are opening this volume Dasd then process this immediately
        //

        if (VolumeOpen) {

            if ((CreateDisposition != FILE_OPEN) &&
                (CreateDisposition != FILE_OPEN_IF)) {

                try_return( Status = STATUS_ACCESS_DENIED );
            }

            if (FlagOn( IrpSp->Parameters.Create.Options, FILE_DIRECTORY_FILE )) {

                try_return( Status = STATUS_NOT_A_DIRECTORY );
            }

            CurrentFcb = Vcb->VolumeDasdFcb;
            CdAcquireFcbExclusive( IrpContext, CurrentFcb, FALSE );

            try_return( Status = CdOpenExistingFcb( IrpContext,
                                                    IrpSp,
                                                    &CurrentFcb,
                                                    UserVolumeOpen,
                                                    FALSE,
                                                    NULL ));
        }

        //
        //  Walk down the directory tree
        //

        CdAcquireFcbExclusive( IrpContext, NextFcb, FALSE );
        CurrentFcb = NextFcb;

        if (RemainingName.FileName.Length != 0) {

            CdFindPrefix( IrpContext,
                          &CurrentFcb,
                          &RemainingName.FileName,
                          IgnoreCase );
        }

        if (RemainingName.FileName.Length == 0) {

            //
            //  Verify Open target type
            //

            if (SafeNodeType( CurrentFcb ) == CDFS_NTC_FCB_DATA) {

                if (FlagOn( IrpContext->Flags, IRP_CONTEXT_FLAG_TRAIL_BACKSLASH ) ||
                    FlagOn( IrpSp->Parameters.Create.Options, FILE_DIRECTORY_FILE )) {

                    try_return( Status = STATUS_NOT_A_DIRECTORY );
                }

                if ((CreateDisposition != FILE_OPEN) &&
                    (CreateDisposition != FILE_OPEN_IF)) {

                    try_return( Status = STATUS_ACCESS_DENIED );
                }

                try_return( Status = CdOpenExistingFcb( IrpContext,
                                                        IrpSp,
                                                        &CurrentFcb,
                                                        UserFileOpen,
                                                        IgnoreCase,
                                                        RelatedCcb ));

            } else if (FlagOn( IrpSp->Parameters.Create.Options, FILE_NON_DIRECTORY_FILE )) {

                try_return( Status = STATUS_FILE_IS_A_DIRECTORY );

            } else {

                if ((CreateDisposition != FILE_OPEN) &&
                    (CreateDisposition != FILE_OPEN_IF)) {

                    try_return( Status = STATUS_ACCESS_DENIED );
                }

                try_return( Status = CdOpenExistingFcb( IrpContext,
                                                        IrpSp,
                                                        &CurrentFcb,
                                                        UserDirectoryOpen,
                                                        IgnoreCase,
                                                        RelatedCcb ));
            }
        }

        //
        //  Parsing Loop
        //

        if (!FlagOn( CurrentFcb->FileAttributes, FILE_ATTRIBUTE_DIRECTORY )) {

            try_return( Status = STATUS_OBJECT_PATH_NOT_FOUND );
        }

        if (!FlagOn( IrpContext->Flags, IRP_CONTEXT_FLAG_WAIT )) {

            CdRaiseStatus( IrpContext, STATUS_CANT_WAIT );
        }

        FinalName.VersionString.Length = 0;

        while (TRUE) {

            ShortNameMatch = FALSE;

            CdDissectName( IrpContext,
                           &RemainingName.FileName,
                           &FinalName.FileName );

            CdInitializeCompoundPathEntry( IrpContext, &CompoundPathEntry );
            CleanupCompoundPathEntry = TRUE;

            FoundEntry = CdFindPathEntry( IrpContext,
                                          CurrentFcb,
                                          &FinalName,
                                          IgnoreCase,
                                          &CompoundPathEntry );

            if (!FoundEntry) {

                ShortNameDirentOffset = CdShortNameDirentOffset( IrpContext, &FinalName.FileName );

                if (ShortNameDirentOffset != MAXULONG) {

                    if (CleanupFileContext) {
                        CdCleanupFileContext( IrpContext, &FileContext );
                    }

                    CdInitializeFileContext( IrpContext, &FileContext );
                    CleanupFileContext = TRUE;

                    FoundEntry = CdFindFileByShortName( IrpContext,
                                                        CurrentFcb,
                                                        &FinalName,
                                                        IgnoreCase,
                                                        ShortNameDirentOffset,
                                                        &FileContext );

                    if (FoundEntry) {

                        ShortNameMatch = TRUE;

                        if (FlagOn( FileContext.InitialDirent->Dirent.DirentFlags,
                                    CD_ATTRIBUTE_DIRECTORY )) {

                            CdCleanupCompoundPathEntry( IrpContext, &CompoundPathEntry );
                            CdInitializeCompoundPathEntry( IrpContext, &CompoundPathEntry );

                            FoundEntry = CdFindPathEntry( IrpContext,
                                                          CurrentFcb,
                                                          &FileContext.InitialDirent->Dirent.CdCaseFileName,
                                                          IgnoreCase,
                                                          &CompoundPathEntry );

                            if (!FoundEntry) {
                                CdRaiseStatus( IrpContext, STATUS_FILE_CORRUPT_ERROR );
                            }

                            if (IgnoreCase) {
                                CdUpcaseName( IrpContext, &FinalName, &FinalName );
                            }

                        } else if (RemainingName.FileName.Length == 0) {

                            MatchingName = &FileContext.ShortName;
                            break;

                        } else {
                            try_return( Status = STATUS_OBJECT_PATH_NOT_FOUND );
                        }
                    }
                }

                if (!FoundEntry) {
                    if (RemainingName.FileName.Length == 0) {
                        break;
                    } else {
                        try_return( Status = STATUS_OBJECT_PATH_NOT_FOUND );
                    }
                }
            }

            if (IgnoreCase && !ShortNameMatch) {

                RtlCopyMemory( FinalName.FileName.Buffer,
                               CompoundPathEntry.PathEntry.CdDirName.FileName.Buffer,
                               CompoundPathEntry.PathEntry.CdDirName.FileName.Length );
            }

            if (RemainingName.FileName.Length == 0) {

                if (FlagOn( IrpSp->Parameters.Create.Options, FILE_NON_DIRECTORY_FILE )) {

                    try_return( Status = STATUS_FILE_IS_A_DIRECTORY );
                }

                if ((CreateDisposition != FILE_OPEN) &&
                    (CreateDisposition != FILE_OPEN_IF)) {

                    try_return( Status = STATUS_ACCESS_DENIED );
                }

                try_return( Status = CdOpenDirectoryFromPathEntry( IrpContext,
                                                                   IrpSp,
                                                                   Vcb,
                                                                   &CurrentFcb,
                                                                   &FinalName,
                                                                   IgnoreCase,
                                                                   ShortNameMatch,
                                                                   &CompoundPathEntry.PathEntry,
                                                                   TRUE,
                                                                   RelatedCcb ));
            }

            CdOpenDirectoryFromPathEntry( IrpContext,
                                          IrpSp,
                                          Vcb,
                                          &CurrentFcb,
                                          &FinalName,
                                          IgnoreCase,
                                          ShortNameMatch,
                                          &CompoundPathEntry.PathEntry,
                                          FALSE,
                                          NULL );

            CdCleanupCompoundPathEntry( IrpContext, &CompoundPathEntry );
            CleanupCompoundPathEntry = FALSE;
        }

        //
        //  Scan current directory for matching file name if not already found.
        //

        if (!FoundEntry) {

            if (CleanupFileContext) {
                CdCleanupFileContext( IrpContext, &FileContext );
            }

            CdInitializeFileContext( IrpContext, &FileContext );
            CleanupFileContext = TRUE;

            CdConvertNameToCdName( IrpContext, &FinalName );

            FoundEntry = CdFindFile( IrpContext,
                                     CurrentFcb,
                                     &FinalName,
                                     IgnoreCase,
                                     &FileContext,
                                     &MatchingName );
        }

        if (!FoundEntry) {

            if ((CreateDisposition == FILE_OPEN) ||
                (CreateDisposition == FILE_OVERWRITE)) {

                try_return( Status = STATUS_OBJECT_NAME_NOT_FOUND );
            }

            try_return( Status = STATUS_ACCESS_DENIED );
        }

        if (FlagOn( FileContext.InitialDirent->Dirent.Flags, CD_ATTRIBUTE_DIRECTORY )) {
            CdRaiseStatus( IrpContext, STATUS_DISK_CORRUPT_ERROR );
        }

        if (FlagOn( IrpContext->Flags, IRP_CONTEXT_FLAG_TRAIL_BACKSLASH ) ||
            FlagOn( IrpSp->Parameters.Create.Options, FILE_DIRECTORY_FILE )) {

            try_return( Status = STATUS_NOT_A_DIRECTORY );
        }

        if ((CreateDisposition != FILE_OPEN) &&
            (CreateDisposition != FILE_OPEN_IF)) {

            try_return( Status = STATUS_ACCESS_DENIED );
        }

        if (IgnoreCase) {

            RtlCopyMemory( FinalName.FileName.Buffer,
                           MatchingName->FileName.Buffer,
                           MatchingName->FileName.Length );
        }

        try_return( Status = CdOpenFileFromFileContext( IrpContext,
                                                        IrpSp,
                                                        Vcb,
                                                        &CurrentFcb,
                                                        &FinalName,
                                                        IgnoreCase,
                                                        (BOOLEAN) (MatchingName == &FileContext.ShortName),
                                                        &FileContext,
                                                        RelatedCcb ));

    try_exit:  NOTHING;
    } _SEH2_FINALLY {

        if (CleanupCompoundPathEntry) {
            CdCleanupCompoundPathEntry( IrpContext, &CompoundPathEntry );
        }

        if (CleanupFileContext) {
            CdCleanupFileContext( IrpContext, &FileContext );
        }

        if (_SEH2_AbnormalTermination()) {

            if ((CurrentFcb != NULL) &&
                (CurrentFcb != Vcb->VolumeDasdFcb)) {

                BOOLEAN RemovedFcb;
                CdTeardownStructures( IrpContext, CurrentFcb, &RemovedFcb );

                if (RemovedFcb) {
                    CurrentFcb = NULL;
                }
            }

            IrpContext = NULL;
            Irp = NULL;

        } else if (Status == STATUS_PENDING) {

            IrpContext = NULL;
            Irp = NULL;
        }

        if (CurrentFcb != NULL) {
            _Analysis_assume_lock_held_(CurrentFcb->FcbNonpaged->FcbResource);
            CdReleaseFcb( IrpContext, CurrentFcb );
        }

        CdReleaseVcb( IrpContext, Vcb );

        CdCompleteRequest( IrpContext, Irp, Status );
    } _SEH2_END;

    return Status;
}


_When_(RelatedTypeOfOpen != UnopenedFileObject, _At_(RelatedCcb, _In_))
_When_(RelatedTypeOfOpen == UnopenedFileObject, _At_(RelatedCcb, _In_opt_))
_When_(RelatedTypeOfOpen != UnopenedFileObject, _At_(RelatedFileName, _In_))
_When_(RelatedTypeOfOpen == UnopenedFileObject, _At_(RelatedFileName, _In_opt_))
NTSTATUS
CdNormalizeFileNames (
    _Inout_ PIRP_CONTEXT IrpContext,
    _In_ PVCB Vcb,
    _In_ BOOLEAN OpenByFileId,
    _In_ BOOLEAN IgnoreCase,
    _In_ TYPE_OF_OPEN RelatedTypeOfOpen,
    _In_opt_ PCCB RelatedCcb,
    _In_opt_ PUNICODE_STRING RelatedFileName,
    _Inout_ PUNICODE_STRING FileName,
    _Inout_ PCD_NAME RemainingName
    )

/*++

Routine Description:

    This routine is called to store the full name and upcased name into the
    filename buffer.

    HARDENING NOTE: This routine now includes explicit integer overflow checks
    for buffer length calculations.

--*/

{
    ULONG RemainingNameLength = 0;
    ULONG RelatedNameLength = 0;
    ULONG SeparatorLength = 0;
    ULONG BufferLength;

    UNICODE_STRING NewFileName;

    PAGED_CODE();

    if (!FlagOn( IrpContext->Flags, IRP_CONTEXT_FLAG_FULL_NAME )) {

        if (!OpenByFileId) {

            // HARDENING: Check FileName buffer validity
            if (FileName->Length > 0 && FileName->Buffer == NULL) {
                 return STATUS_INVALID_PARAMETER;
            }

            if ((FileName->Length > sizeof( WCHAR )) &&
                (FileName->Buffer[1] == L'\\') &&
                (FileName->Buffer[0] == L'\\')) {

                if ((FileName->Length > 2 * sizeof( WCHAR )) &&
                    (FileName->Buffer[2] == L'\\')) {

                    return STATUS_OBJECT_NAME_INVALID;
                }

                FileName->Length -= sizeof( WCHAR );

                RtlMoveMemory( FileName->Buffer,
                               FileName->Buffer + 1,
                               FileName->Length );
            }

            if (((FileName->Length > sizeof( WCHAR)) ||
                 ((FileName->Length == sizeof( WCHAR )) && (RelatedTypeOfOpen == UserDirectoryOpen))) &&
                (FileName->Buffer[ (FileName->Length/2) - 1 ] == L'\\')) {

                SetFlag( IrpContext->Flags, IRP_CONTEXT_FLAG_TRAIL_BACKSLASH );
                FileName->Length -= sizeof( WCHAR );
            }

            RemainingNameLength = FileName->Length;

            if (RelatedTypeOfOpen != UnopenedFileObject) {

                if (FileName->Length != 0) {

                    if (RelatedTypeOfOpen <= UserVolumeOpen) {
                        return STATUS_INVALID_PARAMETER;
                    } else if (FileName->Buffer[0] == L'\\' ) {
                        return STATUS_INVALID_PARAMETER;
                    } else if (RelatedTypeOfOpen == UserFileOpen) {
                        return STATUS_OBJECT_PATH_NOT_FOUND;
                    }
                }

                if (RelatedCcb && !FlagOn( RelatedCcb->Flags, CCB_FLAG_OPEN_BY_ID )) {

                    if ((FileName->Length != 0) &&
                        (RelatedCcb->Fcb != Vcb->RootIndexFcb)) {
                        SeparatorLength = sizeof( WCHAR );
                    }

                    if (RelatedFileName) {
                        RelatedNameLength = RelatedFileName->Length;
                    }
                }

            } else if (FileName->Length != 0) {

                if (FileName->Buffer[0] != L'\\') {
                    return STATUS_INVALID_PARAMETER;
                }

                RemainingNameLength -= sizeof( WCHAR );
                SeparatorLength = sizeof( WCHAR );
            }

            //
            //  HARDENING: Safe Integer Arithmetic for Buffer Calculation
            //  Check for ULONG overflow before casting to USHORT
            //
            if ((ULONG)RelatedNameLength + SeparatorLength < RelatedNameLength ||
                (ULONG)RelatedNameLength + SeparatorLength + RemainingNameLength < RemainingNameLength) {
                return STATUS_INVALID_PARAMETER;
            }

            BufferLength = RelatedNameLength + SeparatorLength + RemainingNameLength;

            if (BufferLength > MAXUSHORT) {
                return STATUS_INVALID_PARAMETER;
            }

            if (FileName->MaximumLength < BufferLength) {

                NewFileName.Buffer = FsRtlAllocatePoolWithTag( CdPagedPool,
                                                               BufferLength,
                                                               TAG_FILE_NAME );
                // FsRtlAllocatePoolWithTag raises on failure, so we don't need NULL check here if using that API.
                
                NewFileName.MaximumLength = (USHORT) BufferLength;

            } else {

                NewFileName.Buffer = FileName->Buffer;
                NewFileName.MaximumLength = FileName->MaximumLength;
            }

            if (RelatedNameLength != 0) {

                if (RemainingNameLength != 0) {

                    RtlMoveMemory( Add2Ptr( NewFileName.Buffer, RelatedNameLength + SeparatorLength, PVOID ),
                                   FileName->Buffer,
                                   RemainingNameLength );
                }

                if (RelatedFileName && RelatedFileName->Buffer) {
                    RtlCopyMemory( NewFileName.Buffer,
                                   RelatedFileName->Buffer,
                                   RelatedNameLength );
                }

                if (SeparatorLength != 0) {
                    *(Add2Ptr( NewFileName.Buffer, RelatedNameLength, PWCHAR )) = L'\\';
                }

                if (NewFileName.Buffer != FileName->Buffer) {

                    if (FileName->Buffer != NULL) {
                        CdFreePool( &FileName->Buffer );
                    }

                    FileName->Buffer = NewFileName.Buffer;
                    FileName->MaximumLength = NewFileName.MaximumLength;
                }

                FileName->Length = (USHORT) BufferLength;
            }

            RemainingName->FileName.MaximumLength =
            RemainingName->FileName.Length = (USHORT) RemainingNameLength;
            RemainingName->VersionString.Length = 0;

            RemainingName->FileName.Buffer = Add2Ptr( FileName->Buffer,
                                                      RelatedNameLength + SeparatorLength,
                                                      PWCHAR );

            if (IgnoreCase && (RemainingNameLength != 0)) {

                CdUpcaseName( IrpContext,
                              RemainingName,
                              RemainingName );
            }

#ifdef _MSC_VER
#pragma prefast(push)
#pragma prefast(suppress:26000, "Buffer bounds verified via explicit length checks")
#endif
            if (FsRtlDoesNameContainWildCards( &RemainingName->FileName )) {
#ifdef _MSC_VER
#pragma prefast(pop)
#endif
                return STATUS_OBJECT_NAME_INVALID;
            }

        } else {

            //
            //  HARDENING: Strict check for FileId size
            //
            if (FileName->Length != sizeof( FILE_ID )) {
                return STATUS_INVALID_PARAMETER;
            }
        }

        SetFlag( IrpContext->Flags, IRP_CONTEXT_FLAG_FULL_NAME );

    } else {

        RemainingName->FileName = *FileName;
        RemainingName->VersionString.Length = 0;

        if (RemainingName->FileName.Length != 0) {

            if (RelatedTypeOfOpen != UnopenedFileObject) {

                if (RelatedCcb && !FlagOn( RelatedCcb->Flags, CCB_FLAG_OPEN_BY_ID )) {
                    
                    if (RelatedFileName) {
                        RemainingName->FileName.Buffer = Add2Ptr( RemainingName->FileName.Buffer,
                                                                  RelatedFileName->Length,
                                                                  PWCHAR );

                        RemainingName->FileName.Length -= RelatedFileName->Length;
                    }
                }
            }

            if (RemainingName->FileName.Length != 0) {

                if (*(RemainingName->FileName.Buffer) == L'\\') {

                    RemainingName->FileName.Buffer = Add2Ptr( RemainingName->FileName.Buffer,
                                                              sizeof( WCHAR ),
                                                              PWCHAR );

                    RemainingName->FileName.Length -= sizeof( WCHAR );
                }
            }
        }

        if (IgnoreCase && (RemainingName->FileName.Length != 0)) {

            CdUpcaseName( IrpContext,
                          RemainingName,
                          RemainingName );
        }
    }

    return STATUS_SUCCESS;
}


_Requires_lock_held_(_Global_critical_region_)
_Acquires_exclusive_lock_((*CurrentFcb)->FcbNonpaged->FcbResource)
NTSTATUS
CdOpenByFileId (
    _In_ PIRP_CONTEXT IrpContext,
    _In_ PIO_STACK_LOCATION IrpSp,
    _In_ PVCB Vcb,
    _Inout_ PFCB *CurrentFcb
    )

/*++

Routine Description:

    This routine is called to open a file by the FileId.

    HARDENING NOTE: Added explicit length checks for FileId buffer.

--*/

{
    NTSTATUS Status = STATUS_ACCESS_DENIED;
    BOOLEAN UnlockVcb = FALSE;
    BOOLEAN Found;
    ULONG StreamOffset;
    NODE_TYPE_CODE NodeTypeCode;
    TYPE_OF_OPEN TypeOfOpen;
    FILE_ENUM_CONTEXT FileContext;
    BOOLEAN CleanupFileContext = FALSE;
    COMPOUND_PATH_ENTRY CompoundPathEntry = {{0}};
    BOOLEAN CleanupCompoundPathEntry = FALSE;
    FILE_ID FileId;
    FILE_ID ParentFileId;
    PFCB NextFcb;

    PAGED_CODE();

    //
    //  HARDENING: Verify buffer size before access
    //
    if (IrpSp->FileObject->FileName.Length != sizeof(FILE_ID)) {
        return STATUS_INVALID_PARAMETER;
    }

    RtlCopyMemory( &FileId, IrpSp->FileObject->FileName.Buffer, sizeof( FILE_ID ));

    _SEH2_TRY {

        if (CdFidIsDirectory( FileId )) {

            TypeOfOpen = UserDirectoryOpen;
            NodeTypeCode = CDFS_NTC_FCB_INDEX;

            if (CdQueryFidDirentOffset( FileId ) != 0) {
                try_return( Status = STATUS_INVALID_PARAMETER );
            }

        } else {

            TypeOfOpen = UserFileOpen;
            NodeTypeCode = CDFS_NTC_FCB_DATA;
        }

        CdLockVcb( IrpContext, Vcb );
        UnlockVcb = TRUE;

        NextFcb = CdLookupFcbTable( IrpContext, Vcb, FileId );

        if (NextFcb == NULL) {

            StreamOffset = CdQueryFidPathTableOffset( FileId );

            CdSetFidDirentOffset( ParentFileId, 0 );
            CdSetFidPathTableOffset( ParentFileId, StreamOffset );
            CdFidSetDirectory( ParentFileId );

            NextFcb = CdLookupFcbTable( IrpContext, Vcb, ParentFileId );

            if (NextFcb == NULL) {

                CdUnlockVcb( IrpContext, Vcb );
                UnlockVcb = FALSE;

                // Hardening: Bound check
                if (StreamOffset > Vcb->PathTableFcb->FileSize.LowPart) {
                    try_return( Status = STATUS_INVALID_PARAMETER );
                }

                CdInitializeCompoundPathEntry( IrpContext, &CompoundPathEntry );
                CleanupCompoundPathEntry = TRUE;

                CdLookupPathEntry( IrpContext,
                                   Vcb->PathTableFcb->StreamOffset,
                                   1,
                                   TRUE,
                                   &CompoundPathEntry );

                while (TRUE) {

                    Found = CdLookupNextPathEntry( IrpContext,
                                                   &CompoundPathEntry.PathContext,
                                                   &CompoundPathEntry.PathEntry );

                    if (!Found ||
                        (CompoundPathEntry.PathEntry.PathTableOffset > StreamOffset)) {

                        try_return( Status = STATUS_INVALID_PARAMETER );
                    }
                }

                if ((TypeOfOpen == UserDirectoryOpen) &&
                    FlagOn( IrpSp->Parameters.Create.Options, FILE_NON_DIRECTORY_FILE )) {

                    try_return( Status = STATUS_FILE_IS_A_DIRECTORY );
                }

                CdLockVcb( IrpContext, Vcb );
                UnlockVcb = TRUE;

                NextFcb = CdCreateFcb( IrpContext, ParentFileId, NodeTypeCode, &Found );

                if (!Found) {

                    CdInitializeFcbFromPathEntry( IrpContext,
                                                  NextFcb,
                                                  NULL,
                                                  &CompoundPathEntry.PathEntry );
                }

                if (TypeOfOpen == UserDirectoryOpen) {

                    *CurrentFcb = NextFcb;
                    NextFcb = NULL;
                }
            }

            if (NextFcb != NULL) {

                if (!CdAcquireFcbExclusive( IrpContext, NextFcb, TRUE )) {

                    NextFcb->FcbReference += 1;
                    CdUnlockVcb( IrpContext, Vcb );

                    CdAcquireFcbExclusive( IrpContext, NextFcb, FALSE );

                    CdLockVcb( IrpContext, Vcb );
                    NextFcb->FcbReference -= 1;
                    CdUnlockVcb( IrpContext, Vcb );

                } else {
                    CdUnlockVcb( IrpContext, Vcb );
                }

                UnlockVcb = FALSE;
                *CurrentFcb = NextFcb;

                StreamOffset = CdQueryFidDirentOffset( FileId );
                CdVerifyOrCreateDirStreamFile( IrpContext, NextFcb);

                if (StreamOffset > NextFcb->FileSize.LowPart) {
                    try_return( Status = STATUS_INVALID_PARAMETER );
                }

                CdInitializeFileContext( IrpContext, &FileContext );
                CdLookupInitialFileDirent( IrpContext,
                                           NextFcb,
                                           &FileContext,
                                           NextFcb->StreamOffset );

                CleanupFileContext = TRUE;

                while (TRUE) {

                    Found = CdLookupNextInitialFileDirent( IrpContext,
                                                           NextFcb,
                                                           &FileContext );

                    if (!Found ||
                        (FileContext.InitialDirent->Dirent.DirentOffset > StreamOffset)) {

                        try_return( Status = STATUS_INVALID_PARAMETER );
                    }
                }

                if (FlagOn( FileContext.InitialDirent->Dirent.DirentFlags,
                            CD_ATTRIBUTE_DIRECTORY )) {

                    try_return( Status = STATUS_INVALID_PARAMETER );
                }

                if (FlagOn( IrpSp->Parameters.Create.Options, FILE_DIRECTORY_FILE )) {
                    try_return( Status = STATUS_NOT_A_DIRECTORY );
                }

                CdLookupLastFileDirent( IrpContext, NextFcb, &FileContext );

                CdLockVcb( IrpContext, Vcb );
                UnlockVcb = TRUE;

                NextFcb = CdCreateFcb( IrpContext, FileId, NodeTypeCode, &Found );

                if (!Found) {
                    CdInitializeFcbFromFileContext( IrpContext,
                                                    NextFcb,
                                                    *CurrentFcb,
                                                    &FileContext );
                }
            }

        } else {

            if (FlagOn( NextFcb->FileAttributes, FILE_ATTRIBUTE_DIRECTORY )) {

                if (FlagOn( IrpSp->Parameters.Create.Options, FILE_NON_DIRECTORY_FILE )) {
                    try_return( Status = STATUS_FILE_IS_A_DIRECTORY );
                }

            } else if (FlagOn( IrpSp->Parameters.Create.Options, FILE_DIRECTORY_FILE )) {
                try_return( Status = STATUS_NOT_A_DIRECTORY );
            }
        }

        if (*CurrentFcb != NULL) {
            CdReleaseFcb( IrpContext, *CurrentFcb );
        }

        if (!CdAcquireFcbExclusive( IrpContext, NextFcb, TRUE )) {

            NextFcb->FcbReference += 1;
            CdUnlockVcb( IrpContext, Vcb );

            CdAcquireFcbExclusive( IrpContext, NextFcb, FALSE );

            CdLockVcb( IrpContext, Vcb );
            NextFcb->FcbReference -= 1;
            CdUnlockVcb( IrpContext, Vcb );

        } else {
            CdUnlockVcb( IrpContext, Vcb );
        }

        UnlockVcb = FALSE;
        *CurrentFcb = NextFcb;

        _Analysis_suppress_lock_checking_(NextFcb->FcbNonpaged->FcbResource);

        if (!CdIllegalFcbAccess( IrpContext,
                                 TypeOfOpen,
                                 IrpSp->Parameters.Create.SecurityContext->DesiredAccess )) {

            Status = CdCompleteFcbOpen( IrpContext,
                                        IrpSp,
                                        Vcb,
                                        CurrentFcb,
                                        TypeOfOpen,
                                        CCB_FLAG_OPEN_BY_ID,
                                        IrpSp->Parameters.Create.SecurityContext->DesiredAccess );
        }

    try_exit:  NOTHING;
    } _SEH2_FINALLY {

        if (UnlockVcb) {
            CdUnlockVcb( IrpContext, Vcb );
        }

        if (CleanupFileContext) {
            CdCleanupFileContext( IrpContext, &FileContext );
        }

        if (CleanupCompoundPathEntry) {
            CdCleanupCompoundPathEntry( IrpContext, &CompoundPathEntry );
        }
    } _SEH2_END;

    return Status;
}


_Requires_lock_held_(_Global_critical_region_)
NTSTATUS
CdOpenExistingFcb (
    _In_ PIRP_CONTEXT IrpContext,
    _In_ PIO_STACK_LOCATION IrpSp,
    _Inout_ PFCB *CurrentFcb,
    _In_ TYPE_OF_OPEN TypeOfOpen,
    _In_ BOOLEAN IgnoreCase,
    _In_opt_ PCCB RelatedCcb
    )

/*++

Routine Description:

    This routine is called to open an Fcb which is already in the Fcb table.

--*/

{
    ULONG CcbFlags = 0;
    NTSTATUS Status = STATUS_ACCESS_DENIED;

    PAGED_CODE();

    if (!CdIllegalFcbAccess( IrpContext,
                             TypeOfOpen,
                             IrpSp->Parameters.Create.SecurityContext->DesiredAccess )) {

        if (IgnoreCase) {
            SetFlag( CcbFlags, CCB_FLAG_IGNORE_CASE );
        }

        if (ARGUMENT_PRESENT( RelatedCcb )) {
            SetFlag( CcbFlags, FlagOn( RelatedCcb->Flags, CCB_FLAG_OPEN_WITH_VERSION ));

            if (FlagOn( RelatedCcb->Flags, CCB_FLAG_OPEN_BY_ID | CCB_FLAG_OPEN_RELATIVE_BY_ID )) {
                SetFlag( CcbFlags, CCB_FLAG_OPEN_RELATIVE_BY_ID );
            }
        }

        Status = CdCompleteFcbOpen( IrpContext,
                                    IrpSp,
                                    (*CurrentFcb)->Vcb,
                                    CurrentFcb,
                                    TypeOfOpen,
                                    CcbFlags,
                                    IrpSp->Parameters.Create.SecurityContext->DesiredAccess );
    }

    return Status;
}


_Requires_lock_held_(_Global_critical_region_)
_Acquires_lock_((*CurrentFcb)->FcbNonpaged->FcbResource)
NTSTATUS
CdOpenDirectoryFromPathEntry (
    _In_ PIRP_CONTEXT IrpContext,
    _In_ PIO_STACK_LOCATION IrpSp,
    _In_ PVCB Vcb,
    _Inout_ PFCB *CurrentFcb,
    _In_ PCD_NAME DirName,
    _In_ BOOLEAN IgnoreCase,
    _In_ BOOLEAN ShortNameMatch,
    _In_ PPATH_ENTRY PathEntry,
    _In_ BOOLEAN PerformUserOpen,
    _In_opt_ PCCB RelatedCcb
    )

/*++

Routine Description:

    This routine is called to open a directory where the directory was found
    in the path table.

--*/

{
    ULONG CcbFlags = 0;
    FILE_ID FileId;

    BOOLEAN UnlockVcb = FALSE;
    BOOLEAN FcbExisted;

    PFCB NextFcb;
    PFCB ParentFcb = NULL;

    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    if (PerformUserOpen &&
        CdIllegalFcbAccess( IrpContext,
                            UserDirectoryOpen,
                            IrpSp->Parameters.Create.SecurityContext->DesiredAccess )) {

        return STATUS_ACCESS_DENIED;
    }

    _SEH2_TRY {

        if (ARGUMENT_PRESENT( RelatedCcb ) &&
            FlagOn( RelatedCcb->Flags, CCB_FLAG_OPEN_BY_ID | CCB_FLAG_OPEN_RELATIVE_BY_ID )) {

            CcbFlags = CCB_FLAG_OPEN_RELATIVE_BY_ID;
        }

        if (IgnoreCase) {
            SetFlag( CcbFlags, CCB_FLAG_IGNORE_CASE );
        }

        // Hardening: Explicit checks on PathEntry logic could be added here if struct defs were visible
        // Assuming PathTableOffset is valid based on previous lookups

        FileId.QuadPart = 0;
        CdSetFidPathTableOffset( FileId, PathEntry->PathTableOffset );
        CdFidSetDirectory( FileId );

        CdLockVcb( IrpContext, Vcb );
        UnlockVcb = TRUE;

        NextFcb = CdCreateFcb( IrpContext, FileId, CDFS_NTC_FCB_INDEX, &FcbExisted );

        if (!FcbExisted) {
            CdInitializeFcbFromPathEntry( IrpContext, NextFcb, *CurrentFcb, PathEntry );
        }

        if (!CdAcquireFcbExclusive( IrpContext, NextFcb, TRUE )) {

            NextFcb->FcbReference += 1;
            CdUnlockVcb( IrpContext, Vcb );

            CdReleaseFcb( IrpContext, *CurrentFcb );
            CdAcquireFcbExclusive( IrpContext, NextFcb, FALSE );
            CdAcquireFcbExclusive( IrpContext, *CurrentFcb, FALSE );

            CdLockVcb( IrpContext, Vcb );
            NextFcb->FcbReference -= 1;
            CdUnlockVcb( IrpContext, Vcb );

        } else {
            CdUnlockVcb( IrpContext, Vcb );
        }

        UnlockVcb = FALSE;
        ParentFcb = *CurrentFcb;
        *CurrentFcb = NextFcb;

        _Analysis_suppress_lock_checking_(NextFcb->FcbNonpaged->FcbResource);

        if (ShortNameMatch) {
            CdInsertPrefix( IrpContext, NextFcb, DirName, FALSE, TRUE, ParentFcb );
            if (IgnoreCase) {
                CdInsertPrefix( IrpContext, NextFcb, DirName, TRUE, TRUE, ParentFcb );
            }
        } else {
            CdInsertPrefix( IrpContext, NextFcb, &PathEntry->CdDirName, FALSE, FALSE, ParentFcb );
            if (IgnoreCase) {
                CdInsertPrefix( IrpContext, NextFcb, &PathEntry->CdCaseDirName, TRUE, FALSE, ParentFcb );
            }
        }

        CdReleaseFcb( IrpContext, ParentFcb );
        ParentFcb = NULL;

        if (PerformUserOpen) {

            Status = CdCompleteFcbOpen( IrpContext,
                                        IrpSp,
                                        Vcb,
                                        CurrentFcb,
                                        UserDirectoryOpen,
                                        CcbFlags,
                                        IrpSp->Parameters.Create.SecurityContext->DesiredAccess );
        }

    } _SEH2_FINALLY {

        if (UnlockVcb) {
            CdUnlockVcb( IrpContext, Vcb );
        }

        if (ParentFcb != NULL) {
            CdReleaseFcb( IrpContext, ParentFcb );
        }
    } _SEH2_END;

    return Status;
}


_Requires_lock_held_(_Global_critical_region_)
NTSTATUS
CdOpenFileFromFileContext (
    _In_ PIRP_CONTEXT IrpContext,
    _In_ PIO_STACK_LOCATION IrpSp,
    _In_ PVCB Vcb,
    _Inout_ PFCB *CurrentFcb,
    _In_ PCD_NAME FileName,
    _In_ BOOLEAN IgnoreCase,
    _In_ BOOLEAN ShortNameMatch,
    _In_ PFILE_ENUM_CONTEXT FileContext,
    _In_opt_ PCCB RelatedCcb
    )
{
    ULONG CcbFlags = 0;
    FILE_ID FileId;
    BOOLEAN UnlockVcb = FALSE;
    BOOLEAN FcbExisted;
    PFCB NextFcb;
    PFCB ParentFcb = NULL;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    if (CdIllegalFcbAccess( IrpContext,
                            UserFileOpen,
                            IrpSp->Parameters.Create.SecurityContext->DesiredAccess )) {
        return STATUS_ACCESS_DENIED;
    }

    _SEH2_TRY {

        if (FileName->VersionString.Length != 0) {
            SetFlag( CcbFlags, CCB_FLAG_OPEN_WITH_VERSION );
        }

        if (ARGUMENT_PRESENT( RelatedCcb ) &&
            FlagOn( RelatedCcb->Flags, CCB_FLAG_OPEN_BY_ID | CCB_FLAG_OPEN_RELATIVE_BY_ID )) {
            SetFlag( CcbFlags, CCB_FLAG_OPEN_RELATIVE_BY_ID );
        }

        if (IgnoreCase) {
            SetFlag( CcbFlags, CCB_FLAG_IGNORE_CASE );
        }

        CdSetFidPathTableOffset( FileId, CdQueryFidPathTableOffset( (*CurrentFcb)->FileId ));
        CdSetFidDirentOffset( FileId, FileContext->InitialDirent->Dirent.DirentOffset );

        CdLockVcb( IrpContext, Vcb );
        UnlockVcb = TRUE;

        NextFcb = CdCreateFcb( IrpContext, FileId, CDFS_NTC_FCB_DATA, &FcbExisted );

        if (!FcbExisted) {
            CdInitializeFcbFromFileContext( IrpContext,
                                            NextFcb,
                                            *CurrentFcb,
                                            FileContext );
        }

        if (!CdAcquireFcbExclusive( IrpContext, NextFcb, TRUE )) {

            NextFcb->FcbReference += 1;
            CdUnlockVcb( IrpContext, Vcb );

            CdReleaseFcb( IrpContext, *CurrentFcb );
            CdAcquireFcbExclusive( IrpContext, NextFcb, FALSE );
            CdAcquireFcbExclusive( IrpContext, *CurrentFcb, FALSE );

            CdLockVcb( IrpContext, Vcb );
            NextFcb->FcbReference -= 1;
            CdUnlockVcb( IrpContext, Vcb );

        } else {
            CdUnlockVcb( IrpContext, Vcb );
        }

        UnlockVcb = FALSE;
        ParentFcb = *CurrentFcb;
        *CurrentFcb = NextFcb;

        if (ShortNameMatch) {
            CdInsertPrefix( IrpContext, NextFcb, FileName, FALSE, TRUE, ParentFcb );
            if (IgnoreCase) {
                CdInsertPrefix( IrpContext, NextFcb, FileName, TRUE, TRUE, ParentFcb );
            }
        } else if (FileName->VersionString.Length == 0) {
            CdInsertPrefix( IrpContext, NextFcb, &FileContext->InitialDirent->Dirent.CdFileName, FALSE, FALSE, ParentFcb );
            if (IgnoreCase) {
                CdInsertPrefix( IrpContext, NextFcb, &FileContext->InitialDirent->Dirent.CdCaseFileName, TRUE, FALSE, ParentFcb );
            }
        }

        _Analysis_assume_same_lock_(ParentFcb->FcbNonpaged->FcbResource, NextFcb->FcbNonpaged->FcbResource);
        CdReleaseFcb( IrpContext, ParentFcb );
        ParentFcb = NULL;

        Status = CdCompleteFcbOpen( IrpContext,
                                    IrpSp,
                                    Vcb,
                                    CurrentFcb,
                                    UserFileOpen,
                                    CcbFlags,
                                    IrpSp->Parameters.Create.SecurityContext->DesiredAccess );

    } _SEH2_FINALLY {

        if (UnlockVcb) {
            CdUnlockVcb( IrpContext, Vcb );
        }

        if (ParentFcb != NULL) {
            CdReleaseFcb( IrpContext, ParentFcb );
        }
    } _SEH2_END;

    return Status;
}


_Requires_lock_held_(_Global_critical_region_)
NTSTATUS
CdCompleteFcbOpen (
    _In_ PIRP_CONTEXT IrpContext,
    _In_ PIO_STACK_LOCATION IrpSp,
    _In_ PVCB Vcb,
    _Inout_ PFCB *CurrentFcb,
    _In_ TYPE_OF_OPEN TypeOfOpen,
    _In_ ULONG UserCcbFlags,
    _In_ ACCESS_MASK DesiredAccess
    )
{
    NTSTATUS Status;
    NTSTATUS OplockStatus  = STATUS_SUCCESS;
    ULONG Information = FILE_OPENED;
    BOOLEAN LockVolume = FALSE;
    PFCB Fcb = *CurrentFcb;
    PCCB Ccb;

    PAGED_CODE();

    if (MAXIMUM_ALLOWED == DesiredAccess)  {
        DesiredAccess = FILE_ALL_ACCESS & ~((TypeOfOpen != UserVolumeOpen ?
                                             (FILE_WRITE_ATTRIBUTES | FILE_WRITE_DATA | FILE_WRITE_EA | FILE_ADD_FILE | FILE_ADD_SUBDIRECTORY | FILE_APPEND_DATA) : 0) |
                                            FILE_DELETE_CHILD | DELETE | WRITE_DAC );
    }

    if ((TypeOfOpen <= UserVolumeOpen) &&
        !FlagOn( IrpSp->Parameters.Create.ShareAccess, FILE_SHARE_READ )) {

        if (Vcb->VcbCleanup != 0) {
            return STATUS_SHARING_VIOLATION;
        }

        if (!FlagOn( IrpContext->Flags, IRP_CONTEXT_FLAG_WAIT )) {
            CdRaiseStatus( IrpContext, STATUS_CANT_WAIT );
        }

        LockVolume = TRUE;

        Status = CdPurgeVolume( IrpContext, Vcb, FALSE );

        if (Status != STATUS_SUCCESS) {
            return Status;
        }

        CdFspClose( Vcb );

        if (Vcb->VcbUserReference > CDFS_RESIDUAL_USER_REFERENCE) {
            return STATUS_SHARING_VIOLATION;
        }
    }

    if (Fcb->FcbCleanup != 0) {

        if (TypeOfOpen == UserFileOpen) {

            IrpContext->TeardownFcb = CurrentFcb;

            if (FsRtlCurrentBatchOplock( CdGetFcbOplock(Fcb) )) {

                Information = FILE_OPBATCH_BREAK_UNDERWAY;

                OplockStatus = FsRtlCheckOplock( CdGetFcbOplock(Fcb),
                                                 IrpContext->Irp,
                                                 IrpContext,
                                                 (PVOID)CdOplockComplete,
                                                 (PVOID)CdPrePostIrp );

                if (OplockStatus == STATUS_PENDING) {
                    return STATUS_PENDING;
                }
            }

            Status = IoCheckShareAccess( DesiredAccess,
                                         IrpSp->Parameters.Create.ShareAccess,
                                         IrpSp->FileObject,
                                         &Fcb->ShareAccess,
                                         FALSE );

            if (!NT_SUCCESS( Status )) {
                return Status;
            }

            OplockStatus = FsRtlCheckOplock( CdGetFcbOplock(Fcb),
                                             IrpContext->Irp,
                                             IrpContext,
                                             (PVOID)CdOplockComplete,
                                             (PVOID)CdPrePostIrp );

            if (OplockStatus == STATUS_PENDING) {
                return STATUS_PENDING;
            }

            IrpContext->TeardownFcb = NULL;

        } else {

            Status = IoCheckShareAccess( DesiredAccess,
                                         IrpSp->Parameters.Create.ShareAccess,
                                         IrpSp->FileObject,
                                         &Fcb->ShareAccess,
                                         FALSE );

            if (!NT_SUCCESS( Status )) {
                return Status;
            }
        }
    }

    Ccb = CdCreateCcb( IrpContext, Fcb, UserCcbFlags );

    if (Fcb->FcbCleanup == 0) {
        IoSetShareAccess( DesiredAccess,
                          IrpSp->Parameters.Create.ShareAccess,
                          IrpSp->FileObject,
                          &Fcb->ShareAccess );
    } else {
        IoUpdateShareAccess( IrpSp->FileObject, &Fcb->ShareAccess );
    }

    CdSetFileObject( IrpContext, IrpSp->FileObject, TypeOfOpen, Fcb, Ccb );

    if (TypeOfOpen == UserFileOpen) {
        if (FlagOn( IrpSp->Parameters.Create.Options, FILE_NO_INTERMEDIATE_BUFFERING )) {
            SetFlag( IrpSp->FileObject->Flags, FO_NO_INTERMEDIATE_BUFFERING );
        } else {
            SetFlag( IrpSp->FileObject->Flags, FO_CACHE_SUPPORTED );
        }
    } else if (TypeOfOpen == UserVolumeOpen)  {
        SetFlag( IrpSp->FileObject->Flags, FO_NO_INTERMEDIATE_BUFFERING );
    }

    CdLockVcb( IrpContext, Vcb );

    CdIncrementCleanupCounts( IrpContext, Fcb );
    CdIncrementReferenceCounts( IrpContext, Fcb, 1, 1 );

    if (LockVolume) {
        Vcb->VolumeLockFileObject = IrpSp->FileObject;
        SetFlag( Vcb->VcbState, VCB_STATE_LOCKED );
    }

    CdUnlockVcb( IrpContext, Vcb );

    CdLockFcb( IrpContext, Fcb );
    Fcb->IsFastIoPossible = (TypeOfOpen == UserFileOpen) ? CdIsFastIoPossible( Fcb ) : FastIoIsNotPossible;
    CdUnlockFcb( IrpContext, Fcb );

    IrpContext->Irp->IoStatus.Information = Information;
    IrpSp->FileObject->SectionObjectPointer = &Fcb->FcbNonpaged->SegmentObject;

    return OplockStatus;
}