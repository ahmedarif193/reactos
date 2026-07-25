/**
 * security_mount.c - Mount-time access to the NTFS $Secure indexes.
 *
 * Derived from libntfs-3g/security.c.
 * Copyright (c) 2004 Anton Altaparmakov
 * Copyright (c) 2005-2006 Szabolcs Szakacsits
 * Copyright (c) 2006 Yura Pakhuchiy
 * Copyright (c) 2007-2015 Jean-Pierre Andre
 * Copyright (c) 2026 Ahmed ARIF <arif.ing@outlook.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>

#include "attrib.h"
#include "host.h"
#include "logging.h"
#include "misc.h"
#include "volume.h"
#include "dir.h"
#include "index.h"
#include "security.h"

static ntfschar sii_stream[] = {
    const_cpu_to_le16('$'),
    const_cpu_to_le16('S'),
    const_cpu_to_le16('I'),
    const_cpu_to_le16('I'),
    const_cpu_to_le16(0)
};

void
ntfs_generate_guid(GUID *Guid)
{
    uint64_t State = (uint64_t)Ntfs3gRosHostGetTime();
    u8 *Bytes = (u8 *)Guid;
    unsigned int Index;

    if (!State)
        State = UINT64_C(0x9e3779b97f4a7c15);
    for (Index = 0; Index < sizeof(*Guid); Index++) {
        State ^= State << 13;
        State ^= State >> 7;
        State ^= State << 17;
        Bytes[Index] = (u8)State;
    }
    Bytes[7] = (Bytes[7] & 0x0f) | 0x40;
    Bytes[8] = (Bytes[8] & 0x3f) | 0x80;
}

static ntfschar sdh_stream[] = {
    const_cpu_to_le16('$'),
    const_cpu_to_le16('S'),
    const_cpu_to_le16('D'),
    const_cpu_to_le16('H'),
    const_cpu_to_le16(0)
};

int
ntfs_open_secure(ntfs_volume *vol)
{
    ntfs_inode *ni;
    ntfs_index_context *sii;
    ntfs_index_context *sdh;

    if (vol->secure_ni)
        return 0;

    ni = ntfs_pathname_to_inode(vol, NULL, "$Secure");
    if (!ni)
        goto error;

    if (ni->mft_no != FILE_Secure) {
        ntfs_log_error("$Secure does not have expected inode number!");
        errno = EINVAL;
        goto close_inode;
    }

    sii = ntfs_index_ctx_get(ni, sii_stream, 4);
    if (!sii)
        goto close_inode;

    sdh = ntfs_index_ctx_get(ni, sdh_stream, 4);
    if (!sdh)
        goto close_sii;

    vol->secure_xsdh = sdh;
    vol->secure_xsii = sii;
    vol->secure_ni = ni;
    return 0;

close_sii:
    ntfs_index_ctx_put(sii);
close_inode:
    ntfs_inode_close(ni);
error:
    if (vol->major_ver < 3)
        return 0;
    ntfs_log_perror("Failed to open $Secure");
    return -1;
}

int
ntfs_close_secure(ntfs_volume *vol)
{
    int result = 0;

    if (vol->secure_ni) {
        ntfs_index_ctx_put(vol->secure_xsdh);
        ntfs_index_ctx_put(vol->secure_xsii);
        result = ntfs_inode_close(vol->secure_ni);
        vol->secure_ni = NULL;
    }
    return result;
}

int
ntfs_sd_add_everyone(ntfs_inode *ni)
{
    SECURITY_DESCRIPTOR_RELATIVE *Descriptor;
    ACCESS_ALLOWED_ACE *Ace;
    ACL *Acl;
    SID *Sid;
    int DescriptorLength;
    int Result;

    DescriptorLength = sizeof(SECURITY_DESCRIPTOR_ATTR) +
                       2 * (sizeof(SID) + sizeof(le32)) +
                       sizeof(ACL) + sizeof(ACCESS_ALLOWED_ACE);
    Descriptor = ntfs_calloc(DescriptorLength);
    if (!Descriptor)
        return -1;

    Descriptor->revision = SECURITY_DESCRIPTOR_REVISION;
    Descriptor->control = SE_DACL_PRESENT | SE_SELF_RELATIVE;

    Sid = (SID *)((u8 *)Descriptor + sizeof(SECURITY_DESCRIPTOR_ATTR));
    Sid->revision = SID_REVISION;
    Sid->sub_authority_count = 2;
    Sid->sub_authority[0] =
        const_cpu_to_le32(SECURITY_BUILTIN_DOMAIN_RID);
    Sid->sub_authority[1] = const_cpu_to_le32(DOMAIN_ALIAS_RID_ADMINS);
    Sid->identifier_authority.value[5] = 5;
    Descriptor->owner = cpu_to_le32((u8 *)Sid - (u8 *)Descriptor);

    Sid = (SID *)((u8 *)Sid + sizeof(SID) + sizeof(le32));
    Sid->revision = SID_REVISION;
    Sid->sub_authority_count = 2;
    Sid->sub_authority[0] =
        const_cpu_to_le32(SECURITY_BUILTIN_DOMAIN_RID);
    Sid->sub_authority[1] = const_cpu_to_le32(DOMAIN_ALIAS_RID_ADMINS);
    Sid->identifier_authority.value[5] = 5;
    Descriptor->group = cpu_to_le32((u8 *)Sid - (u8 *)Descriptor);

    Acl = (ACL *)((u8 *)Sid + sizeof(SID) + sizeof(le32));
    Acl->revision = ACL_REVISION;
    Acl->size = const_cpu_to_le16(sizeof(ACL) +
                                  sizeof(ACCESS_ALLOWED_ACE));
    Acl->ace_count = const_cpu_to_le16(1);
    Descriptor->dacl = cpu_to_le32((u8 *)Acl - (u8 *)Descriptor);

    Ace = (ACCESS_ALLOWED_ACE *)((u8 *)Acl + sizeof(ACL));
    Ace->type = ACCESS_ALLOWED_ACE_TYPE;
    Ace->flags = OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE;
    Ace->size = const_cpu_to_le16(sizeof(ACCESS_ALLOWED_ACE));
    Ace->mask = const_cpu_to_le32(0x1f01ff);
    Ace->sid.revision = SID_REVISION;
    Ace->sid.sub_authority_count = 1;
    Ace->sid.sub_authority[0] = const_cpu_to_le32(0);
    Ace->sid.identifier_authority.value[5] = 1;

    Result = ntfs_attr_add(ni, AT_SECURITY_DESCRIPTOR, AT_UNNAMED, 0,
                           (u8 *)Descriptor, DescriptorLength);
    if (Result)
        ntfs_log_perror("Failed to add initial SECURITY_DESCRIPTOR");
    free(Descriptor);
    return Result;
}
