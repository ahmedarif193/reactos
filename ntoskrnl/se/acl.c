/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Access control lists (ACLs) implementation
 * COPYRIGHT:   Copyright David Welch <welch@cwcom.net>
 */

/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
#include <reactos/acltransform.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

PACL SePublicDefaultDacl = NULL;
PACL SeSystemDefaultDacl = NULL;
PACL SePublicDefaultUnrestrictedDacl = NULL;
PACL SePublicOpenDacl = NULL;
PACL SePublicOpenUnrestrictedDacl = NULL;
PACL SeUnrestrictedDacl = NULL;
PACL SeSystemAnonymousLogonDacl = NULL;

/* FUNCTIONS ******************************************************************/

/**
 * @brief
 * Initializes known discretionary access control lists in the system upon
 * kernel and Executive initialization procedure.
 *
 * @return
 * Returns TRUE if all the DACLs have been successfully initialized,
 * FALSE otherwise.
 */
CODE_SEG("INIT")
BOOLEAN
NTAPI
SepInitDACLs(VOID)
{
    ULONG AclLength;

    /* create PublicDefaultDacl */
    AclLength = sizeof(ACL) +
                (sizeof(ACE) + RtlLengthSid(SeWorldSid)) +
                (sizeof(ACE) + RtlLengthSid(SeLocalSystemSid)) +
                (sizeof(ACE) + RtlLengthSid(SeAliasAdminsSid));

    SePublicDefaultDacl = ExAllocatePoolWithTag(PagedPool,
                                                AclLength,
                                                TAG_ACL);
    if (SePublicDefaultDacl == NULL)
        return FALSE;

    RtlCreateAcl(SePublicDefaultDacl,
                 AclLength,
                 ACL_REVISION);

    RtlAddAccessAllowedAce(SePublicDefaultDacl,
                           ACL_REVISION,
                           GENERIC_EXECUTE,
                           SeWorldSid);

    RtlAddAccessAllowedAce(SePublicDefaultDacl,
                           ACL_REVISION,
                           GENERIC_ALL,
                           SeLocalSystemSid);

    RtlAddAccessAllowedAce(SePublicDefaultDacl,
                           ACL_REVISION,
                           GENERIC_ALL,
                           SeAliasAdminsSid);

    /* create PublicDefaultUnrestrictedDacl */
    AclLength = sizeof(ACL) +
                (sizeof(ACE) + RtlLengthSid(SeWorldSid)) +
                (sizeof(ACE) + RtlLengthSid(SeLocalSystemSid)) +
                (sizeof(ACE) + RtlLengthSid(SeAliasAdminsSid)) +
                (sizeof(ACE) + RtlLengthSid(SeRestrictedCodeSid));

    SePublicDefaultUnrestrictedDacl = ExAllocatePoolWithTag(PagedPool,
                                                            AclLength,
                                                            TAG_ACL);
    if (SePublicDefaultUnrestrictedDacl == NULL)
        return FALSE;

    RtlCreateAcl(SePublicDefaultUnrestrictedDacl,
                 AclLength,
                 ACL_REVISION);

    RtlAddAccessAllowedAce(SePublicDefaultUnrestrictedDacl,
                           ACL_REVISION,
                           GENERIC_EXECUTE,
                           SeWorldSid);

    RtlAddAccessAllowedAce(SePublicDefaultUnrestrictedDacl,
                           ACL_REVISION,
                           GENERIC_ALL,
                           SeLocalSystemSid);

    RtlAddAccessAllowedAce(SePublicDefaultUnrestrictedDacl,
                           ACL_REVISION,
                           GENERIC_ALL,
                           SeAliasAdminsSid);

    RtlAddAccessAllowedAce(SePublicDefaultUnrestrictedDacl,
                           ACL_REVISION,
                           GENERIC_READ | GENERIC_EXECUTE | READ_CONTROL,
                           SeRestrictedCodeSid);

    /* create PublicOpenDacl */
    AclLength = sizeof(ACL) +
                (sizeof(ACE) + RtlLengthSid(SeWorldSid)) +
                (sizeof(ACE) + RtlLengthSid(SeLocalSystemSid)) +
                (sizeof(ACE) + RtlLengthSid(SeAliasAdminsSid));

    SePublicOpenDacl = ExAllocatePoolWithTag(PagedPool,
                                             AclLength,
                                             TAG_ACL);
    if (SePublicOpenDacl == NULL)
        return FALSE;

    RtlCreateAcl(SePublicOpenDacl,
                 AclLength,
                 ACL_REVISION);

    RtlAddAccessAllowedAce(SePublicOpenDacl,
                           ACL_REVISION,
                           GENERIC_READ | GENERIC_WRITE | GENERIC_EXECUTE,
                           SeWorldSid);

    RtlAddAccessAllowedAce(SePublicOpenDacl,
                           ACL_REVISION,
                           GENERIC_ALL,
                           SeLocalSystemSid);

    RtlAddAccessAllowedAce(SePublicOpenDacl,
                           ACL_REVISION,
                           GENERIC_ALL,
                           SeAliasAdminsSid);

    /* create PublicOpenUnrestrictedDacl */
    AclLength = sizeof(ACL) +
                (sizeof(ACE) + RtlLengthSid(SeWorldSid)) +
                (sizeof(ACE) + RtlLengthSid(SeLocalSystemSid)) +
                (sizeof(ACE) + RtlLengthSid(SeAliasAdminsSid)) +
                (sizeof(ACE) + RtlLengthSid(SeRestrictedCodeSid));

    SePublicOpenUnrestrictedDacl = ExAllocatePoolWithTag(PagedPool,
                                                         AclLength,
                                                         TAG_ACL);
    if (SePublicOpenUnrestrictedDacl == NULL)
        return FALSE;

    RtlCreateAcl(SePublicOpenUnrestrictedDacl,
                 AclLength,
                 ACL_REVISION);

    RtlAddAccessAllowedAce(SePublicOpenUnrestrictedDacl,
                           ACL_REVISION,
                           GENERIC_ALL,
                           SeWorldSid);

    RtlAddAccessAllowedAce(SePublicOpenUnrestrictedDacl,
                           ACL_REVISION,
                           GENERIC_ALL,
                           SeLocalSystemSid);

    RtlAddAccessAllowedAce(SePublicOpenUnrestrictedDacl,
                           ACL_REVISION,
                           GENERIC_ALL,
                           SeAliasAdminsSid);

    RtlAddAccessAllowedAce(SePublicOpenUnrestrictedDacl,
                           ACL_REVISION,
                           GENERIC_READ | GENERIC_EXECUTE,
                           SeRestrictedCodeSid);

    /* create SystemDefaultDacl */
    AclLength = sizeof(ACL) +
                (sizeof(ACE) + RtlLengthSid(SeLocalSystemSid)) +
                (sizeof(ACE) + RtlLengthSid(SeAliasAdminsSid));

    SeSystemDefaultDacl = ExAllocatePoolWithTag(PagedPool,
                                                AclLength,
                                                TAG_ACL);
    if (SeSystemDefaultDacl == NULL)
        return FALSE;

    RtlCreateAcl(SeSystemDefaultDacl,
                 AclLength,
                 ACL_REVISION);

    RtlAddAccessAllowedAce(SeSystemDefaultDacl,
                           ACL_REVISION,
                           GENERIC_ALL,
                           SeLocalSystemSid);

    RtlAddAccessAllowedAce(SeSystemDefaultDacl,
                           ACL_REVISION,
                           GENERIC_READ | GENERIC_EXECUTE | READ_CONTROL,
                           SeAliasAdminsSid);

    /* create UnrestrictedDacl */
    AclLength = sizeof(ACL) +
                (sizeof(ACE) + RtlLengthSid(SeWorldSid)) +
                (sizeof(ACE) + RtlLengthSid(SeRestrictedCodeSid));

    SeUnrestrictedDacl = ExAllocatePoolWithTag(PagedPool,
                                               AclLength,
                                               TAG_ACL);
    if (SeUnrestrictedDacl == NULL)
        return FALSE;

    RtlCreateAcl(SeUnrestrictedDacl,
                 AclLength,
                 ACL_REVISION);

    RtlAddAccessAllowedAce(SeUnrestrictedDacl,
                           ACL_REVISION,
                           GENERIC_ALL,
                           SeWorldSid);

    RtlAddAccessAllowedAce(SeUnrestrictedDacl,
                           ACL_REVISION,
                           GENERIC_READ | GENERIC_EXECUTE,
                           SeRestrictedCodeSid);

    /* create SystemAnonymousLogonDacl */
    AclLength = sizeof(ACL) +
                (sizeof(ACE) + RtlLengthSid(SeWorldSid)) +
                (sizeof(ACE) + RtlLengthSid(SeAnonymousLogonSid));

    SeSystemAnonymousLogonDacl = ExAllocatePoolWithTag(PagedPool,
                                                       AclLength,
                                                       TAG_ACL);
    if (SeSystemAnonymousLogonDacl == NULL)
        return FALSE;

    RtlCreateAcl(SeSystemAnonymousLogonDacl,
                 AclLength,
                 ACL_REVISION);

    RtlAddAccessAllowedAce(SeSystemAnonymousLogonDacl,
                           ACL_REVISION,
                           GENERIC_ALL,
                           SeWorldSid);

    RtlAddAccessAllowedAce(SeSystemAnonymousLogonDacl,
                           ACL_REVISION,
                           GENERIC_ALL,
                           SeAnonymousLogonSid);

    return TRUE;
}

