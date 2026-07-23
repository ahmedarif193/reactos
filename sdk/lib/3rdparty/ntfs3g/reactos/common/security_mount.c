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

#include "logging.h"
#include "volume.h"
#include "dir.h"
#include "index.h"

static ntfschar sii_stream[] = {
    const_cpu_to_le16('$'),
    const_cpu_to_le16('S'),
    const_cpu_to_le16('I'),
    const_cpu_to_le16('I'),
    const_cpu_to_le16(0)
};

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

/* The ReactOS hosts are intentionally read-only.  Keep the symbol required by
 * dir.c, but never synthesize or write a descriptor from these builds. */
int
ntfs_sd_add_everyone(ntfs_inode *ni)
{
    (void)ni;
    errno = EROFS;
    return -1;
}
