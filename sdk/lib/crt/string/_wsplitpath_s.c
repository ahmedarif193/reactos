/*
 * PROJECT:     LiberNT CRT library
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Implementation of _wsplitpath_s, from Wine dlls/msvcrt/dir.c
 * COPYRIGHT:   Copyright the Wine project authors
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <stddef.h>
#include <string.h>

#define CRT_EINVAL 22
#define CRT_ERANGE 34

int
__cdecl
_wsplitpath_s(
    const wchar_t *inpath,
    wchar_t *drive,
    size_t sz_drive,
    wchar_t *dir,
    size_t sz_dir,
    wchar_t *fname,
    size_t sz_fname,
    wchar_t *ext,
    size_t sz_ext)
{
    const wchar_t *p, *end;

    if (!inpath ||
        (!drive && sz_drive) || (drive && !sz_drive) ||
        (!dir && sz_dir) || (dir && !sz_dir) ||
        (!fname && sz_fname) || (fname && !sz_fname) ||
        (!ext && sz_ext) || (ext && !sz_ext))
    {
        if (drive && sz_drive) drive[0] = 0;
        if (dir && sz_dir) dir[0] = 0;
        if (fname && sz_fname) fname[0] = 0;
        if (ext && sz_ext) ext[0] = 0;
        return CRT_EINVAL;
    }

    if (inpath[0] && inpath[1] == L':')
    {
        if (drive)
        {
            if (sz_drive <= 2) goto do_error;
            drive[0] = inpath[0];
            drive[1] = inpath[1];
            drive[2] = 0;
        }
        inpath += 2;
    }
    else if (drive)
    {
        drive[0] = 0;
    }

    end = NULL;
    for (p = inpath; *p; p++)
    {
        if (*p == L'/' || *p == L'\\') end = p + 1;
    }

    if (end)
    {
        if (dir)
        {
            if (sz_dir <= (size_t)(end - inpath)) goto do_error;
            memcpy(dir, inpath, (end - inpath) * sizeof(wchar_t));
            dir[end - inpath] = 0;
        }
        inpath = end;
    }
    else if (dir)
    {
        dir[0] = 0;
    }

    end = NULL;
    for (p = inpath; *p; p++)
    {
        if (*p == L'.') end = p;
    }
    if (!end) end = p;

    if (fname)
    {
        if (sz_fname <= (size_t)(end - inpath)) goto do_error;
        memcpy(fname, inpath, (end - inpath) * sizeof(wchar_t));
        fname[end - inpath] = 0;
    }
    if (ext)
    {
        if (sz_ext <= wcslen(end)) goto do_error;
        wcscpy(ext, end);
    }
    return 0;

do_error:
    if (drive) drive[0] = 0;
    if (dir) dir[0] = 0;
    if (fname) fname[0] = 0;
    if (ext) ext[0] = 0;
    return CRT_ERANGE;
}
