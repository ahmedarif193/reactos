/*
 * PROJECT:     ReactOS NTFS-3G Library
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Logging shared by all ReactOS hosts
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "config.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "host.h"
#include "logging.h"

static u32 Ntfs3gLogLevels = NTFS_LOG_LEVEL_INFO |
                             NTFS_LOG_LEVEL_WARNING |
                             NTFS_LOG_LEVEL_ERROR |
                             NTFS_LOG_LEVEL_PERROR |
                             NTFS_LOG_LEVEL_CRITICAL;
static u32 Ntfs3gLogFlags;
static ntfs_log_handler *Ntfs3gLogHandler = ntfs_log_handler_outerr;

static void
Ntfs3gRosLogMessage(int IsError,
                    const char *Format,
                    va_list Arguments)
{
    char Buffer[512];
    int Length;

    Length = _vsnprintf(Buffer, sizeof(Buffer) - 1, Format, Arguments);
    if (Length < 0 || Length >= sizeof(Buffer))
        Length = sizeof(Buffer) - 1;
    Buffer[Length] = '\0';
    Ntfs3gRosHostLog(IsError, Buffer);
}

static int
Ntfs3gRosLog(const char *Function,
             const char *File,
             int Line,
             u32 Level,
             void *Data,
             const char *Format,
             va_list Arguments)
{
    (void)Function;
    (void)File;
    (void)Line;
    (void)Data;
    Ntfs3gRosLogMessage((Level & (NTFS_LOG_LEVEL_ERROR |
                                 NTFS_LOG_LEVEL_PERROR |
                                 NTFS_LOG_LEVEL_CRITICAL)) != 0,
                         Format,
                         Arguments);
    return 0;
}

void
ntfs_log_set_handler(ntfs_log_handler *Handler)
{
    Ntfs3gLogHandler = Handler ? Handler : ntfs_log_handler_null;
}

u32
ntfs_log_set_levels(u32 Levels)
{
    u32 OldLevels = Ntfs3gLogLevels;
    Ntfs3gLogLevels |= Levels;
    return OldLevels;
}

u32
ntfs_log_clear_levels(u32 Levels)
{
    u32 OldLevels = Ntfs3gLogLevels;
    Ntfs3gLogLevels &= ~Levels;
    return OldLevels;
}

u32
ntfs_log_get_levels(void)
{
    return Ntfs3gLogLevels;
}

u32
ntfs_log_set_flags(u32 Flags)
{
    u32 OldFlags = Ntfs3gLogFlags;
    Ntfs3gLogFlags |= Flags;
    return OldFlags;
}

u32
ntfs_log_clear_flags(u32 Flags)
{
    u32 OldFlags = Ntfs3gLogFlags;
    Ntfs3gLogFlags &= ~Flags;
    return OldFlags;
}

u32
ntfs_log_get_flags(void)
{
    return Ntfs3gLogFlags;
}

BOOL
ntfs_log_parse_option(const char *Option)
{
    if (!Option)
        return FALSE;
    if (!strcmp(Option, "debug")) {
        ntfs_log_set_levels(NTFS_LOG_LEVEL_DEBUG | NTFS_LOG_LEVEL_TRACE);
        return TRUE;
    }
    return FALSE;
}

int
ntfs_log_redirect(const char *Function,
                  const char *File,
                  int Line,
                  u32 Level,
                  void *Data,
                  const char *Format,
                  ...)
{
    int OldErrno = errno;
    int Result;
    va_list Arguments;

    if (!(Ntfs3gLogLevels & Level))
        return 0;

    va_start(Arguments, Format);
    Result = Ntfs3gLogHandler(Function, File, Line, Level, Data, Format,
                              Arguments);
    va_end(Arguments);
    errno = OldErrno;
    return Result;
}

#define NTFS3G_DEFINE_LOG_HANDLER(Name) \
    int Name(const char *Function, const char *File, int Line, u32 Level, \
             void *Data, const char *Format, va_list Arguments) \
    { \
        return Ntfs3gRosLog(Function, File, Line, Level, Data, Format, \
                            Arguments); \
    }

NTFS3G_DEFINE_LOG_HANDLER(ntfs_log_handler_syslog)
NTFS3G_DEFINE_LOG_HANDLER(ntfs_log_handler_fprintf)
NTFS3G_DEFINE_LOG_HANDLER(ntfs_log_handler_stdout)
NTFS3G_DEFINE_LOG_HANDLER(ntfs_log_handler_outerr)
NTFS3G_DEFINE_LOG_HANDLER(ntfs_log_handler_stderr)

int
ntfs_log_handler_null(const char *Function,
                      const char *File,
                      int Line,
                      u32 Level,
                      void *Data,
                      const char *Format,
                      va_list Arguments)
{
    (void)Function;
    (void)File;
    (void)Line;
    (void)Level;
    (void)Data;
    (void)Format;
    (void)Arguments;
    return 0;
}

void
ntfs_log_early_error(const char *Format,
                     ...)
{
    va_list Arguments;

    va_start(Arguments, Format);
    Ntfs3gRosLogMessage(1, Format, Arguments);
    va_end(Arguments);
}
