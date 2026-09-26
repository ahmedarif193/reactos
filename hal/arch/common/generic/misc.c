/*
 * PROJECT:         ReactOS Hardware Abstraction Layer (HAL)
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            hal/arch/common/generic/misc.c
 * PURPOSE:         Registry helpers every HAL shares
 * PROGRAMMERS:     ReactOS Portable Systems Group
 */

/* INCLUDES *******************************************************************/

#include <ntifs.h>

/* FUNCTIONS ******************************************************************/

NTSTATUS
NTAPI
HalpOpenRegistryKey(IN PHANDLE KeyHandle,
                    IN HANDLE RootKey,
                    IN PUNICODE_STRING KeyName,
                    IN ACCESS_MASK DesiredAccess,
                    IN BOOLEAN Create)
{
    NTSTATUS Status;
    ULONG Disposition;
    OBJECT_ATTRIBUTES ObjectAttributes;

    /* Setup the attributes we received */
    InitializeObjectAttributes(&ObjectAttributes,
                               KeyName,
                               OBJ_CASE_INSENSITIVE,
                               RootKey,
                               NULL);

    /* What to do? */
    if (Create)
    {
        /* Create the key */
        Status = ZwCreateKey(KeyHandle,
                             DesiredAccess,
                             &ObjectAttributes,
                             0,
                             NULL,
                             REG_OPTION_VOLATILE,
                             &Disposition);
    }
    else
    {
        /* Open the key */
        Status = ZwOpenKey(KeyHandle, DesiredAccess, &ObjectAttributes);
    }

    /* We're done */
    return Status;
}

/* EOF */
