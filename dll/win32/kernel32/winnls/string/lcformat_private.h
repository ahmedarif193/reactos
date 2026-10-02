/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS System Libraries
 * FILE:            dll/win32/kernel32/winnls/string/lcformat_private.h
 * PURPOSE:         Win32 Kernel Library Header
 * PROGRAMMER:      Dmitry Chapyshev (dmitry@reactos.org)
 */

#pragma once

/*lcformat.c */
extern BOOL NLS_IsUnicodeOnlyLcid(LCID);

/* Returns the ANSI codepage used by locale formatting when LOCALE_USE_CP_ACP is not set.
 * dwFlags should contain the LOCALE_ flags relevant to formatting (typically LOCALE_NOUSEROVERRIDE).
 */
extern DWORD NLS_GetAnsiCodePage(LCID lcid, DWORD dwFlags);

enum enum_callback_type {
	CALLBACK_ENUMPROC,
	CALLBACK_ENUMPROCEX,
	CALLBACK_ENUMPROCEXEX
};

struct enumdateformats_context {
	enum enum_callback_type type;  /* callback kind */
	union {
		DATEFMT_ENUMPROCW    callback;     /* user callback pointer */
		DATEFMT_ENUMPROCEXW  callbackex;
		DATEFMT_ENUMPROCEXEX callbackexex;
	} u;
	LCID   lcid;    /* locale of interest */
	DWORD  flags;
	LPARAM lParam;
	BOOL   unicode; /* A vs W callback type, only for regular and Ex callbacks */
};

struct enumtimeformats_context {
	enum enum_callback_type type;  /* callback kind */
	union {
		TIMEFMT_ENUMPROCW  callback;     /* user callback pointer */
		TIMEFMT_ENUMPROCEX callbackex;
	} u;
	LCID   lcid;    /* locale of interest */
	DWORD  flags;
	LPARAM lParam;
	BOOL   unicode; /* A vs W callback type, only for regular and Ex callbacks */
};

extern BOOL NLS_EnumDateFormats(const struct enumdateformats_context *ctxt);
extern BOOL NLS_EnumTimeFormats(struct enumtimeformats_context *ctxt);
extern const void * WINAPI NlsValidateLocale(LCID *lcid, ULONG flags);
extern BOOL WINAPI Internal_EnumCalendarInfo(CALINFO_ENUMPROCW proc, const void *locale, CALID id,
                                             CALTYPE type, BOOL unicode, BOOL ex, BOOL exex, LPARAM lparam);