/**
 * @brief
 * Allocates a discretionary access control list based on certain properties
 * of a regular and primary access tokens.
 *
 * @param[in] Token
 * An access token.
 *
 * @param[in] PrimaryToken
 * A primary access token.
 *
 * @param[out] Dacl
 * The returned allocated DACL.
 *
 * @return
 * Returns STATUS_SUCCESS if DACL creation from tokens has completed
 * successfully. STATUS_INSUFFICIENT_RESOURCES is returned if DACL
 * allocation from memory pool fails otherwise.
 */
NTSTATUS
NTAPI
SepCreateImpersonationTokenDacl(
    _In_ PTOKEN Token,
    _In_ PTOKEN PrimaryToken,
    _Out_ PACL* Dacl)
{
    ULONG AclLength;
    PACL TokenDacl;

    PAGED_CODE();

    *Dacl = NULL;

    AclLength = sizeof(ACL) +
        (sizeof(ACE) + RtlLengthSid(SeAliasAdminsSid)) +
        (sizeof(ACE) + RtlLengthSid(SeLocalSystemSid)) +
        (sizeof(ACE) + RtlLengthSid(SeRestrictedCodeSid)) +
        (sizeof(ACE) + RtlLengthSid(Token->UserAndGroups->Sid)) +
        (sizeof(ACE) + RtlLengthSid(PrimaryToken->UserAndGroups->Sid));

    TokenDacl = ExAllocatePoolWithTag(PagedPool, AclLength, TAG_ACL);
    if (TokenDacl == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlCreateAcl(TokenDacl, AclLength, ACL_REVISION);
    RtlAddAccessAllowedAce(TokenDacl, ACL_REVISION, GENERIC_ALL,
                           Token->UserAndGroups->Sid);
    RtlAddAccessAllowedAce(TokenDacl, ACL_REVISION, GENERIC_ALL,
                           PrimaryToken->UserAndGroups->Sid);
    RtlAddAccessAllowedAce(TokenDacl, ACL_REVISION, GENERIC_ALL,
                           SeAliasAdminsSid);
    RtlAddAccessAllowedAce(TokenDacl, ACL_REVISION, GENERIC_ALL,
                           SeLocalSystemSid);

    if (Token->RestrictedSids != NULL || PrimaryToken->RestrictedSids != NULL)
    {
        RtlAddAccessAllowedAce(TokenDacl, ACL_REVISION, GENERIC_ALL,
                               SeRestrictedCodeSid);
    }

    *Dacl = TokenDacl;

    return STATUS_SUCCESS;
}

/**
 * @brief
 * Captures an access control list from an already valid input ACL.
 *
 * @param[in] InputAcl
 * A valid ACL.
 *
 * @param[in] AccessMode
 * Processor level access mode. The processor mode determines how
 * the input arguments are probed.
 *
 * @param[in] PoolType
 * Pool type for new captured ACL for creation. The pool type determines
 * in which memory pool the ACL data should reside.
 *
 * @param[in] CaptureIfKernel
 * If set to TRUE and the processor access mode being KernelMode, we are
 * capturing an ACL directly in the kernel. Otherwise we are capturing
 * within a kernel mode driver.
 *
 * @param[out] CapturedAcl
 * The returned and allocated captured ACL.
 *
 * @return
 * Returns STATUS_SUCCESS if the ACL has been successfully captured.
 * STATUS_INSUFFICIENT_RESOURCES is returned otherwise.
 */
NTSTATUS
NTAPI
SepCaptureAcl(
    _In_ PACL InputAcl,
    _In_ KPROCESSOR_MODE AccessMode,
    _In_ POOL_TYPE PoolType,
    _In_ BOOLEAN CaptureIfKernel,
    _Out_ PACL *CapturedAcl)
{
    PACL NewAcl;
    ULONG AclSize = 0;

    PAGED_CODE();

    /* If in kernel mode and we do not capture, just
     * return the given ACL and don't validate it. */
    if ((AccessMode == KernelMode) && !CaptureIfKernel)
    {
        *CapturedAcl = InputAcl;
        return STATUS_SUCCESS;
    }

    /* Otherwise, capture and validate the ACL, depending on the access mode */
    if (AccessMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForRead(InputAcl,
                         sizeof(ACL),
                         sizeof(ULONG));
            AclSize = InputAcl->AclSize;
            ProbeForRead(InputAcl,
                         AclSize,
                         sizeof(ULONG));
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            /* Return the exception code */
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;

        /* Validate the minimal size an ACL can have */
        if (AclSize < sizeof(ACL))
            return STATUS_INVALID_ACL;

        NewAcl = ExAllocatePoolWithTag(PoolType,
                                       AclSize,
                                       TAG_ACL);
        if (!NewAcl)
            return STATUS_INSUFFICIENT_RESOURCES;

        _SEH2_TRY
        {
            RtlCopyMemory(NewAcl, InputAcl, AclSize);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            /* Free the ACL and return the exception code */
            ExFreePoolWithTag(NewAcl, TAG_ACL);
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }
    else
    {
        AclSize = InputAcl->AclSize;

        /* Validate the minimal size an ACL can have */
        if (AclSize < sizeof(ACL))
            return STATUS_INVALID_ACL;

        NewAcl = ExAllocatePoolWithTag(PoolType,
                                       AclSize,
                                       TAG_ACL);
        if (!NewAcl)
            return STATUS_INSUFFICIENT_RESOURCES;

        RtlCopyMemory(NewAcl, InputAcl, AclSize);
    }

    /* Validate the captured ACL */
    if (!RtlValidAcl(NewAcl))
    {
        /* Free the ACL and fail */
        ExFreePoolWithTag(NewAcl, TAG_ACL);
        return STATUS_INVALID_ACL;
    }

    /* It's valid, return it */
    *CapturedAcl = NewAcl;
    return STATUS_SUCCESS;
}

/**
 * @brief
 * Releases (frees) a captured ACL from the memory pool.
 *
 * @param[in] CapturedAcl
 * A valid captured ACL to free.
 *
 * @param[in] AccessMode
 * Processor level access mode.
 *
 * @param[in] CaptureIfKernel
 * If set to TRUE and the processor access mode being KernelMode, we're
 * releasing an ACL directly in the kernel. Otherwise we're releasing
 * within a kernel mode driver.
 *
 * @return
 * Nothing.
 */
VOID
NTAPI
SepReleaseAcl(
    _In_ PACL CapturedAcl,
    _In_ KPROCESSOR_MODE AccessMode,
    _In_ BOOLEAN CaptureIfKernel)
{
    PAGED_CODE();

    if (CapturedAcl != NULL &&
        (AccessMode != KernelMode ||
         (AccessMode == KernelMode && CaptureIfKernel)))
    {
        ExFreePoolWithTag(CapturedAcl, TAG_ACL);
    }
}

static NTSTATUS
SepTransformCreatorAce(RTL_SECURITY_ACL_BUFFER *Buffer, PACE_HEADER Ace,
                       PSID Owner, PSID Group, PGENERIC_MAPPING Mapping)
{
    RTL_SECURITY_ACE_VIEW View;
    ACCESS_MASK Mask;
    NTSTATUS Status;

    Status = RtlpSecurityAceView(Ace, &View);
    if (!NT_SUCCESS(Status)) return Status;
    Mask = ((PACCESS_ALLOWED_ACE)Ace)->Mask;
    if (Ace->AceType <= ACCESS_MAX_MS_V2_ACE_TYPE && !(Ace->AceFlags & INHERIT_ONLY_ACE))
    {
        RtlMapGenericMask(&Mask, Mapping);
        Mask &= Mapping->GenericAll;
    }
    return RtlpSecurityEmitAce(Buffer, Ace, Ace->AceFlags, FALSE, Owner, Group, Mapping, &Mask);
}

static NTSTATUS
SepPropagateOpaqueAce(RTL_SECURITY_ACL_BUFFER *Buffer, PACE_HEADER Ace,
                      BOOLEAN Inherited, BOOLEAN Container)
{
    UCHAR Flags = Ace->AceFlags;
    PACE_HEADER Dest;

    if (Inherited)
    {
        if (!Container || (Flags & NO_PROPAGATE_INHERIT_ACE))
        {
            if (!(Flags & (Container ? CONTAINER_INHERIT_ACE : OBJECT_INHERIT_ACE)))
                return STATUS_SUCCESS;
            Flags &= ~VALID_INHERIT_FLAGS;
        }
        else if (Flags & CONTAINER_INHERIT_ACE)
        {
            Flags &= ~INHERIT_ONLY_ACE;
        }
        else if (Flags & OBJECT_INHERIT_ACE)
        {
            Flags |= INHERIT_ONLY_ACE;
        }
        else return STATUS_SUCCESS;
        Flags |= INHERITED_ACE;
    }
    if (Buffer->Length > MAXUSHORT - Ace->AceSize || Buffer->Count == MAXUSHORT)
        return STATUS_ALLOTTED_SPACE_EXCEEDED;
    if (Buffer->Acl)
    {
        if (Buffer->Length > Buffer->Capacity || Ace->AceSize > Buffer->Capacity - Buffer->Length)
            return STATUS_BUFFER_TOO_SMALL;
        Dest = (PACE_HEADER)((PUCHAR)Buffer->Acl + Buffer->Length);
        RtlCopyMemory(Dest, Ace, Ace->AceSize);
        Dest->AceFlags = Flags;
    }
    Buffer->Length += Ace->AceSize;
    Buffer->Count++;
    return STATUS_SUCCESS;
}

/**
 * @brief
 * Propagates (copies) an access control list.
 *
 * @param[out] AclDest
 * The destination parameter with propagated ACL.
 *
 * @param[in,out] AclLength
 * The length of the ACL that we propagate.
 *
 * @param[in] AclSource
 * The source instance of a valid ACL.
 *
 * @param[in] Owner
 * A SID that represents the main user that identifies the ACL.
 *
 * @param[in] Group
 * A SID that represents a group that identifies the ACL.
 *
 * @param[in] IsInherited
 * If set to TRUE, that means the ACL is directly inherited.
 *
 * @param[in] IsDirectoryObject
 * If set to TRUE, that means the ACL is directly inherited because
 * of the object that inherits it.
 *
 * @param[in] GenericMapping
 * Generic mapping of access rights to map only certain effective
 * ACEs.
 *
 * @return
 * Returns STATUS_SUCCESS if ACL has been propagated successfully.
 * STATUS_BUFFER_TOO_SMALL is returned if the ACL length is not greater
 * than the maximum written size of the buffer for ACL propagation
 * otherwise.
 */
NTSTATUS
SepPropagateAcl(
    _Out_writes_bytes_opt_(AclLength) PACL AclDest,
    _Inout_ PULONG AclLength,
    _In_reads_bytes_(AclSource->AclSize) PACL AclSource,
    _In_ PSID Owner,
    _In_ PSID Group,
    _In_ BOOLEAN IsInherited,
    _In_ BOOLEAN IsDirectoryObject,
    _In_opt_ GUID *ObjectType,
    _In_ PGENERIC_MAPPING GenericMapping)
{
    RTL_SECURITY_ACL_BUFFER Buffer = {0};
    PACE_HEADER Ace;
    ULONG Index, Pass, Required;
    NTSTATUS Status;

    if (!RtlValidAcl(AclSource)) return STATUS_INVALID_ACL;
    for (Pass = 0; Pass < 2; ++Pass)
    {
        Buffer.Length = sizeof(ACL);
        Buffer.Count = 0;
        Buffer.Revision = AclSource->AclRevision;
        for (Index = 0; Index < AclSource->AceCount; ++Index)
        {
            Status = RtlGetAce(AclSource, Index, (PVOID *)&Ace);
            if (!NT_SUCCESS(Status)) return Status;
            if (IsInherited)
                Status = RtlpSecurityTransformAce(&Buffer, Ace, TRUE, FALSE,
                                                   IsDirectoryObject, &ObjectType,
                                                   ObjectType ? 1 : 0, Owner, Group,
                                                   GenericMapping);
            else
                Status = SepTransformCreatorAce(&Buffer, Ace, Owner, Group, GenericMapping);
            if (Status == STATUS_NOT_IMPLEMENTED)
                Status = SepPropagateOpaqueAce(&Buffer, Ace, IsInherited, IsDirectoryObject);
            if (!NT_SUCCESS(Status)) return Status;
        }
        if (!Pass)
        {
            Required = Buffer.Length;
            Buffer.Capacity = *AclLength;
            *AclLength = Required;
            if (!AclDest || Buffer.Capacity < Required) return STATUS_BUFFER_TOO_SMALL;
            Buffer.Acl = AclDest;
        }
    }
    RtlZeroMemory(AclDest, sizeof(ACL));
    AclDest->AclRevision = Buffer.Revision;
    AclDest->AclSize = (USHORT)Buffer.Length;
    AclDest->AceCount = (USHORT)Buffer.Count;
    return STATUS_SUCCESS;
}

/**
 * @brief
 * Selects an ACL and returns it to the caller.
 *
 * @param[in] ExplicitAcl
 * If specified, the specified ACL to the call will be
 * the selected ACL for the caller.
 *
 * @param[in] ExplicitPresent
 * If set to TRUE and with specific ACL filled to the call, the
 * function will immediately return the specific ACL as the selected
 * ACL for the caller.
 *
 * @param[in] ExplicitDefaulted
 * If set to FALSE and with specific ACL filled to the call, the ACL
 * is not a default ACL. Otherwise it's a default ACL that we cannot
 * select it as is.
 *
 * @param[in] ParentAcl
 * If specified, the parent ACL will be used to determine the exact ACL
 * length to check  if the ACL in question is not empty. If the list
 * is not empty then the function will select such ACL to the caller.
 *
 * @param[in] DefaultAcl
 * If specified, the default ACL will be the selected one for the caller.
 *
 * @param[out] AclLength
 * The size length of an ACL.
 *
 * @param[in] Owner
 * A SID that represents the main user that identifies the ACL.
 *
 * @param[in] Group
 * A SID that represents a group that identifies the ACL.
 *
 * @param[out] AclPresent
 * The returned boolean value, indicating if the ACL that we want to select
 * does actually exist.
 *
 * @param[out] IsInherited
 * The returned boolean value, indicating if the ACL we want to select it
 * is actually inherited or not.
 *
 * @param[in] IsDirectoryObject
 * If set to TRUE, the object inherits this ACL.
 *
 * @param[in] GenericMapping
 * Generic mapping of access rights to map only certain effective
 * ACEs of an ACL that we want to select it.
 *
 */
NTSTATUS
SepSelectAcl(
    _In_opt_ PACL ExplicitAcl,
    _In_ BOOLEAN ExplicitPresent,
    _In_ BOOLEAN ExplicitDefaulted,
    _In_opt_ PACL ParentAcl,
    _In_opt_ PACL DefaultAcl,
    _Out_ PULONG AclLength,
    _In_ PSID Owner,
    _In_ PSID Group,
    _Out_ PBOOLEAN AclPresent,
    _Out_ PBOOLEAN IsInherited,
    _In_ BOOLEAN IsDirectoryObject,
    _In_opt_ GUID *ObjectType,
    _In_ PGENERIC_MAPPING GenericMapping,
    _Out_ PACL *SelectedAcl)
{
    PACL Acl;
    NTSTATUS Status;

    *SelectedAcl = NULL;
    *AclPresent = TRUE;
    if (ExplicitPresent && !ExplicitDefaulted)
    {
        Acl = ExplicitAcl;
    }
    else
    {
        if (ParentAcl)
        {
            *IsInherited = TRUE;
            *AclLength = 0;
            Status = SepPropagateAcl(NULL,
                                     AclLength,
                                     ParentAcl,
                                     Owner,
                                     Group,
                                     *IsInherited,
                                     IsDirectoryObject,
                                     ObjectType,
                                     GenericMapping);
            if (Status != STATUS_BUFFER_TOO_SMALL) return Status;

            /* Use the parent ACL only if it's not empty */
            if (*AclLength != sizeof(ACL))
            {
                *SelectedAcl = ParentAcl;
                return STATUS_SUCCESS;
            }
        }

        if (ExplicitPresent)
        {
            Acl = ExplicitAcl;
        }
        else if (DefaultAcl)
        {
            Acl = DefaultAcl;
        }
        else
        {
            *AclPresent = FALSE;
            Acl = NULL;
        }
    }

    *IsInherited = FALSE;
    *AclLength = 0;
    if (Acl)
    {
        /* Get the length */
        Status = SepPropagateAcl(NULL,
                                 AclLength,
                                 Acl,
                                 Owner,
                                 Group,
                                 *IsInherited,
                                 IsDirectoryObject,
                                 ObjectType,
                                 GenericMapping);
        if (Status != STATUS_BUFFER_TOO_SMALL) return Status;
    }
    *SelectedAcl = Acl;
    return STATUS_SUCCESS;
}

/* EOF */
