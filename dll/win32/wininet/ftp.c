/*
 * WININET - Ftp implementation
 *
 * Copyright 1999 Corel Corporation
 * Copyright 2004 Mike McCormack for CodeWeavers
 * Copyright 2004 Kevin Koltzau
 * Copyright 2007 Hans Leidekker
 *
 * Ulrich Czekalla
 * Noureddine Jemmali
 *
 * Copyright 2000 Andreas Mohr
 * Copyright 2002 Jaco Greeff
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "ws2tcpip.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <assert.h>

#include "windef.h"
#include "winbase.h"
#include "wingdi.h"
#include "winuser.h"
#include "wininet.h"
#include "winnls.h"
#include "winerror.h"
#include "winreg.h"
#include "winternl.h"
#include "shlwapi.h"

#include "wine/debug.h"
#include "internet.h"

WINE_DEFAULT_DEBUG_CHANNEL(wininet);

#define RESPONSE_TIMEOUT        30

typedef struct _ftp_session_t ftp_session_t;

typedef struct
{
    object_header_t hdr;
    ftp_session_t *lpFtpSession;
    BOOL session_deleted;
    int nDataSocket;
    WCHAR *cache_file;
    HANDLE cache_file_handle;
} ftp_file_t;

struct _ftp_session_t
{
    object_header_t hdr;
    appinfo_t *lpAppInfo;
    int sndSocket;
    int lstnSocket;
    int pasvSocket; /* data socket connected by us in case of passive FTP */
    ftp_file_t *download_in_progress;
    struct sockaddr_in socketAddress;
    struct sockaddr_in lstnSocketAddress;
    LPWSTR servername;
    INTERNET_PORT serverport;
    LPWSTR  lpszPassword;
    LPWSTR  lpszUserName;
};

typedef struct
{
    BOOL bIsDirectory;
    LPWSTR lpszName;
    DWORD nSize;
    SYSTEMTIME tmLastModified;
    unsigned short permissions;
} FILEPROPERTIESW, *LPFILEPROPERTIESW;

typedef struct
{
    object_header_t hdr;
    ftp_session_t *lpFtpSession;
    DWORD index;
    DWORD size;
    LPFILEPROPERTIESW lpafp;
} WININETFTPFINDNEXTW, *LPWININETFTPFINDNEXTW;

#define DATA_PACKET_SIZE 	0x2000
#define szCRLF 			"\r\n"
#define MAX_BACKLOG 		5

/* Testing shows that Windows only accepts dwFlags where the last
 * 3 (yes 3) bits define FTP_TRANSFER_TYPE_UNKNOWN, FTP_TRANSFER_TYPE_ASCII or FTP_TRANSFER_TYPE_BINARY.
 */
#define FTP_CONDITION_MASK      0x0007

typedef enum {
  /* FTP commands with arguments. */
  FTP_CMD_ACCT,
  FTP_CMD_CWD,
  FTP_CMD_DELE,
  FTP_CMD_MKD,
  FTP_CMD_PASS,
  FTP_CMD_PORT,
  FTP_CMD_RETR,
  FTP_CMD_RMD,
  FTP_CMD_RNFR,
  FTP_CMD_RNTO,
  FTP_CMD_STOR,
  FTP_CMD_TYPE,
  FTP_CMD_USER,
  FTP_CMD_SIZE,

  /* FTP commands without arguments. */
  FTP_CMD_ABOR,
  FTP_CMD_LIST,
  FTP_CMD_NLST,
  FTP_CMD_PASV,
  FTP_CMD_PWD,
  FTP_CMD_QUIT,
} FTP_COMMAND;

static const CHAR *const szFtpCommands[] = {
  "ACCT",
  "CWD",
  "DELE",
  "MKD",
  "PASS",
  "PORT",
  "RETR",
  "RMD",
  "RNFR",
  "RNTO",
  "STOR",
  "TYPE",
  "USER",
  "SIZE",
  "ABOR",
  "LIST",
  "NLST",
  "PASV",
  "PWD",
  "QUIT",
};

static const CHAR szMonths[] = "JANFEBMARAPRMAYJUNJULAUGSEPOCTNOVDEC";

static BOOL FTP_SendCommand(INT nSocket, FTP_COMMAND ftpCmd, LPCWSTR lpszParam,
	INTERNET_STATUS_CALLBACK lpfnStatusCB, object_header_t *hdr, DWORD_PTR dwContext);
static BOOL FTP_SendStore(ftp_session_t*, LPCWSTR lpszRemoteFile, DWORD dwType);
static BOOL FTP_GetDataSocket(ftp_session_t*, LPINT nDataSocket);
static BOOL FTP_SendData(ftp_session_t*, INT nDataSocket, HANDLE hFile);
static INT FTP_ReceiveResponse(ftp_session_t*, DWORD_PTR dwContext);
static BOOL FTP_SendRetrieve(ftp_session_t*, LPCWSTR lpszRemoteFile, DWORD dwType);
static BOOL FTP_RetrieveFileData(ftp_session_t*, INT nDataSocket, HANDLE hFile);
static BOOL FTP_InitListenSocket(ftp_session_t*);
static BOOL FTP_ConnectToHost(ftp_session_t*);
static BOOL FTP_SendPassword(ftp_session_t*);
static BOOL FTP_SendAccount(ftp_session_t*);
static BOOL FTP_SendType(ftp_session_t*, DWORD dwType);
static BOOL FTP_SendPort(ftp_session_t*);
static BOOL FTP_DoPassive(ftp_session_t*);
static BOOL FTP_SendPortOrPasv(ftp_session_t*);
static BOOL FTP_ParsePermission(LPCSTR lpszPermission, LPFILEPROPERTIESW lpfp);
#ifdef __REACTOS__
static DWORD FTP_ParseNextFile(INT nSocket, LPCWSTR lpszSearchFile, LPFILEPROPERTIESW fileprop);
#else
static BOOL FTP_ParseNextFile(INT nSocket, LPCWSTR lpszSearchFile, LPFILEPROPERTIESW fileprop);
#endif
static BOOL FTP_ParseDirectory(ftp_session_t*, INT nSocket, LPCWSTR lpszSearchFile,
        LPFILEPROPERTIESW *lpafp, LPDWORD dwfp);
static HINTERNET FTP_ReceiveFileList(ftp_session_t*, INT nSocket, LPCWSTR lpszSearchFile,
        LPWIN32_FIND_DATAW lpFindFileData, DWORD_PTR dwContext);
static DWORD FTP_SetResponseError(DWORD dwResponse);
static BOOL FTP_ConvertFileProp(LPFILEPROPERTIESW lpafp, LPWIN32_FIND_DATAW lpFindFileData);
static BOOL FTP_FtpPutFileW(ftp_session_t*, LPCWSTR lpszLocalFile,
        LPCWSTR lpszNewRemoteFile, DWORD dwFlags, DWORD_PTR dwContext);
static BOOL FTP_FtpSetCurrentDirectoryW(ftp_session_t*, LPCWSTR lpszDirectory);
static BOOL FTP_FtpCreateDirectoryW(ftp_session_t*, LPCWSTR lpszDirectory);
static HINTERNET FTP_FtpFindFirstFileW(ftp_session_t*,
#ifdef __REACTOS__
        LPCWSTR lpszSearchFile, void *lpFindFileData, DWORD dwFlags, DWORD_PTR dwContext, BOOL unicode);
static HINTERNET FTP_FindFirstFile(HINTERNET, LPCWSTR, void *, DWORD, DWORD_PTR, BOOL);
static BOOL FTP_FtpGetCurrentDirectoryW(ftp_session_t*, void *lpszCurrentDirectory,
        LPDWORD lpdwCurrentDirectory, BOOL unicode);
static BOOL FTP_GetCurrentDirectory(HINTERNET, void *, DWORD *, BOOL);
#else
        LPCWSTR lpszSearchFile, LPWIN32_FIND_DATAW lpFindFileData, DWORD dwFlags, DWORD_PTR dwContext);
static BOOL FTP_FtpGetCurrentDirectoryW(ftp_session_t*, LPWSTR lpszCurrentDirectory,
        LPDWORD lpdwCurrentDirectory);
#endif
static BOOL FTP_FtpRenameFileW(ftp_session_t*, LPCWSTR lpszSrc, LPCWSTR lpszDest);
static BOOL FTP_FtpRemoveDirectoryW(ftp_session_t*, LPCWSTR lpszDirectory);
static BOOL FTP_FtpDeleteFileW(ftp_session_t*, LPCWSTR lpszFileName);
static BOOL FTP_FtpGetFileW(ftp_session_t*, LPCWSTR lpszRemoteFile, LPCWSTR lpszNewFile,
        BOOL fFailIfExists, DWORD dwLocalFlagsAttribute, DWORD dwInternetFlags,
        DWORD_PTR dwContext);

/* A temporary helper until we get rid of INTERNET_GetLastError calls */
static BOOL res_to_le(DWORD res)
{
    if(res != ERROR_SUCCESS)
        INTERNET_SetLastError(res);
    return res == ERROR_SUCCESS;
}

/***********************************************************************
 *           FtpPutFileA (WININET.@)
 *
 * Uploads a file to the FTP server
 *
 * RETURNS
 *    TRUE on success
 *    FALSE on failure
 *
 */
BOOL WINAPI FtpPutFileA(HINTERNET hConnect, LPCSTR lpszLocalFile,
    LPCSTR lpszNewRemoteFile, DWORD dwFlags, DWORD_PTR dwContext)
{
#ifdef __REACTOS__
    LPWSTR lpwzLocalFile = NULL;
    LPWSTR lpwzNewRemoteFile = NULL;
    BOOL ret = FALSE;
    DWORD error;
#else
    LPWSTR lpwzLocalFile;
    LPWSTR lpwzNewRemoteFile;
    BOOL ret;
#endif
    
#ifdef __REACTOS__
    if (lpszLocalFile && !(lpwzLocalFile = strdupAtoW(lpszLocalFile))) goto done;
    if (lpszNewRemoteFile && !(lpwzNewRemoteFile = strdupAtoW(lpszNewRemoteFile))) goto done;
#else
    lpwzLocalFile = strdupAtoW(lpszLocalFile);
    lpwzNewRemoteFile = strdupAtoW(lpszNewRemoteFile);
#endif
    ret = FtpPutFileW(hConnect, lpwzLocalFile, lpwzNewRemoteFile,
                      dwFlags, dwContext);
#ifdef __REACTOS__
done:
    error = GetLastError();
#endif
    free(lpwzLocalFile);
    free(lpwzNewRemoteFile);
#ifdef __REACTOS__
    if (!ret) INTERNET_SetLastError(error);
#endif
    return ret;
}

typedef struct {
    task_header_t hdr;
    WCHAR *local_file;
    WCHAR *remote_file;
    DWORD flags;
    DWORD_PTR context;
} put_file_task_t;

static void AsyncFtpPutFileProc(task_header_t *hdr)
{
    put_file_task_t *task = (put_file_task_t*)hdr;
    ftp_session_t *session = (ftp_session_t*)task->hdr.hdr;

    TRACE("%p\n", session);

    FTP_FtpPutFileW(session, task->local_file, task->remote_file,
               task->flags, task->context);

#ifdef __REACTOS__
}

static void FreeFtpPutFileTask(task_header_t *hdr)
{
    put_file_task_t *task = (put_file_task_t*)hdr;

#endif
    free(task->local_file);
    free(task->remote_file);
}

/***********************************************************************
 *           FtpPutFileW (WININET.@)
 *
 * Uploads a file to the FTP server
 *
 * RETURNS
 *    TRUE on success
 *    FALSE on failure
 *
 */
BOOL WINAPI FtpPutFileW(HINTERNET hConnect, LPCWSTR lpszLocalFile,
    LPCWSTR lpszNewRemoteFile, DWORD dwFlags, DWORD_PTR dwContext)
{
    ftp_session_t *lpwfs;
    appinfo_t *hIC = NULL;
    BOOL r = FALSE;
#ifdef __REACTOS__
    DWORD error;
#endif

    if (!lpszLocalFile || !lpszNewRemoteFile)
    {
        INTERNET_SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    lpwfs = (ftp_session_t*) get_handle_object( hConnect );
    if (!lpwfs)
    {
        INTERNET_SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }

    if (WH_HFTPSESSION != lpwfs->hdr.htype)
    {
        INTERNET_SetLastError(ERROR_INTERNET_INCORRECT_HANDLE_TYPE);
        goto lend;
    }

    if (lpwfs->download_in_progress != NULL)
    {
        INTERNET_SetLastError(ERROR_FTP_TRANSFER_IN_PROGRESS);
        goto lend;
    }

    if ((dwFlags & FTP_CONDITION_MASK) > FTP_TRANSFER_TYPE_BINARY)
    {
        INTERNET_SetLastError(ERROR_INVALID_PARAMETER);
        goto lend;
    }

    hIC = lpwfs->lpAppInfo;
    if (hIC->hdr.dwFlags & INTERNET_FLAG_ASYNC)
    {
        put_file_task_t *task = alloc_async_task(&lpwfs->hdr, AsyncFtpPutFileProc, sizeof(*task));
#ifdef __REACTOS__
        if (!task)
        {
            INTERNET_SetLastError(ERROR_OUTOFMEMORY);
            goto lend;
        }
        task->hdr.destroy = FreeFtpPutFileTask;
#endif

        task->local_file = wcsdup(lpszLocalFile);
        task->remote_file = wcsdup(lpszNewRemoteFile);
#ifdef __REACTOS__
        if (!task->local_file || !task->remote_file)
        {
            free_async_task(&task->hdr);
            INTERNET_SetLastError(ERROR_OUTOFMEMORY);
            goto lend;
        }
#endif
        task->flags = dwFlags;
        task->context = dwContext;

        r = res_to_le(INTERNET_AsyncCall(&task->hdr));
    }
    else
    {
        r = FTP_FtpPutFileW(lpwfs, lpszLocalFile,
	    lpszNewRemoteFile, dwFlags, dwContext);
    }

lend:
#ifdef __REACTOS__
    error = r ? ERROR_SUCCESS : INTERNET_GetLastError();
#endif
    WININET_Release( &lpwfs->hdr );
#ifdef __REACTOS__
    if (!r)
        INTERNET_SetLastError(error);
#endif

    return r;
}

/***********************************************************************
 *           FTP_FtpPutFileW (Internal)
 *
 * Uploads a file to the FTP server
 *
 * RETURNS
 *    TRUE on success
 *    FALSE on failure
 *
 */
static BOOL FTP_FtpPutFileW(ftp_session_t *lpwfs, LPCWSTR lpszLocalFile,
    LPCWSTR lpszNewRemoteFile, DWORD dwFlags, DWORD_PTR dwContext)
{
    HANDLE hFile;
    BOOL bSuccess = FALSE;
#ifdef __REACTOS__
    appinfo_t *hIC = lpwfs->lpAppInfo;
#else
    appinfo_t *hIC = NULL;
#endif
    INT nResCode;
#ifdef __REACTOS__
    DWORD result_error;
#endif

    TRACE(" lpszLocalFile(%s) lpszNewRemoteFile(%s)\n", debugstr_w(lpszLocalFile), debugstr_w(lpszNewRemoteFile));

    /* Clear any error information */
    INTERNET_SetLastError(0);

    /* Open file to be uploaded */
    if (INVALID_HANDLE_VALUE ==
        (hFile = CreateFileW(lpszLocalFile, GENERIC_READ, 0, 0, OPEN_EXISTING, 0, 0)))
#ifdef __REACTOS__
    {
#endif
        /* Let CreateFile set the appropriate error */
#ifdef __REACTOS__
        INTERNET_SetLastError(GetLastError());
        goto done;
    }
#else
        return FALSE;

    hIC = lpwfs->lpAppInfo;
#endif

    INTERNET_SendCallback(&lpwfs->hdr, lpwfs->hdr.dwContext, INTERNET_STATUS_SENDING_REQUEST, NULL, 0);

    if (FTP_SendStore(lpwfs, lpszNewRemoteFile, dwFlags))
    {
        INT nDataSocket;

        /* Get data socket to server */
        if (FTP_GetDataSocket(lpwfs, &nDataSocket))
        {
#ifdef __REACTOS__
            BOOL transferred = FTP_SendData(lpwfs, nDataSocket, hFile);
            DWORD error = transferred ? ERROR_SUCCESS : INTERNET_GetLastError();

#else
            FTP_SendData(lpwfs, nDataSocket, hFile);
#endif
            closesocket(nDataSocket);
	    nResCode = FTP_ReceiveResponse(lpwfs, dwContext);
#ifdef __REACTOS__
            if (!transferred)
                INTERNET_SetLastError(error);
	    else if (nResCode)
#else
	    if (nResCode)
#endif
	    {
	        if (nResCode == 226)
		    bSuccess = TRUE;
		else
		    FTP_SetResponseError(nResCode);
	    }
        }
    }

    if (lpwfs->lstnSocket != -1)
    {
        closesocket(lpwfs->lstnSocket);
        lpwfs->lstnSocket = -1;
    }

#ifdef __REACTOS__
done:
    result_error = bSuccess ? ERROR_SUCCESS : INTERNET_GetLastError();
    if (!bSuccess && !result_error)
        result_error = ERROR_INTERNET_EXTENDED_ERROR;
#endif
    if (hIC->hdr.dwFlags & INTERNET_FLAG_ASYNC)
    {
        INTERNET_ASYNC_RESULT iar;

        iar.dwResult = (DWORD)bSuccess;
#ifdef __REACTOS__
        iar.dwError = result_error;
#else
        iar.dwError = bSuccess ? ERROR_SUCCESS : INTERNET_GetLastError();
#endif
        INTERNET_SendCallback(&lpwfs->hdr, lpwfs->hdr.dwContext, INTERNET_STATUS_REQUEST_COMPLETE,
            &iar, sizeof(INTERNET_ASYNC_RESULT));
    }

#ifdef __REACTOS__
    if (hFile != INVALID_HANDLE_VALUE)
        CloseHandle(hFile);
    if (!bSuccess)
        INTERNET_SetLastError(result_error);
#else
    CloseHandle(hFile);
#endif

    return bSuccess;
}


/***********************************************************************
 *           FtpSetCurrentDirectoryA (WININET.@)
 *
 * Change the working directory on the FTP server
 *
 * RETURNS
 *    TRUE on success
 *    FALSE on failure
 *
 */
BOOL WINAPI FtpSetCurrentDirectoryA(HINTERNET hConnect, LPCSTR lpszDirectory)
{
    LPWSTR lpwzDirectory;
    BOOL ret;

    lpwzDirectory = strdupAtoW(lpszDirectory);
#ifdef __REACTOS__
    if (lpszDirectory && !lpwzDirectory)
    {
        INTERNET_SetLastError(GetLastError());
        return FALSE;
    }
#endif
    ret = FtpSetCurrentDirectoryW(hConnect, lpwzDirectory);
    free(lpwzDirectory);
    return ret;
}

typedef struct {
    task_header_t hdr;
    WCHAR *directory;
} directory_task_t;

static void AsyncFtpSetCurrentDirectoryProc(task_header_t *hdr)
{
    directory_task_t *task = (directory_task_t*)hdr;
    ftp_session_t *session = (ftp_session_t*)task->hdr.hdr;

    TRACE("%p\n", session);

    FTP_FtpSetCurrentDirectoryW(session, task->directory);
#ifdef __REACTOS__
}

static void FreeFtpDirectoryTask(task_header_t *hdr)
{
    directory_task_t *task = (directory_task_t*)hdr;

#endif
    free(task->directory);
}

/***********************************************************************
 *           FtpSetCurrentDirectoryW (WININET.@)
 *
 * Change the working directory on the FTP server
 *
 * RETURNS
 *    TRUE on success
 *    FALSE on failure
 *
 */
BOOL WINAPI FtpSetCurrentDirectoryW(HINTERNET hConnect, LPCWSTR lpszDirectory)
{
    ftp_session_t *lpwfs = NULL;
    appinfo_t *hIC = NULL;
    BOOL r = FALSE;

    if (!lpszDirectory)
    {
        INTERNET_SetLastError(ERROR_INVALID_PARAMETER);
        goto lend;
    }

    lpwfs = (ftp_session_t*) get_handle_object( hConnect );
    if (NULL == lpwfs || WH_HFTPSESSION != lpwfs->hdr.htype)
    {
        INTERNET_SetLastError(ERROR_INTERNET_INCORRECT_HANDLE_TYPE);
        goto lend;
    }

    if (lpwfs->download_in_progress != NULL)
    {
        INTERNET_SetLastError(ERROR_FTP_TRANSFER_IN_PROGRESS);
        goto lend;
    }

    TRACE("lpszDirectory(%s)\n", debugstr_w(lpszDirectory));

    hIC = lpwfs->lpAppInfo;
    if (hIC->hdr.dwFlags & INTERNET_FLAG_ASYNC)
    {
        directory_task_t *task;

        task = alloc_async_task(&lpwfs->hdr, AsyncFtpSetCurrentDirectoryProc, sizeof(*task));
#ifdef __REACTOS__
        if (!task)
        {
            INTERNET_SetLastError(ERROR_OUTOFMEMORY);
            goto lend;
        }
        task->hdr.destroy = FreeFtpDirectoryTask;
#endif
        task->directory = wcsdup(lpszDirectory);
#ifdef __REACTOS__
        if (!task->directory)
        {
            free_async_task(&task->hdr);
            INTERNET_SetLastError(ERROR_OUTOFMEMORY);
            goto lend;
        }
#endif

        r = res_to_le(INTERNET_AsyncCall(&task->hdr));
    }
    else
    {
        r = FTP_FtpSetCurrentDirectoryW(lpwfs, lpszDirectory);
    }

lend:
    if( lpwfs )
        WININET_Release( &lpwfs->hdr );

    return r;
}


/***********************************************************************
 *           FTP_FtpSetCurrentDirectoryW (Internal)
 *
 * Change the working directory on the FTP server
 *
 * RETURNS
 *    TRUE on success
 *    FALSE on failure
 *
 */
static BOOL FTP_FtpSetCurrentDirectoryW(ftp_session_t *lpwfs, LPCWSTR lpszDirectory)
{
    INT nResCode;
    appinfo_t *hIC = NULL;
    BOOL bSuccess = FALSE;

    TRACE("lpszDirectory(%s)\n", debugstr_w(lpszDirectory));

    /* Clear any error information */
    INTERNET_SetLastError(0);

    hIC = lpwfs->lpAppInfo;
    if (!FTP_SendCommand(lpwfs->sndSocket, FTP_CMD_CWD, lpszDirectory,
        lpwfs->hdr.lpfnStatusCB, &lpwfs->hdr, lpwfs->hdr.dwContext))
        goto lend;

    nResCode = FTP_ReceiveResponse(lpwfs, lpwfs->hdr.dwContext);

    if (nResCode)
    {
        if (nResCode == 250)
            bSuccess = TRUE;
        else
            FTP_SetResponseError(nResCode);
    }

lend:
    if (hIC->hdr.dwFlags & INTERNET_FLAG_ASYNC)
    {
        INTERNET_ASYNC_RESULT iar;

        iar.dwResult = bSuccess;
        iar.dwError = bSuccess ? ERROR_SUCCESS : ERROR_INTERNET_EXTENDED_ERROR;
        INTERNET_SendCallback(&lpwfs->hdr, lpwfs->hdr.dwContext, INTERNET_STATUS_REQUEST_COMPLETE,
            &iar, sizeof(INTERNET_ASYNC_RESULT));
    }
    return bSuccess;
}


/***********************************************************************
 *           FtpCreateDirectoryA (WININET.@)
 *
 * Create new directory on the FTP server
 *
 * RETURNS
 *    TRUE on success
 *    FALSE on failure
 *
 */
BOOL WINAPI FtpCreateDirectoryA(HINTERNET hConnect, LPCSTR lpszDirectory)
{
    LPWSTR lpwzDirectory;
    BOOL ret;

    lpwzDirectory = strdupAtoW(lpszDirectory);
#ifdef __REACTOS__
    if (lpszDirectory && !lpwzDirectory)
    {
        INTERNET_SetLastError(GetLastError());
        return FALSE;
    }
#endif
    ret = FtpCreateDirectoryW(hConnect, lpwzDirectory);
    free(lpwzDirectory);
    return ret;
}


static void AsyncFtpCreateDirectoryProc(task_header_t *hdr)
{
    directory_task_t *task = (directory_task_t*)hdr;
    ftp_session_t *session = (ftp_session_t*)task->hdr.hdr;

    TRACE(" %p\n", session);

    FTP_FtpCreateDirectoryW(session, task->directory);
#ifndef __REACTOS__
    free(task->directory);
#endif
}

/***********************************************************************
 *           FtpCreateDirectoryW (WININET.@)
 *
 * Create new directory on the FTP server
 *
 * RETURNS
 *    TRUE on success
 *    FALSE on failure
 *
 */
BOOL WINAPI FtpCreateDirectoryW(HINTERNET hConnect, LPCWSTR lpszDirectory)
{
    ftp_session_t *lpwfs;
    appinfo_t *hIC = NULL;
    BOOL r = FALSE;

    lpwfs = (ftp_session_t*) get_handle_object( hConnect );
    if (!lpwfs)
    {
        INTERNET_SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }

    if (WH_HFTPSESSION != lpwfs->hdr.htype)
    {
        INTERNET_SetLastError(ERROR_INTERNET_INCORRECT_HANDLE_TYPE);
        goto lend;
    }

    if (lpwfs->download_in_progress != NULL)
    {
        INTERNET_SetLastError(ERROR_FTP_TRANSFER_IN_PROGRESS);
        goto lend;
    }

    if (!lpszDirectory)
    {
        INTERNET_SetLastError(ERROR_INVALID_PARAMETER);
        goto lend;
    }

    hIC = lpwfs->lpAppInfo;
    if (hIC->hdr.dwFlags & INTERNET_FLAG_ASYNC)
    {
        directory_task_t *task;

        task = alloc_async_task(&lpwfs->hdr, AsyncFtpCreateDirectoryProc, sizeof(*task));
#ifdef __REACTOS__
        if (!task)
        {
            INTERNET_SetLastError(ERROR_OUTOFMEMORY);
            goto lend;
        }
        task->hdr.destroy = FreeFtpDirectoryTask;
#endif
        task->directory = wcsdup(lpszDirectory);
#ifdef __REACTOS__
        if (!task->directory)
        {
            free_async_task(&task->hdr);
            INTERNET_SetLastError(ERROR_OUTOFMEMORY);
            goto lend;
        }
#endif

        r = res_to_le(INTERNET_AsyncCall(&task->hdr));
    }
    else
    {
        r = FTP_FtpCreateDirectoryW(lpwfs, lpszDirectory);
    }
lend:
    WININET_Release( &lpwfs->hdr );

    return r;
}


/***********************************************************************
 *           FTP_FtpCreateDirectoryW (Internal)
 *
 * Create new directory on the FTP server
 *
 * RETURNS
 *    TRUE on success
 *    FALSE on failure
 *
 */
static BOOL FTP_FtpCreateDirectoryW(ftp_session_t *lpwfs, LPCWSTR lpszDirectory)
{
    INT nResCode;
    BOOL bSuccess = FALSE;
    appinfo_t *hIC = NULL;

    TRACE("lpszDirectory(%s)\n", debugstr_w(lpszDirectory));

    /* Clear any error information */
    INTERNET_SetLastError(0);

    if (!FTP_SendCommand(lpwfs->sndSocket, FTP_CMD_MKD, lpszDirectory, 0, 0, 0))
        goto lend;

    nResCode = FTP_ReceiveResponse(lpwfs, lpwfs->hdr.dwContext);
    if (nResCode)
    {
        if (nResCode == 257)
            bSuccess = TRUE;
        else
            FTP_SetResponseError(nResCode);
    }

lend:
    hIC = lpwfs->lpAppInfo;
    if (hIC->hdr.dwFlags & INTERNET_FLAG_ASYNC)
    {
        INTERNET_ASYNC_RESULT iar;

        iar.dwResult = (DWORD)bSuccess;
        iar.dwError = bSuccess ? ERROR_SUCCESS : INTERNET_GetLastError();
        INTERNET_SendCallback(&lpwfs->hdr, lpwfs->hdr.dwContext, INTERNET_STATUS_REQUEST_COMPLETE,
            &iar, sizeof(INTERNET_ASYNC_RESULT));
    }

    return bSuccess;
}

/***********************************************************************
 *           FtpFindFirstFileA (WININET.@)
 *
 * Search the specified directory
 *
 * RETURNS
 *    HINTERNET on success
 *    NULL on failure
 *
 */
HINTERNET WINAPI FtpFindFirstFileA(HINTERNET hConnect,
    LPCSTR lpszSearchFile, LPWIN32_FIND_DATAA lpFindFileData, DWORD dwFlags, DWORD_PTR dwContext)
{
    LPWSTR lpwzSearchFile;
#ifndef __REACTOS__
    WIN32_FIND_DATAW wfd;
    LPWIN32_FIND_DATAW lpFindFileDataW;
#endif
    HINTERNET ret;

    lpwzSearchFile = strdupAtoW(lpszSearchFile);
#ifdef __REACTOS__
    if (lpszSearchFile && !lpwzSearchFile)
    {
        INTERNET_SetLastError(ERROR_OUTOFMEMORY);
        return NULL;
    }
    ret = FTP_FindFirstFile(hConnect, lpwzSearchFile, lpFindFileData, dwFlags, dwContext, FALSE);
#else
    lpFindFileDataW = lpFindFileData?&wfd:NULL;
    ret = FtpFindFirstFileW(hConnect, lpwzSearchFile, lpFindFileDataW, dwFlags, dwContext);
#endif
    free(lpwzSearchFile);

#ifndef __REACTOS__
    if (ret && lpFindFileData)
        WININET_find_data_WtoA(lpFindFileDataW, lpFindFileData);

#endif
    return ret;
}

typedef struct {
    task_header_t hdr;
    WCHAR *search_file;
#ifdef __REACTOS__
    void *find_file_data;
#else
    WIN32_FIND_DATAW *find_file_data;
#endif
    DWORD flags;
    DWORD_PTR context;
#ifdef __REACTOS__
    BOOL unicode;
#endif
} find_first_file_task_t;

static void AsyncFtpFindFirstFileProc(task_header_t *hdr)
{
    find_first_file_task_t *task = (find_first_file_task_t*)hdr;
    ftp_session_t *session = (ftp_session_t*)task->hdr.hdr;

    TRACE("%p\n", session);

#ifdef __REACTOS__
    FTP_FtpFindFirstFileW(session, task->search_file, task->find_file_data, task->flags, task->context,
                        task->unicode);
}

static void FreeFtpFindFirstFileTask(task_header_t *hdr)
{
    find_first_file_task_t *task = (find_first_file_task_t*)hdr;

#else
    FTP_FtpFindFirstFileW(session, task->search_file, task->find_file_data, task->flags, task->context);
#endif
    free(task->search_file);
}

/***********************************************************************
 *           FtpFindFirstFileW (WININET.@)
 *
 * Search the specified directory
 *
 * RETURNS
 *    HINTERNET on success
 *    NULL on failure
 *
 */
HINTERNET WINAPI FtpFindFirstFileW(HINTERNET hConnect,
    LPCWSTR lpszSearchFile, LPWIN32_FIND_DATAW lpFindFileData, DWORD dwFlags, DWORD_PTR dwContext)
#ifdef __REACTOS__
{
    return FTP_FindFirstFile(hConnect, lpszSearchFile, lpFindFileData, dwFlags, dwContext, TRUE);
}

static HINTERNET FTP_FindFirstFile(HINTERNET hConnect, LPCWSTR lpszSearchFile, void *lpFindFileData,
                                 DWORD dwFlags, DWORD_PTR dwContext, BOOL unicode)
#endif
{
    ftp_session_t *lpwfs;
    appinfo_t *hIC = NULL;
    HINTERNET r = NULL;
#ifdef __REACTOS__
    DWORD error;
#endif

    lpwfs = (ftp_session_t*) get_handle_object( hConnect );
    if (NULL == lpwfs || WH_HFTPSESSION != lpwfs->hdr.htype)
    {
        INTERNET_SetLastError(ERROR_INTERNET_INCORRECT_HANDLE_TYPE);
        goto lend;
    }

    if (lpwfs->download_in_progress != NULL)
    {
        INTERNET_SetLastError(ERROR_FTP_TRANSFER_IN_PROGRESS);
        goto lend;
    }

    hIC = lpwfs->lpAppInfo;
    if (hIC->hdr.dwFlags & INTERNET_FLAG_ASYNC)
    {
        find_first_file_task_t *task;
#ifdef __REACTOS__
        DWORD res;
#endif

        task = alloc_async_task(&lpwfs->hdr, AsyncFtpFindFirstFileProc, sizeof(*task));
#ifdef __REACTOS__
        if (!task)
        {
            INTERNET_SetLastError(ERROR_OUTOFMEMORY);
            goto lend;
        }
        task->hdr.destroy = FreeFtpFindFirstFileTask;
        task->search_file = lpszSearchFile ? wcsdup(lpszSearchFile) : NULL;
        if (lpszSearchFile && !task->search_file)
        {
            free_async_task(&task->hdr);
            INTERNET_SetLastError(ERROR_OUTOFMEMORY);
            goto lend;
        }
#else
        task->search_file = wcsdup(lpszSearchFile);
#endif
        task->find_file_data = lpFindFileData;
        task->flags = dwFlags;
        task->context = dwContext;
#ifdef __REACTOS__
        task->unicode = unicode;
#endif

#ifdef __REACTOS__
        res = INTERNET_AsyncCall(&task->hdr);
        INTERNET_SetLastError(res);
#else
        INTERNET_AsyncCall(&task->hdr);
#endif
        r = NULL;
    }
    else
    {
        r = FTP_FtpFindFirstFileW(lpwfs, lpszSearchFile, lpFindFileData,
#ifdef __REACTOS__
            dwFlags, dwContext, unicode);
#else
            dwFlags, dwContext);
#endif
    }
lend:
#ifdef __REACTOS__
    error = INTERNET_GetLastError();
#endif
    if( lpwfs )
        WININET_Release( &lpwfs->hdr );
#ifdef __REACTOS__
    if (!r) INTERNET_SetLastError(error);
#endif

    return r;
}


/***********************************************************************
 *           FTP_FtpFindFirstFileW (Internal)
 *
 * Search the specified directory
 *
 * RETURNS
 *    HINTERNET on success
 *    NULL on failure
 *
 */
static HINTERNET FTP_FtpFindFirstFileW(ftp_session_t *lpwfs,
#ifdef __REACTOS__
    LPCWSTR lpszSearchFile, void *lpFindFileData, DWORD dwFlags, DWORD_PTR dwContext, BOOL unicode)
#else
    LPCWSTR lpszSearchFile, LPWIN32_FIND_DATAW lpFindFileData, DWORD dwFlags, DWORD_PTR dwContext)
#endif
{
    INT nResCode;
    appinfo_t *hIC = NULL;
    HINTERNET hFindNext = NULL;
    LPWSTR lpszSearchPath = NULL;
#ifdef __REACTOS__
    WIN32_FIND_DATAW find_data;
#endif

    TRACE("\n");

    /* Clear any error information */
    INTERNET_SetLastError(0);

    if (!FTP_InitListenSocket(lpwfs))
        goto lend;

    if (!FTP_SendType(lpwfs, INTERNET_FLAG_TRANSFER_ASCII))
        goto lend;

    if (!FTP_SendPortOrPasv(lpwfs))
        goto lend;

    /* split search path into file and path */
    if (lpszSearchFile)
    {
        LPCWSTR name = lpszSearchFile, p;
        if ((p = wcsrchr( name, '\\' ))) name = p + 1;
        if ((p = wcsrchr( name, '/' ))) name = p + 1;
        if (name != lpszSearchFile)
        {
            lpszSearchPath = strndupW(lpszSearchFile, name - lpszSearchFile);
#ifdef __REACTOS__
            if (!lpszSearchPath)
            {
                INTERNET_SetLastError(ERROR_OUTOFMEMORY);
                goto lend;
            }
#endif
            lpszSearchFile = name;
        }
    }

    if (!FTP_SendCommand(lpwfs->sndSocket, FTP_CMD_LIST, lpszSearchPath,
        lpwfs->hdr.lpfnStatusCB, &lpwfs->hdr, lpwfs->hdr.dwContext))
        goto lend;

    nResCode = FTP_ReceiveResponse(lpwfs, lpwfs->hdr.dwContext);
    if (nResCode)
    {
        if (nResCode == 125 || nResCode == 150)
        {
            INT nDataSocket;

            /* Get data socket to server */
            if (FTP_GetDataSocket(lpwfs, &nDataSocket))
            {
#ifdef __REACTOS__
                DWORD list_error;

                hFindNext = FTP_ReceiveFileList(lpwfs, nDataSocket, lpszSearchFile,
                                              unicode || !lpFindFileData ? lpFindFileData : &find_data,
                                              dwContext);
                list_error = hFindNext ? ERROR_SUCCESS : INTERNET_GetLastError();
                if (hFindNext && lpFindFileData && !unicode)
                    WININET_find_data_WtoA(&find_data, lpFindFileData);
#else
                hFindNext = FTP_ReceiveFileList(lpwfs, nDataSocket, lpszSearchFile, lpFindFileData, dwContext);
#endif
                closesocket(nDataSocket);
                nResCode = FTP_ReceiveResponse(lpwfs, lpwfs->hdr.dwContext);
#ifdef __REACTOS__
                if (!hFindNext)
                    INTERNET_SetLastError(list_error);
                else if (nResCode != 226 && nResCode != 250)
#else
                if (nResCode != 226 && nResCode != 250)
#endif
                    INTERNET_SetLastError(ERROR_NO_MORE_FILES);
            }
        }
        else
            FTP_SetResponseError(nResCode);
    }

lend:
    free(lpszSearchPath);

    if (lpwfs->lstnSocket != -1)
    {
        closesocket(lpwfs->lstnSocket);
        lpwfs->lstnSocket = -1;
    }

    hIC = lpwfs->lpAppInfo;
    if (hIC->hdr.dwFlags & INTERNET_FLAG_ASYNC)
    {
        INTERNET_ASYNC_RESULT iar;

        if (hFindNext)
	{
            iar.dwResult = (DWORD_PTR)hFindNext;
            iar.dwError = ERROR_SUCCESS;
            INTERNET_SendCallback(&lpwfs->hdr, lpwfs->hdr.dwContext, INTERNET_STATUS_HANDLE_CREATED,
                &iar, sizeof(INTERNET_ASYNC_RESULT));
	}

        iar.dwResult = (DWORD_PTR)hFindNext;
        iar.dwError = hFindNext ? ERROR_SUCCESS : INTERNET_GetLastError();
        INTERNET_SendCallback(&lpwfs->hdr, lpwfs->hdr.dwContext, INTERNET_STATUS_REQUEST_COMPLETE,
            &iar, sizeof(INTERNET_ASYNC_RESULT));
    }

    return hFindNext;
}


/***********************************************************************
 *           FtpGetCurrentDirectoryA (WININET.@)
 *
 * Retrieves the current directory
 *
 * RETURNS
 *    TRUE on success
 *    FALSE on failure
 *
 */
BOOL WINAPI FtpGetCurrentDirectoryA(HINTERNET hFtpSession, LPSTR lpszCurrentDirectory,
    LPDWORD lpdwCurrentDirectory)
{
#ifdef __REACTOS__
    return FTP_GetCurrentDirectory(hFtpSession, lpszCurrentDirectory, lpdwCurrentDirectory, FALSE);
#else
    WCHAR *dir = NULL;
    DWORD len;
    BOOL ret;

    if(lpdwCurrentDirectory) {
        len = *lpdwCurrentDirectory;
        if(lpszCurrentDirectory)
        {
            dir = malloc(len * sizeof(WCHAR));
            if (NULL == dir)
            {
                INTERNET_SetLastError(ERROR_OUTOFMEMORY);
                return FALSE;
            }
        }
    }
    ret = FtpGetCurrentDirectoryW(hFtpSession, lpszCurrentDirectory?dir:NULL, lpdwCurrentDirectory?&len:NULL);

    if (ret && lpszCurrentDirectory)
        WideCharToMultiByte(CP_ACP, 0, dir, -1, lpszCurrentDirectory, len, NULL, NULL);

    if (lpdwCurrentDirectory) *lpdwCurrentDirectory = len;
    free(dir);
    return ret;
#endif
}

typedef struct {
    task_header_t hdr;
#ifdef __REACTOS__
    void *directory;
#else
    WCHAR *directory;
#endif
    DWORD *directory_len;
#ifdef __REACTOS__
    BOOL unicode;
#endif
} get_current_dir_task_t;

static void AsyncFtpGetCurrentDirectoryProc(task_header_t *hdr)
{
    get_current_dir_task_t *task = (get_current_dir_task_t*)hdr;
    ftp_session_t *session = (ftp_session_t*)task->hdr.hdr;

    TRACE("%p\n", session);

#ifdef __REACTOS__
    FTP_FtpGetCurrentDirectoryW(session, task->directory, task->directory_len, task->unicode);
#else
    FTP_FtpGetCurrentDirectoryW(session, task->directory, task->directory_len);
#endif
}

/***********************************************************************
 *           FtpGetCurrentDirectoryW (WININET.@)
 *
 * Retrieves the current directory
 *
 * RETURNS
 *    TRUE on success
 *    FALSE on failure
 *
 */
BOOL WINAPI FtpGetCurrentDirectoryW(HINTERNET hFtpSession, LPWSTR lpszCurrentDirectory,
    LPDWORD lpdwCurrentDirectory)
#ifdef __REACTOS__
{
    return FTP_GetCurrentDirectory(hFtpSession, lpszCurrentDirectory, lpdwCurrentDirectory, TRUE);
}

static BOOL FTP_GetCurrentDirectory(HINTERNET hFtpSession, void *lpszCurrentDirectory,
                                   DWORD *lpdwCurrentDirectory, BOOL unicode)
#endif
{
    ftp_session_t *lpwfs;
    appinfo_t *hIC = NULL;
    BOOL r = FALSE;

    TRACE("%p %p %p\n", hFtpSession, lpszCurrentDirectory, lpdwCurrentDirectory);

    lpwfs = (ftp_session_t*) get_handle_object( hFtpSession );
    if (NULL == lpwfs)
    {
        INTERNET_SetLastError(ERROR_INVALID_HANDLE);
        goto lend;
    }

    if (WH_HFTPSESSION != lpwfs->hdr.htype)
    {
        INTERNET_SetLastError(ERROR_INTERNET_INCORRECT_HANDLE_TYPE);
        goto lend;
    }

    if (!lpdwCurrentDirectory)
    {
        INTERNET_SetLastError(ERROR_INVALID_PARAMETER);
        goto lend;
    }

    if (lpszCurrentDirectory == NULL)
    {
        INTERNET_SetLastError(ERROR_INSUFFICIENT_BUFFER);
        goto lend;
    }

    if (lpwfs->download_in_progress != NULL)
    {
        INTERNET_SetLastError(ERROR_FTP_TRANSFER_IN_PROGRESS);
        goto lend;
    }

    hIC = lpwfs->lpAppInfo;
    if (hIC->hdr.dwFlags & INTERNET_FLAG_ASYNC)
    {
        get_current_dir_task_t *task;

        task = alloc_async_task(&lpwfs->hdr, AsyncFtpGetCurrentDirectoryProc, sizeof(*task));
#ifdef __REACTOS__
        if (!task)
        {
            INTERNET_SetLastError(ERROR_OUTOFMEMORY);
            goto lend;
        }
#endif
        task->directory = lpszCurrentDirectory;
        task->directory_len = lpdwCurrentDirectory;
#ifdef __REACTOS__
        task->unicode = unicode;
#endif

        r = res_to_le(INTERNET_AsyncCall(&task->hdr));
    }
    else
    {
        r = FTP_FtpGetCurrentDirectoryW(lpwfs, lpszCurrentDirectory,
#ifdef __REACTOS__
            lpdwCurrentDirectory, unicode);
#else
            lpdwCurrentDirectory);
#endif
    }

lend:
    if( lpwfs )
        WININET_Release( &lpwfs->hdr );

    return r;
}


/***********************************************************************
 *           FTP_FtpGetCurrentDirectoryW (Internal)
 *
 * Retrieves the current directory
 *
 * RETURNS
 *    TRUE on success
 *    FALSE on failure
 *
 */
#ifdef __REACTOS__
static BOOL FTP_FtpGetCurrentDirectoryW(ftp_session_t *lpwfs, void *lpszCurrentDirectory,
        LPDWORD lpdwCurrentDirectory, BOOL unicode)
#else
static BOOL FTP_FtpGetCurrentDirectoryW(ftp_session_t *lpwfs, LPWSTR lpszCurrentDirectory,
	LPDWORD lpdwCurrentDirectory)
#endif
{
    INT nResCode;
    appinfo_t *hIC = NULL;
    BOOL bSuccess = FALSE;

    /* Clear any error information */
    INTERNET_SetLastError(0);

    hIC = lpwfs->lpAppInfo;
    if (!FTP_SendCommand(lpwfs->sndSocket, FTP_CMD_PWD, NULL,
        lpwfs->hdr.lpfnStatusCB, &lpwfs->hdr, lpwfs->hdr.dwContext))
        goto lend;

    nResCode = FTP_ReceiveResponse(lpwfs, lpwfs->hdr.dwContext);
    if (nResCode)
    {
        if (nResCode == 257) /* Extract directory name */
        {
#ifdef __REACTOS__
            DWORD len;
#else
            DWORD firstpos, lastpos, len;
#endif
            WCHAR *lpszResponseBuffer = strdupAtoW(INTERNET_GetResponseBuffer());
#ifdef __REACTOS__
            WCHAR *first, *last;
#endif

#ifdef __REACTOS__
            if (!lpszResponseBuffer)
            {
                INTERNET_SetLastError(ERROR_OUTOFMEMORY);
                goto lend;
            }

            first = wcschr(lpszResponseBuffer, '"');
            last = first ? wcschr(first + 1, '"') : NULL;
            if (!last)
            {
                free(lpszResponseBuffer);
                INTERNET_SetLastError(ERROR_INTERNET_EXTENDED_ERROR);
                goto lend;
            }
            *last = 0;
            first++;
            len = unicode ? last - first + 1 : WideCharToMultiByte(CP_ACP, 0, first, -1, NULL, 0, NULL, NULL);
            if (!len)
#else
            for (firstpos = 0, lastpos = 0; lpszResponseBuffer[lastpos]; lastpos++)
#endif
            {
#ifdef __REACTOS__
                INTERNET_SetLastError(GetLastError());
                free(lpszResponseBuffer);
                goto lend;
            }
            if (*lpdwCurrentDirectory >= len)
            {
                if (unicode)
#else
                if ('"' == lpszResponseBuffer[lastpos])
#endif
                {
#ifdef __REACTOS__
                    memcpy(lpszCurrentDirectory, first, len * sizeof(WCHAR));
                    bSuccess = TRUE;
#else
                    if (!firstpos)
                        firstpos = lastpos;
                    else
                        break;
#endif
                }
#ifdef __REACTOS__
                else
                    bSuccess = WideCharToMultiByte(CP_ACP, 0, first, -1, lpszCurrentDirectory, len,
                                                  NULL, NULL) != 0;
                if (bSuccess)
                    *lpdwCurrentDirectory = len;
                else
                    INTERNET_SetLastError(GetLastError());
#endif
            }
#ifdef __REACTOS__
            else
#else
            len = lastpos - firstpos;
            if (*lpdwCurrentDirectory >= len)
#endif
            {
#ifdef __REACTOS__
                *lpdwCurrentDirectory = unicode ? len * sizeof(WCHAR) : len;
                INTERNET_SetLastError(ERROR_INSUFFICIENT_BUFFER);
#else
                memcpy(lpszCurrentDirectory, &lpszResponseBuffer[firstpos + 1], len * sizeof(WCHAR));
                lpszCurrentDirectory[len - 1] = 0;
                *lpdwCurrentDirectory = len;
                bSuccess = TRUE;
#endif
            }
#ifndef __REACTOS__
            else INTERNET_SetLastError(ERROR_INSUFFICIENT_BUFFER);
#endif

            free(lpszResponseBuffer);
        }
        else
            FTP_SetResponseError(nResCode);
    }

lend:
#ifdef __REACTOS__
    if (!bSuccess && !INTERNET_GetLastError())
        INTERNET_SetLastError(ERROR_INTERNET_EXTENDED_ERROR);
#endif
    if (hIC->hdr.dwFlags & INTERNET_FLAG_ASYNC)
    {
        INTERNET_ASYNC_RESULT iar;

        iar.dwResult = bSuccess;
#ifdef __REACTOS__
        iar.dwError = bSuccess ? ERROR_SUCCESS : INTERNET_GetLastError();
#else
        iar.dwError = bSuccess ? ERROR_SUCCESS : ERROR_INTERNET_EXTENDED_ERROR;
#endif
        INTERNET_SendCallback(&lpwfs->hdr, lpwfs->hdr.dwContext, INTERNET_STATUS_REQUEST_COMPLETE,
            &iar, sizeof(INTERNET_ASYNC_RESULT));
    }

    return bSuccess;
}


/***********************************************************************
 *           FTPFILE_Destroy(internal)
 *
 * Closes the file transfer handle. This also 'cleans' the data queue of
 * the 'transfer complete' message (this is a bit of a hack though :-/ )
 *
 */
static void FTPFILE_Destroy(object_header_t *hdr)
{
    ftp_file_t *lpwh = (ftp_file_t*) hdr;
    ftp_session_t *lpwfs = lpwh->lpFtpSession;
    INT nResCode;

    TRACE("\n");

    if (lpwh->cache_file_handle != INVALID_HANDLE_VALUE)
        CloseHandle(lpwh->cache_file_handle);

    free(lpwh->cache_file);

    if (!lpwh->session_deleted)
        lpwfs->download_in_progress = NULL;

    if (lpwh->nDataSocket != -1)
        closesocket(lpwh->nDataSocket);

    nResCode = FTP_ReceiveResponse(lpwfs, lpwfs->hdr.dwContext);
    if (nResCode > 0 && nResCode != 226) WARN("server reports failed transfer\n");

    WININET_Release(&lpwh->lpFtpSession->hdr);
}

static DWORD FTPFILE_QueryOption(object_header_t *hdr, DWORD option, void *buffer, DWORD *size, BOOL unicode)
{
    switch(option) {
    case INTERNET_OPTION_HANDLE_TYPE:
        TRACE("INTERNET_OPTION_HANDLE_TYPE\n");

        if (*size < sizeof(ULONG))
            return ERROR_INSUFFICIENT_BUFFER;

        *size = sizeof(DWORD);
        *(DWORD*)buffer = INTERNET_HANDLE_TYPE_FTP_FILE;
        return ERROR_SUCCESS;
    case INTERNET_OPTION_DATAFILE_NAME:
    {
        DWORD required;
        ftp_file_t *file = (ftp_file_t *)hdr;

        TRACE("INTERNET_OPTION_DATAFILE_NAME\n");

        if (!file->cache_file)
        {
            *size = 0;
            return ERROR_INTERNET_ITEM_NOT_FOUND;
        }
        if (unicode)
        {
            required = (lstrlenW(file->cache_file) + 1) * sizeof(WCHAR);
            if (*size < required)
                return ERROR_INSUFFICIENT_BUFFER;

            *size = required;
            memcpy(buffer, file->cache_file, *size);
            return ERROR_SUCCESS;
        }
        else
        {
            required = WideCharToMultiByte(CP_ACP, 0, file->cache_file, -1, NULL, 0, NULL, NULL);
            if (required > *size)
                return ERROR_INSUFFICIENT_BUFFER;

            *size = WideCharToMultiByte(CP_ACP, 0, file->cache_file, -1, buffer, *size, NULL, NULL);
            return ERROR_SUCCESS;
        }
    }
    }
    return INET_QueryOption(hdr, option, buffer, size, unicode);
}

static DWORD FTPFILE_ReadFile(object_header_t *hdr, void *buffer, DWORD size, DWORD *read,
    DWORD flags, DWORD_PTR context)
{
    ftp_file_t *file = (ftp_file_t*)hdr;
    int res;
    DWORD error;

    if (file->nDataSocket == -1)
        return ERROR_INTERNET_DISCONNECTED;

    /* FIXME: FTP should use NETCON_ stuff */
    res = sock_recv(file->nDataSocket, buffer, size, MSG_WAITALL);
    *read = res>0 ? res : 0;

    error = res >= 0 ? ERROR_SUCCESS : INTERNET_ERROR_BASE; /* FIXME */
    if (error == ERROR_SUCCESS && file->cache_file)
    {
        DWORD bytes_written;

        if (!WriteFile(file->cache_file_handle, buffer, *read, &bytes_written, NULL))
            WARN("WriteFile failed: %lu\n", GetLastError());
    }
    return error;
}

static DWORD FTPFILE_WriteFile(object_header_t *hdr, const void *buffer, DWORD size, DWORD *written)
{
    ftp_file_t *lpwh = (ftp_file_t*) hdr;
    int res;

    res = sock_send(lpwh->nDataSocket, buffer, size, 0);

    *written = res>0 ? res : 0;
    return res >= 0 ? ERROR_SUCCESS : WSAGetLastError();
}

static void FTP_ReceiveRequestData(ftp_file_t *file, BOOL first_notif)
{
    INTERNET_ASYNC_RESULT iar;
    BYTE buffer[4096];
    int available;

    TRACE("%p\n", file);

    available = sock_recv(file->nDataSocket, buffer, sizeof(buffer), MSG_PEEK);

    if(available != -1) {
        iar.dwResult = (DWORD_PTR)file->hdr.hInternet;
        iar.dwError = first_notif ? 0 : available;
    }else {
        iar.dwResult = 0;
#ifdef __REACTOS__
        iar.dwError = WSAGetLastError();
#else
        iar.dwError = INTERNET_GetLastError();
#endif
    }

    INTERNET_SendCallback(&file->hdr, file->hdr.dwContext, INTERNET_STATUS_REQUEST_COMPLETE, &iar,
                          sizeof(INTERNET_ASYNC_RESULT));
}

static void FTPFILE_AsyncQueryDataAvailableProc(task_header_t *task)
{
    ftp_file_t *file = (ftp_file_t*)task->hdr;

    FTP_ReceiveRequestData(file, FALSE);
}

static DWORD FTPFILE_QueryDataAvailable(object_header_t *hdr, DWORD *available, DWORD flags, DWORD_PTR ctx)
{
    ftp_file_t *file = (ftp_file_t*) hdr;
    ULONG unread = 0;
    int retval;

    TRACE("(%p %p %lx %Ix)\n", file, available, flags, ctx);

    retval = ioctlsocket(file->nDataSocket, FIONREAD, &unread);
    if (!retval)
        TRACE("%ld bytes of queued, but unread data\n", unread);

    *available = unread;

    if(!unread) {
        BYTE byte;

        *available = 0;

        retval = sock_recv(file->nDataSocket, &byte, 1, MSG_PEEK);
        if(retval > 0) {
            task_header_t *task;

            task = alloc_async_task(&file->hdr, FTPFILE_AsyncQueryDataAvailableProc, sizeof(*task));
#ifdef __REACTOS__
            if (!task)
                return ERROR_OUTOFMEMORY;
            return INTERNET_AsyncCall(task);
#else
            INTERNET_AsyncCall(task);

            return ERROR_IO_PENDING;
#endif
        }
    }

    return ERROR_SUCCESS;
}

static DWORD FTPFILE_LockRequestFile(object_header_t *hdr, req_file_t **ret)
{
    ftp_file_t *file = (ftp_file_t*)hdr;
    FIXME("%p\n", file);
    return ERROR_NOT_SUPPORTED;
}

static const object_vtbl_t FTPFILEVtbl = {
    FTPFILE_Destroy,
    NULL,
    FTPFILE_QueryOption,
    INET_SetOption,
    NULL,
    FTPFILE_ReadFile,
    FTPFILE_WriteFile,
    FTPFILE_QueryDataAvailable,
    NULL,
    FTPFILE_LockRequestFile
};

/***********************************************************************
 *           FTP_FtpOpenFileW (Internal)
 *
 * Open a remote file for writing or reading
 *
 * RETURNS
 *    HINTERNET handle on success
 *    NULL on failure
 *
 */
static HINTERNET FTP_FtpOpenFileW(ftp_session_t *lpwfs,
	LPCWSTR lpszFileName, DWORD fdwAccess, DWORD dwFlags,
	DWORD_PTR dwContext)
{
    INT nDataSocket;
    BOOL bSuccess = FALSE;
    ftp_file_t *lpwh = NULL;
    appinfo_t *hIC = NULL;
#ifdef __REACTOS__
    HINTERNET handle = NULL;
    DWORD error;
#endif

    TRACE("\n");

    /* Clear any error information */
    INTERNET_SetLastError(0);

    if (GENERIC_READ == fdwAccess)
    {
        /* Set up socket to retrieve data */
        bSuccess = FTP_SendRetrieve(lpwfs, lpszFileName, dwFlags);
    }
    else if (GENERIC_WRITE == fdwAccess)
    {
        /* Set up socket to send data */
        bSuccess = FTP_SendStore(lpwfs, lpszFileName, dwFlags);
    }

    /* Get data socket to server */
#ifdef __REACTOS__
    if (bSuccess)
        bSuccess = FTP_GetDataSocket(lpwfs, &nDataSocket);
    if (bSuccess)
#else
    if (bSuccess && FTP_GetDataSocket(lpwfs, &nDataSocket))
#endif
    {
        lpwh = alloc_object(&lpwfs->hdr, &FTPFILEVtbl, sizeof(ftp_file_t));
#ifdef __REACTOS__
        if (!lpwh)
        {
            closesocket(nDataSocket);
            FTP_ReceiveResponse(lpwfs, lpwfs->hdr.dwContext);
            INTERNET_SetLastError(ERROR_OUTOFMEMORY);
            bSuccess = FALSE;
            goto finished;
        }
#endif
        lpwh->hdr.htype = WH_HFILE;
        lpwh->hdr.dwFlags = dwFlags;
        lpwh->hdr.dwContext = dwContext;
        lpwh->nDataSocket = nDataSocket;
        lpwh->cache_file = NULL;
        lpwh->cache_file_handle = INVALID_HANDLE_VALUE;
        lpwh->session_deleted = FALSE;

        WININET_AddRef( &lpwfs->hdr );
        lpwh->lpFtpSession = lpwfs;
#ifdef __REACTOS__
        WININET_AddRef(&lpwh->hdr);
#endif
        list_add_head( &lpwfs->hdr.children, &lpwh->hdr.entry );
	
	/* Indicate that a download is currently in progress */
	lpwfs->download_in_progress = lpwh;
#ifdef __REACTOS__
        handle = lpwh->hdr.hInternet;
#endif
    }

#ifdef __REACTOS__
finished:
    error = bSuccess ? ERROR_SUCCESS : INTERNET_GetLastError();
#endif
    if (lpwfs->lstnSocket != -1)
    {
        closesocket(lpwfs->lstnSocket);
        lpwfs->lstnSocket = -1;
    }

    if (bSuccess && fdwAccess == GENERIC_READ)
    {
        WCHAR filename[MAX_PATH + 1];
        URL_COMPONENTSW uc;
#ifdef __REACTOS__
        DWORD len = 0;
#else
        DWORD len;
#endif

        memset(&uc, 0, sizeof(uc));
        uc.dwStructSize = sizeof(uc);
        uc.nScheme      = INTERNET_SCHEME_FTP;
        uc.lpszHostName = lpwfs->servername;
        uc.nPort        = lpwfs->serverport;
        uc.lpszUserName = lpwfs->lpszUserName;
        uc.lpszUrlPath  = wcsdup(lpszFileName);

#ifdef __REACTOS__
        if (uc.lpszUrlPath && !InternetCreateUrlW(&uc, 0, NULL, &len) &&
            GetLastError() == ERROR_INSUFFICIENT_BUFFER && (size_t)len <= (size_t)-1 / sizeof(WCHAR))
#else
        if (!InternetCreateUrlW(&uc, 0, NULL, &len) && GetLastError() == ERROR_INSUFFICIENT_BUFFER)
#endif
        {
            WCHAR *url = malloc(len * sizeof(WCHAR));

            if (url && InternetCreateUrlW(&uc, 0, url, &len) && CreateUrlCacheEntryW(url, 0, NULL, filename, 0))
            {
                lpwh->cache_file = wcsdup(filename);
#ifdef __REACTOS__
                if (lpwh->cache_file)
                    lpwh->cache_file_handle = CreateFileW(filename, GENERIC_WRITE, FILE_SHARE_READ,
                                                          NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
#else
                lpwh->cache_file_handle = CreateFileW(filename, GENERIC_WRITE, FILE_SHARE_READ,
                                                      NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
#endif
                if (lpwh->cache_file_handle == INVALID_HANDLE_VALUE)
                {
                    WARN("Could not create cache file: %lu\n", GetLastError());
                    free(lpwh->cache_file);
                    lpwh->cache_file = NULL;
                }
            }
            free(url);
        }
        free(uc.lpszUrlPath);
    }

    hIC = lpwfs->lpAppInfo;
    if (hIC->hdr.dwFlags & INTERNET_FLAG_ASYNC)
    {
        INTERNET_ASYNC_RESULT iar;

	if (lpwh)
	{
            iar.dwResult = (DWORD_PTR)lpwh->hdr.hInternet;
            iar.dwError = ERROR_SUCCESS;
#ifdef __REACTOS__
            INTERNET_SendCallback(&lpwfs->hdr, dwContext, INTERNET_STATUS_HANDLE_CREATED,
#else
            INTERNET_SendCallback(&lpwfs->hdr, lpwfs->hdr.dwContext, INTERNET_STATUS_HANDLE_CREATED,
#endif
                &iar, sizeof(INTERNET_ASYNC_RESULT));
	}

#ifdef __REACTOS__
        if (lpwh && !lpwh->hdr.valid_handle)
        {
            bSuccess = FALSE;
            handle = NULL;
            error = ERROR_INTERNET_OPERATION_CANCELLED;
        }
        if (bSuccess && fdwAccess == GENERIC_READ) {
#else
        if(bSuccess) {
#endif
            FTP_ReceiveRequestData(lpwh, TRUE);
        }else {
#ifdef __REACTOS__
            iar.dwResult = (DWORD_PTR)handle;
            iar.dwError = error;
            INTERNET_SendCallback(lpwh ? &lpwh->hdr : &lpwfs->hdr, dwContext, INTERNET_STATUS_REQUEST_COMPLETE,
#else
            iar.dwResult = 0;
            iar.dwError = INTERNET_GetLastError();
            INTERNET_SendCallback(&lpwfs->hdr, lpwfs->hdr.dwContext, INTERNET_STATUS_REQUEST_COMPLETE,
#endif
                    &iar, sizeof(INTERNET_ASYNC_RESULT));
        }
    }

#ifdef __REACTOS__
    if (lpwh) WININET_Release(&lpwh->hdr);
    if (!bSuccess) INTERNET_SetLastError(error);
    return handle;
#else
    if(!bSuccess)
        return FALSE;

    return lpwh->hdr.hInternet;
#endif
}


/***********************************************************************
 *           FtpOpenFileA (WININET.@)
 *
 * Open a remote file for writing or reading
 *
 * RETURNS
 *    HINTERNET handle on success
 *    NULL on failure
 *
 */
HINTERNET WINAPI FtpOpenFileA(HINTERNET hFtpSession,
    LPCSTR lpszFileName, DWORD fdwAccess, DWORD dwFlags,
    DWORD_PTR dwContext)
{
    LPWSTR lpwzFileName;
    HINTERNET ret;

    lpwzFileName = strdupAtoW(lpszFileName);
#ifdef __REACTOS__
    if (lpszFileName && !lpwzFileName)
    {
        INTERNET_SetLastError(ERROR_OUTOFMEMORY);
        return NULL;
    }
#endif
    ret = FtpOpenFileW(hFtpSession, lpwzFileName, fdwAccess, dwFlags, dwContext);
    free(lpwzFileName);
    return ret;
}

typedef struct {
    task_header_t hdr;
    WCHAR *file_name;
    DWORD access;
    DWORD flags;
    DWORD_PTR context;
} open_file_task_t;

static void AsyncFtpOpenFileProc(task_header_t *hdr)
{
    open_file_task_t *task = (open_file_task_t*)hdr;
    ftp_session_t *session = (ftp_session_t*)task->hdr.hdr;

    TRACE("%p\n", session);

#ifdef __REACTOS__
    if (!FTP_FtpOpenFileW(session, task->file_name, task->access, task->flags, task->context) &&
        (session->hdr.dwInternalFlags & INET_OPENURL))
        InternetCloseHandle(session->hdr.hInternet);
}

static void FreeFtpOpenFileTask(task_header_t *hdr)
{
    open_file_task_t *task = (open_file_task_t*)hdr;

#else
    FTP_FtpOpenFileW(session, task->file_name, task->access, task->flags, task->context);
#endif
    free(task->file_name);
}

/***********************************************************************
 *           FtpOpenFileW (WININET.@)
 *
 * Open a remote file for writing or reading
 *
 * RETURNS
 *    HINTERNET handle on success
 *    NULL on failure
 *
 */
HINTERNET WINAPI FtpOpenFileW(HINTERNET hFtpSession,
    LPCWSTR lpszFileName, DWORD fdwAccess, DWORD dwFlags,
    DWORD_PTR dwContext)
{
    ftp_session_t *lpwfs;
    appinfo_t *hIC = NULL;
    HINTERNET r = NULL;
#ifdef __REACTOS__
    DWORD error;
#endif

    TRACE("(%p,%s,0x%08lx,0x%08lx,0x%08Ix)\n", hFtpSession,
        debugstr_w(lpszFileName), fdwAccess, dwFlags, dwContext);

    lpwfs = (ftp_session_t*) get_handle_object( hFtpSession );
    if (!lpwfs)
    {
        INTERNET_SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }

    if (WH_HFTPSESSION != lpwfs->hdr.htype)
    {
        INTERNET_SetLastError(ERROR_INTERNET_INCORRECT_HANDLE_TYPE);
        goto lend;
    }

    if ((!lpszFileName) ||
        ((fdwAccess != GENERIC_READ) && (fdwAccess != GENERIC_WRITE)) ||
        ((dwFlags & FTP_CONDITION_MASK) > FTP_TRANSFER_TYPE_BINARY))
    {
        INTERNET_SetLastError(ERROR_INVALID_PARAMETER);
        goto lend;
    }

    if (lpwfs->download_in_progress != NULL)
    {
        INTERNET_SetLastError(ERROR_FTP_TRANSFER_IN_PROGRESS);
        goto lend;
    }

    hIC = lpwfs->lpAppInfo;
    if (hIC->hdr.dwFlags & INTERNET_FLAG_ASYNC)
    {
        open_file_task_t *task;
#ifdef __REACTOS__
        DWORD res;
#endif

        task = alloc_async_task(&lpwfs->hdr, AsyncFtpOpenFileProc, sizeof(*task));
#ifdef __REACTOS__
        if (!task)
        {
            INTERNET_SetLastError(ERROR_OUTOFMEMORY);
            goto lend;
        }
        task->hdr.destroy = FreeFtpOpenFileTask;
#endif
        task->file_name = wcsdup(lpszFileName);
#ifdef __REACTOS__
        if (!task->file_name)
        {
            free_async_task(&task->hdr);
            INTERNET_SetLastError(ERROR_OUTOFMEMORY);
            goto lend;
        }
#endif
        task->access = fdwAccess;
        task->flags = dwFlags;
        task->context = dwContext;

#ifdef __REACTOS__
        res = INTERNET_AsyncCall(&task->hdr);
        INTERNET_SetLastError(res);
#else
        INTERNET_AsyncCall(&task->hdr);
#endif
        r = NULL;
    }
    else
    {
	r = FTP_FtpOpenFileW(lpwfs, lpszFileName, fdwAccess, dwFlags, dwContext);
    }

lend:
#ifdef __REACTOS__
    error = INTERNET_GetLastError();
#endif
    WININET_Release( &lpwfs->hdr );
#ifdef __REACTOS__
    if (!r) INTERNET_SetLastError(error);
#endif

    return r;
}


/***********************************************************************
 *           FtpGetFileA (WININET.@)
 *
 * Retrieve file from the FTP server
 *
 * RETURNS
 *    TRUE on success
 *    FALSE on failure
 *
 */
BOOL WINAPI FtpGetFileA(HINTERNET hInternet, LPCSTR lpszRemoteFile, LPCSTR lpszNewFile,
    BOOL fFailIfExists, DWORD dwLocalFlagsAttribute, DWORD dwInternetFlags,
    DWORD_PTR dwContext)
{
#ifdef __REACTOS__
    LPWSTR lpwzRemoteFile = NULL;
    LPWSTR lpwzNewFile = NULL;
    BOOL ret = FALSE;
    DWORD error;
#else
    LPWSTR lpwzRemoteFile;
    LPWSTR lpwzNewFile;
    BOOL ret;
#endif
    
#ifdef __REACTOS__
    if (lpszRemoteFile && !(lpwzRemoteFile = strdupAtoW(lpszRemoteFile))) goto done;
    if (lpszNewFile && !(lpwzNewFile = strdupAtoW(lpszNewFile))) goto done;
#else
    lpwzRemoteFile = strdupAtoW(lpszRemoteFile);
    lpwzNewFile = strdupAtoW(lpszNewFile);
#endif
    ret = FtpGetFileW(hInternet, lpwzRemoteFile, lpwzNewFile, fFailIfExists,
        dwLocalFlagsAttribute, dwInternetFlags, dwContext);
#ifdef __REACTOS__
done:
    error = GetLastError();
#endif
    free(lpwzRemoteFile);
    free(lpwzNewFile);
#ifdef __REACTOS__
    if (!ret) INTERNET_SetLastError(error);
#endif
    return ret;
}

typedef struct {
    task_header_t hdr;
    WCHAR *remote_file;
    WCHAR *new_file;
    BOOL fail_if_exists;
    DWORD local_attr;
    DWORD flags;
    DWORD_PTR context;
} get_file_task_t;

static void AsyncFtpGetFileProc(task_header_t *hdr)
{
    get_file_task_t *task = (get_file_task_t*)hdr;
    ftp_session_t *session = (ftp_session_t*)task->hdr.hdr;

    TRACE("%p\n", session);

    FTP_FtpGetFileW(session, task->remote_file, task->new_file, task->fail_if_exists,
             task->local_attr, task->flags, task->context);
#ifdef __REACTOS__
}

static void FreeFtpGetFileTask(task_header_t *hdr)
{
    get_file_task_t *task = (get_file_task_t*)hdr;

#endif
    free(task->remote_file);
    free(task->new_file);
}


/***********************************************************************
 *           FtpGetFileW (WININET.@)
 *
 * Retrieve file from the FTP server
 *
 * RETURNS
 *    TRUE on success
 *    FALSE on failure
 *
 */
BOOL WINAPI FtpGetFileW(HINTERNET hInternet, LPCWSTR lpszRemoteFile, LPCWSTR lpszNewFile,
    BOOL fFailIfExists, DWORD dwLocalFlagsAttribute, DWORD dwInternetFlags,
    DWORD_PTR dwContext)
{
    ftp_session_t *lpwfs;
    appinfo_t *hIC = NULL;
    BOOL r = FALSE;
#ifdef __REACTOS__
    DWORD error;
#endif

    if (!lpszRemoteFile || !lpszNewFile)
    {
        INTERNET_SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    lpwfs = (ftp_session_t*) get_handle_object( hInternet );
    if (!lpwfs)
    {
        INTERNET_SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }

    if (WH_HFTPSESSION != lpwfs->hdr.htype)
    {
        INTERNET_SetLastError(ERROR_INTERNET_INCORRECT_HANDLE_TYPE);
        goto lend;
    }

    if ((dwInternetFlags & FTP_CONDITION_MASK) > FTP_TRANSFER_TYPE_BINARY)
    {
        INTERNET_SetLastError(ERROR_INVALID_PARAMETER);
        goto lend;
    }

    if (lpwfs->download_in_progress != NULL)
    {
        INTERNET_SetLastError(ERROR_FTP_TRANSFER_IN_PROGRESS);
        goto lend;
    }
    
    hIC = lpwfs->lpAppInfo;
    if (hIC->hdr.dwFlags & INTERNET_FLAG_ASYNC)
    {
        get_file_task_t *task;

        task = alloc_async_task(&lpwfs->hdr, AsyncFtpGetFileProc, sizeof(*task));
#ifdef __REACTOS__
        if (!task)
        {
            INTERNET_SetLastError(ERROR_OUTOFMEMORY);
            goto lend;
        }
        task->hdr.destroy = FreeFtpGetFileTask;
#endif
        task->remote_file = wcsdup(lpszRemoteFile);
        task->new_file = wcsdup(lpszNewFile);
#ifdef __REACTOS__
        if (!task->remote_file || !task->new_file)
        {
            free_async_task(&task->hdr);
            INTERNET_SetLastError(ERROR_OUTOFMEMORY);
            goto lend;
        }
#endif
        task->local_attr = dwLocalFlagsAttribute;
        task->fail_if_exists = fFailIfExists;
        task->flags = dwInternetFlags;
        task->context = dwContext;

        r = res_to_le(INTERNET_AsyncCall(&task->hdr));
    }
    else
    {
        r = FTP_FtpGetFileW(lpwfs, lpszRemoteFile, lpszNewFile,
           fFailIfExists, dwLocalFlagsAttribute, dwInternetFlags, dwContext);
    }

lend:
#ifdef __REACTOS__
    error = r ? ERROR_SUCCESS : INTERNET_GetLastError();
#endif
    WININET_Release( &lpwfs->hdr );
#ifdef __REACTOS__
    if (!r)
        INTERNET_SetLastError(error);
#endif

    return r;
}


/***********************************************************************
 *           FTP_FtpGetFileW (Internal)
 *
 * Retrieve file from the FTP server
 *
 * RETURNS
 *    TRUE on success
 *    FALSE on failure
 *
 */
static BOOL FTP_FtpGetFileW(ftp_session_t *lpwfs, LPCWSTR lpszRemoteFile, LPCWSTR lpszNewFile,
	BOOL fFailIfExists, DWORD dwLocalFlagsAttribute, DWORD dwInternetFlags,
	DWORD_PTR dwContext)
{
    BOOL bSuccess = FALSE;
    HANDLE hFile;
    appinfo_t *hIC = NULL;
#ifdef __REACTOS__
    DWORD result_error;
#endif

    TRACE("lpszRemoteFile(%s) lpszNewFile(%s)\n", debugstr_w(lpszRemoteFile), debugstr_w(lpszNewFile));

    /* Clear any error information */
    INTERNET_SetLastError(0);

    /* Ensure we can write to lpszNewfile by opening it */
    hFile = CreateFileW(lpszNewFile, GENERIC_WRITE, 0, 0, fFailIfExists ?
        CREATE_NEW : CREATE_ALWAYS, dwLocalFlagsAttribute, 0);
    if (INVALID_HANDLE_VALUE == hFile)
#ifdef __REACTOS__
    {
        INTERNET_SetLastError(GetLastError());
        goto done;
    }
#else
        return FALSE;
#endif

    /* Set up socket to retrieve data */
    if (FTP_SendRetrieve(lpwfs, lpszRemoteFile, dwInternetFlags))
    {
        INT nDataSocket;

        /* Get data socket to server */
        if (FTP_GetDataSocket(lpwfs, &nDataSocket))
        {
            INT nResCode;
#ifdef __REACTOS__
            BOOL transferred;
            DWORD error;
#endif

            /* Receive data */
#ifdef __REACTOS__
            transferred = FTP_RetrieveFileData(lpwfs, nDataSocket, hFile);
            error = transferred ? ERROR_SUCCESS : INTERNET_GetLastError();
#else
            FTP_RetrieveFileData(lpwfs, nDataSocket, hFile);
#endif
            closesocket(nDataSocket);

            nResCode = FTP_ReceiveResponse(lpwfs, dwContext);
#ifdef __REACTOS__
            if (!transferred)
                INTERNET_SetLastError(error);
            else if (nResCode)
#else
            if (nResCode)
#endif
            {
                if (nResCode == 226)
                    bSuccess = TRUE;
                else
                    FTP_SetResponseError(nResCode);
            }
        }
    }

    if (lpwfs->lstnSocket != -1)
    {
        closesocket(lpwfs->lstnSocket);
        lpwfs->lstnSocket = -1;
    }

#ifdef __REACTOS__
done:
    result_error = bSuccess ? ERROR_SUCCESS : INTERNET_GetLastError();
    if (!bSuccess && !result_error)
        result_error = ERROR_INTERNET_EXTENDED_ERROR;
    if (hFile != INVALID_HANDLE_VALUE)
        CloseHandle(hFile);
#else
    CloseHandle(hFile);
#endif

    hIC = lpwfs->lpAppInfo;
    if (hIC->hdr.dwFlags & INTERNET_FLAG_ASYNC)
    {
        INTERNET_ASYNC_RESULT iar;

        iar.dwResult = (DWORD)bSuccess;
#ifdef __REACTOS__
        iar.dwError = result_error;
#else
        iar.dwError = bSuccess ? ERROR_SUCCESS : INTERNET_GetLastError();
#endif
        INTERNET_SendCallback(&lpwfs->hdr, lpwfs->hdr.dwContext, INTERNET_STATUS_REQUEST_COMPLETE,
            &iar, sizeof(INTERNET_ASYNC_RESULT));
    }

#ifdef __REACTOS__
    if (!bSuccess)
    {
        if (hFile != INVALID_HANDLE_VALUE)
            DeleteFileW(lpszNewFile);
        INTERNET_SetLastError(result_error);
    }
#else
    if (!bSuccess) DeleteFileW(lpszNewFile);
#endif
    return bSuccess;
}

/***********************************************************************
 *           FtpGetFileSize  (WININET.@)
 */
DWORD WINAPI FtpGetFileSize( HINTERNET hFile, LPDWORD lpdwFileSizeHigh )
{
    FIXME("(%p, %p)\n", hFile, lpdwFileSizeHigh);

    if (lpdwFileSizeHigh)
        *lpdwFileSizeHigh = 0;

    return 0;
}

/***********************************************************************
 *           FtpDeleteFileA  (WININET.@)
 *
 * Delete a file on the ftp server
 *
 * RETURNS
 *    TRUE on success
 *    FALSE on failure
 *
 */
BOOL WINAPI FtpDeleteFileA(HINTERNET hFtpSession, LPCSTR lpszFileName)
{
    LPWSTR lpwzFileName;
    BOOL ret;

    lpwzFileName = strdupAtoW(lpszFileName);
#ifdef __REACTOS__
    if (lpszFileName && !lpwzFileName)
    {
        INTERNET_SetLastError(GetLastError());
        return FALSE;
    }
#endif
    ret = FtpDeleteFileW(hFtpSession, lpwzFileName);
    free(lpwzFileName);
    return ret;
}

typedef struct {
    task_header_t hdr;
    WCHAR *file_name;
} delete_file_task_t;

static void AsyncFtpDeleteFileProc(task_header_t *hdr)
{
    delete_file_task_t *task = (delete_file_task_t*)hdr;
    ftp_session_t *session = (ftp_session_t*)task->hdr.hdr;

    TRACE("%p\n", session);

    FTP_FtpDeleteFileW(session, task->file_name);
#ifdef __REACTOS__
}

static void FreeFtpDeleteFileTask(task_header_t *hdr)
{
    delete_file_task_t *task = (delete_file_task_t*)hdr;

#endif
    free(task->file_name);
}

/***********************************************************************
 *           FtpDeleteFileW  (WININET.@)
 *
 * Delete a file on the ftp server
 *
 * RETURNS
 *    TRUE on success
 *    FALSE on failure
 *
 */
BOOL WINAPI FtpDeleteFileW(HINTERNET hFtpSession, LPCWSTR lpszFileName)
{
    ftp_session_t *lpwfs;
    appinfo_t *hIC = NULL;
    BOOL r = FALSE;

    lpwfs = (ftp_session_t*) get_handle_object( hFtpSession );
    if (!lpwfs)
    {
        INTERNET_SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }

    if (WH_HFTPSESSION != lpwfs->hdr.htype)
    {
        INTERNET_SetLastError(ERROR_INTERNET_INCORRECT_HANDLE_TYPE);
        goto lend;
    }

    if (lpwfs->download_in_progress != NULL)
    {
        INTERNET_SetLastError(ERROR_FTP_TRANSFER_IN_PROGRESS);
        goto lend;
    }

    if (!lpszFileName)
    {
        INTERNET_SetLastError(ERROR_INVALID_PARAMETER);
        goto lend;
    }

    hIC = lpwfs->lpAppInfo;
    if (hIC->hdr.dwFlags & INTERNET_FLAG_ASYNC)
    {
        delete_file_task_t *task;

        task = alloc_async_task(&lpwfs->hdr, AsyncFtpDeleteFileProc, sizeof(*task));
#ifdef __REACTOS__
        if (!task)
        {
            INTERNET_SetLastError(ERROR_OUTOFMEMORY);
            goto lend;
        }
        task->hdr.destroy = FreeFtpDeleteFileTask;
#endif
        task->file_name = wcsdup(lpszFileName);
#ifdef __REACTOS__
        if (!task->file_name)
        {
            free_async_task(&task->hdr);
            INTERNET_SetLastError(ERROR_OUTOFMEMORY);
            goto lend;
        }
#endif

        r = res_to_le(INTERNET_AsyncCall(&task->hdr));
    }
    else
    {
        r = FTP_FtpDeleteFileW(lpwfs, lpszFileName);
    }

lend:
    WININET_Release( &lpwfs->hdr );

    return r;
}

/***********************************************************************
 *           FTP_FtpDeleteFileW  (Internal)
 *
 * Delete a file on the ftp server
 *
 * RETURNS
 *    TRUE on success
 *    FALSE on failure
 *
 */
BOOL FTP_FtpDeleteFileW(ftp_session_t *lpwfs, LPCWSTR lpszFileName)
{
    INT nResCode;
    BOOL bSuccess = FALSE;
    appinfo_t *hIC = NULL;

    TRACE("%p\n", lpwfs);

    /* Clear any error information */
    INTERNET_SetLastError(0);

    if (!FTP_SendCommand(lpwfs->sndSocket, FTP_CMD_DELE, lpszFileName, 0, 0, 0))
        goto lend;

    nResCode = FTP_ReceiveResponse(lpwfs, lpwfs->hdr.dwContext);
    if (nResCode)
    {
        if (nResCode == 250)
            bSuccess = TRUE;
        else
            FTP_SetResponseError(nResCode);
    }
lend:
    hIC = lpwfs->lpAppInfo;
    if (hIC->hdr.dwFlags & INTERNET_FLAG_ASYNC)
    {
        INTERNET_ASYNC_RESULT iar;

        iar.dwResult = (DWORD)bSuccess;
        iar.dwError = bSuccess ? ERROR_SUCCESS : INTERNET_GetLastError();
        INTERNET_SendCallback(&lpwfs->hdr, lpwfs->hdr.dwContext, INTERNET_STATUS_REQUEST_COMPLETE,
            &iar, sizeof(INTERNET_ASYNC_RESULT));
    }

    return bSuccess;
}


/***********************************************************************
 *           FtpRemoveDirectoryA  (WININET.@)
 *
 * Remove a directory on the ftp server
 *
 * RETURNS
 *    TRUE on success
 *    FALSE on failure
 *
 */
BOOL WINAPI FtpRemoveDirectoryA(HINTERNET hFtpSession, LPCSTR lpszDirectory)
{
    LPWSTR lpwzDirectory;
    BOOL ret;

    lpwzDirectory = strdupAtoW(lpszDirectory);
#ifdef __REACTOS__
    if (lpszDirectory && !lpwzDirectory)
    {
        INTERNET_SetLastError(GetLastError());
        return FALSE;
    }
#endif
    ret = FtpRemoveDirectoryW(hFtpSession, lpwzDirectory);
    free(lpwzDirectory);
    return ret;
}

static void AsyncFtpRemoveDirectoryProc(task_header_t *hdr)
{
    directory_task_t *task = (directory_task_t*)hdr;
    ftp_session_t *session = (ftp_session_t*)task->hdr.hdr;

    TRACE("%p\n", session);

    FTP_FtpRemoveDirectoryW(session, task->directory);
#ifndef __REACTOS__
    free(task->directory);
#endif
}

/***********************************************************************
 *           FtpRemoveDirectoryW  (WININET.@)
 *
 * Remove a directory on the ftp server
 *
 * RETURNS
 *    TRUE on success
 *    FALSE on failure
 *
 */
BOOL WINAPI FtpRemoveDirectoryW(HINTERNET hFtpSession, LPCWSTR lpszDirectory)
{
    ftp_session_t *lpwfs;
    appinfo_t *hIC = NULL;
    BOOL r = FALSE;

    lpwfs = (ftp_session_t*) get_handle_object( hFtpSession );
    if (!lpwfs)
    {
        INTERNET_SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }

    if (WH_HFTPSESSION != lpwfs->hdr.htype)
    {
        INTERNET_SetLastError(ERROR_INTERNET_INCORRECT_HANDLE_TYPE);
        goto lend;
    }

    if (lpwfs->download_in_progress != NULL)
    {
        INTERNET_SetLastError(ERROR_FTP_TRANSFER_IN_PROGRESS);
        goto lend;
    }

    if (!lpszDirectory)
    {
        INTERNET_SetLastError(ERROR_INVALID_PARAMETER);
        goto lend;
    }

    hIC = lpwfs->lpAppInfo;
    if (hIC->hdr.dwFlags & INTERNET_FLAG_ASYNC)
    {
        directory_task_t *task;

        task = alloc_async_task(&lpwfs->hdr, AsyncFtpRemoveDirectoryProc, sizeof(*task));
#ifdef __REACTOS__
        if (!task)
        {
            INTERNET_SetLastError(ERROR_OUTOFMEMORY);
            goto lend;
        }
        task->hdr.destroy = FreeFtpDirectoryTask;
#endif
        task->directory = wcsdup(lpszDirectory);
#ifdef __REACTOS__
        if (!task->directory)
        {
            free_async_task(&task->hdr);
            INTERNET_SetLastError(ERROR_OUTOFMEMORY);
            goto lend;
        }
#endif

        r = res_to_le(INTERNET_AsyncCall(&task->hdr));
    }
    else
    {
        r = FTP_FtpRemoveDirectoryW(lpwfs, lpszDirectory);
    }

lend:
    WININET_Release( &lpwfs->hdr );

    return r;
}

/***********************************************************************
 *           FTP_FtpRemoveDirectoryW  (Internal)
 *
 * Remove a directory on the ftp server
 *
 * RETURNS
 *    TRUE on success
 *    FALSE on failure
 *
 */
BOOL FTP_FtpRemoveDirectoryW(ftp_session_t *lpwfs, LPCWSTR lpszDirectory)
{
    INT nResCode;
    BOOL bSuccess = FALSE;
    appinfo_t *hIC = NULL;

    TRACE("\n");

    /* Clear any error information */
    INTERNET_SetLastError(0);

    if (!FTP_SendCommand(lpwfs->sndSocket, FTP_CMD_RMD, lpszDirectory, 0, 0, 0))
        goto lend;

    nResCode = FTP_ReceiveResponse(lpwfs, lpwfs->hdr.dwContext);
    if (nResCode)
    {
        if (nResCode == 250)
            bSuccess = TRUE;
        else
            FTP_SetResponseError(nResCode);
    }

lend:
    hIC = lpwfs->lpAppInfo;
    if (hIC->hdr.dwFlags & INTERNET_FLAG_ASYNC)
    {
        INTERNET_ASYNC_RESULT iar;

        iar.dwResult = (DWORD)bSuccess;
        iar.dwError = bSuccess ? ERROR_SUCCESS : INTERNET_GetLastError();
        INTERNET_SendCallback(&lpwfs->hdr, lpwfs->hdr.dwContext, INTERNET_STATUS_REQUEST_COMPLETE,
            &iar, sizeof(INTERNET_ASYNC_RESULT));
    }

    return bSuccess;
}


/***********************************************************************
 *           FtpRenameFileA  (WININET.@)
 *
 * Rename a file on the ftp server
 *
 * RETURNS
 *    TRUE on success
 *    FALSE on failure
 *
 */
BOOL WINAPI FtpRenameFileA(HINTERNET hFtpSession, LPCSTR lpszSrc, LPCSTR lpszDest)
{
#ifdef __REACTOS__
    LPWSTR lpwzSrc = NULL;
    LPWSTR lpwzDest = NULL;
    BOOL ret = FALSE;
    DWORD error;
#else
    LPWSTR lpwzSrc;
    LPWSTR lpwzDest;
    BOOL ret;
#endif

#ifdef __REACTOS__
    if (lpszSrc && !(lpwzSrc = strdupAtoW(lpszSrc))) goto done;
    if (lpszDest && !(lpwzDest = strdupAtoW(lpszDest))) goto done;
#else
    lpwzSrc = strdupAtoW(lpszSrc);
    lpwzDest = strdupAtoW(lpszDest);
#endif
    ret = FtpRenameFileW(hFtpSession, lpwzSrc, lpwzDest);
#ifdef __REACTOS__
done:
    error = GetLastError();
#endif
    free(lpwzSrc);
    free(lpwzDest);
#ifdef __REACTOS__
    if (!ret) INTERNET_SetLastError(error);
#endif
    return ret;
}

typedef struct {
    task_header_t hdr;
    WCHAR *src_file;
    WCHAR *dst_file;
} rename_file_task_t;

static void AsyncFtpRenameFileProc(task_header_t *hdr)
{
    rename_file_task_t *task = (rename_file_task_t*)hdr;
    ftp_session_t *session = (ftp_session_t*)task->hdr.hdr;

    TRACE("%p\n", session);

    FTP_FtpRenameFileW(session, task->src_file, task->dst_file);
#ifdef __REACTOS__
}

static void FreeFtpRenameFileTask(task_header_t *hdr)
{
    rename_file_task_t *task = (rename_file_task_t*)hdr;

#endif
    free(task->src_file);
    free(task->dst_file);
}

/***********************************************************************
 *           FtpRenameFileW  (WININET.@)
 *
 * Rename a file on the ftp server
 *
 * RETURNS
 *    TRUE on success
 *    FALSE on failure
 *
 */
BOOL WINAPI FtpRenameFileW(HINTERNET hFtpSession, LPCWSTR lpszSrc, LPCWSTR lpszDest)
{
    ftp_session_t *lpwfs;
    appinfo_t *hIC = NULL;
    BOOL r = FALSE;

    lpwfs = (ftp_session_t*) get_handle_object( hFtpSession );
    if (!lpwfs)
    {
        INTERNET_SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }

    if (WH_HFTPSESSION != lpwfs->hdr.htype)
    {
        INTERNET_SetLastError(ERROR_INTERNET_INCORRECT_HANDLE_TYPE);
        goto lend;
    }

    if (lpwfs->download_in_progress != NULL)
    {
        INTERNET_SetLastError(ERROR_FTP_TRANSFER_IN_PROGRESS);
        goto lend;
    }

    if (!lpszSrc || !lpszDest)
    {
        INTERNET_SetLastError(ERROR_INVALID_PARAMETER);
        goto lend;
    }

    hIC = lpwfs->lpAppInfo;
    if (hIC->hdr.dwFlags & INTERNET_FLAG_ASYNC)
    {
        rename_file_task_t *task;

        task = alloc_async_task(&lpwfs->hdr, AsyncFtpRenameFileProc, sizeof(*task));
#ifdef __REACTOS__
        if (!task)
        {
            INTERNET_SetLastError(ERROR_OUTOFMEMORY);
            goto lend;
        }
        task->hdr.destroy = FreeFtpRenameFileTask;
#endif
        task->src_file = wcsdup(lpszSrc);
        task->dst_file = wcsdup(lpszDest);
#ifdef __REACTOS__
        if (!task->src_file || !task->dst_file)
        {
            free_async_task(&task->hdr);
            INTERNET_SetLastError(ERROR_OUTOFMEMORY);
            goto lend;
        }
#endif

        r = res_to_le(INTERNET_AsyncCall(&task->hdr));
    }
    else
    {
        r = FTP_FtpRenameFileW(lpwfs, lpszSrc, lpszDest);
    }

lend:
    WININET_Release( &lpwfs->hdr );

    return r;
}

/***********************************************************************
 *           FTP_FtpRenameFileW  (Internal)
 *
 * Rename a file on the ftp server
 *
 * RETURNS
 *    TRUE on success
 *    FALSE on failure
 *
 */
BOOL FTP_FtpRenameFileW(ftp_session_t *lpwfs, LPCWSTR lpszSrc, LPCWSTR lpszDest)
{
    INT nResCode;
    BOOL bSuccess = FALSE;
    appinfo_t *hIC = NULL;

    TRACE("\n");

    /* Clear any error information */
    INTERNET_SetLastError(0);

    if (!FTP_SendCommand(lpwfs->sndSocket, FTP_CMD_RNFR, lpszSrc, 0, 0, 0))
        goto lend;

    nResCode = FTP_ReceiveResponse(lpwfs, lpwfs->hdr.dwContext);
    if (nResCode == 350)
    {
        if (!FTP_SendCommand(lpwfs->sndSocket, FTP_CMD_RNTO, lpszDest, 0, 0, 0))
            goto lend;

        nResCode = FTP_ReceiveResponse(lpwfs, lpwfs->hdr.dwContext);
    }

    if (nResCode == 250)
        bSuccess = TRUE;
    else
        FTP_SetResponseError(nResCode);

lend:
    hIC = lpwfs->lpAppInfo;
    if (hIC->hdr.dwFlags & INTERNET_FLAG_ASYNC)
    {
        INTERNET_ASYNC_RESULT iar;

        iar.dwResult = (DWORD)bSuccess;
        iar.dwError = bSuccess ? ERROR_SUCCESS : INTERNET_GetLastError();
        INTERNET_SendCallback(&lpwfs->hdr, lpwfs->hdr.dwContext, INTERNET_STATUS_REQUEST_COMPLETE,
            &iar, sizeof(INTERNET_ASYNC_RESULT));
    }

    return bSuccess;
}

/***********************************************************************
 *           FtpCommandA  (WININET.@)
 */
BOOL WINAPI FtpCommandA( HINTERNET hConnect, BOOL fExpectResponse, DWORD dwFlags,
                         LPCSTR lpszCommand, DWORD_PTR dwContext, HINTERNET* phFtpCommand )
{
    BOOL r;
    WCHAR *cmdW;

    TRACE("%p %d 0x%08lx %s 0x%08Ix %p\n", hConnect, fExpectResponse, dwFlags,
          debugstr_a(lpszCommand), dwContext, phFtpCommand);

    if (fExpectResponse)
    {
        FIXME("data connection not supported\n");
        return FALSE;
    }

    if (!lpszCommand || !lpszCommand[0])
    {
        INTERNET_SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if (!(cmdW = strdupAtoW(lpszCommand)))
    {
        INTERNET_SetLastError(ERROR_OUTOFMEMORY);
        return FALSE;
    }

    r = FtpCommandW(hConnect, fExpectResponse, dwFlags, cmdW, dwContext, phFtpCommand);

    free(cmdW);
    return r;
}

/***********************************************************************
 *           FtpCommandW  (WININET.@)
 */
BOOL WINAPI FtpCommandW( HINTERNET hConnect, BOOL fExpectResponse, DWORD dwFlags,
                         LPCWSTR lpszCommand, DWORD_PTR dwContext, HINTERNET* phFtpCommand )
{
    BOOL r = FALSE;
    ftp_session_t *lpwfs;
    LPSTR cmd = NULL;
    DWORD len, nBytesSent= 0;
    INT nResCode, nRC = 0;

    TRACE("%p %d 0x%08lx %s 0x%08Ix %p\n", hConnect, fExpectResponse, dwFlags,
           debugstr_w(lpszCommand), dwContext, phFtpCommand);

    if (!lpszCommand || !lpszCommand[0])
    {
        INTERNET_SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if (fExpectResponse)
    {
        FIXME("data connection not supported\n");
        return FALSE;
    }

    lpwfs = (ftp_session_t*) get_handle_object( hConnect );
    if (!lpwfs)
    {
        INTERNET_SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }

    if (WH_HFTPSESSION != lpwfs->hdr.htype)
    {
        INTERNET_SetLastError(ERROR_INTERNET_INCORRECT_HANDLE_TYPE);
        goto lend;
    }

    if (lpwfs->download_in_progress != NULL)
    {
        INTERNET_SetLastError(ERROR_FTP_TRANSFER_IN_PROGRESS);
        goto lend;
    }

    len = WideCharToMultiByte(CP_ACP, 0, lpszCommand, -1, NULL, 0, NULL, NULL) + strlen(szCRLF);
    if ((cmd = malloc(len)))
        WideCharToMultiByte(CP_ACP, 0, lpszCommand, -1, cmd, len, NULL, NULL);
    else
    {
        INTERNET_SetLastError(ERROR_OUTOFMEMORY);
        goto lend;
    }

    strcat(cmd, szCRLF);
    len--;

    TRACE("Sending (%s) len(%ld)\n", debugstr_a(cmd), len);
    while ((nBytesSent < len) && (nRC != -1))
    {
        nRC = sock_send(lpwfs->sndSocket, cmd + nBytesSent, len - nBytesSent, 0);
        if (nRC != -1)
        {
            nBytesSent += nRC;
            TRACE("Sent %d bytes\n", nRC);
        }
    }

    if (nBytesSent)
    {
        nResCode = FTP_ReceiveResponse(lpwfs, lpwfs->hdr.dwContext);
        if (nResCode > 0 && nResCode < 400)
            r = TRUE;
        else
            FTP_SetResponseError(nResCode);
    }

lend:
    WININET_Release( &lpwfs->hdr );
    free(cmd);
    return r;
}


/***********************************************************************
 *           FTPSESSION_Destroy (internal)
 *
 * Deallocate session handle
 */
static void FTPSESSION_Destroy(object_header_t *hdr)
{
    ftp_session_t *lpwfs = (ftp_session_t*) hdr;

    TRACE("\n");

    WININET_Release(&lpwfs->lpAppInfo->hdr);

    free(lpwfs->lpszPassword);
    free(lpwfs->lpszUserName);
    free(lpwfs->servername);
}

static void FTPSESSION_CloseConnection(object_header_t *hdr)
{
    ftp_session_t *lpwfs = (ftp_session_t*) hdr;

    TRACE("\n");

    INTERNET_SendCallback(&lpwfs->hdr, lpwfs->hdr.dwContext,
                      INTERNET_STATUS_CLOSING_CONNECTION, 0, 0);

    if (lpwfs->download_in_progress != NULL)
        lpwfs->download_in_progress->session_deleted = TRUE;

    if (lpwfs->sndSocket != -1)
        closesocket(lpwfs->sndSocket);

    if (lpwfs->lstnSocket != -1)
        closesocket(lpwfs->lstnSocket);

    if (lpwfs->pasvSocket != -1)
        closesocket(lpwfs->pasvSocket);

    INTERNET_SendCallback(&lpwfs->hdr, lpwfs->hdr.dwContext,
                      INTERNET_STATUS_CONNECTION_CLOSED, 0, 0);
}

static DWORD FTPSESSION_QueryOption(object_header_t *hdr, DWORD option, void *buffer, DWORD *size, BOOL unicode)
{
    switch(option) {
    case INTERNET_OPTION_HANDLE_TYPE:
        TRACE("INTERNET_OPTION_HANDLE_TYPE\n");

        if (*size < sizeof(ULONG))
            return ERROR_INSUFFICIENT_BUFFER;

        *size = sizeof(DWORD);
        *(DWORD*)buffer = INTERNET_HANDLE_TYPE_CONNECT_FTP;
        return ERROR_SUCCESS;
    }

    return INET_QueryOption(hdr, option, buffer, size, unicode);
}

static const object_vtbl_t FTPSESSIONVtbl = {
    FTPSESSION_Destroy,
    FTPSESSION_CloseConnection,
    FTPSESSION_QueryOption,
    INET_SetOption,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};


/***********************************************************************
 *           FTP_Connect (internal)
 *
 * Connect to a ftp server
 *
 * RETURNS
 *   HINTERNET a session handle on success
 *   NULL on failure
 *
 * NOTES:
 *
 * Windows uses 'anonymous' as the username, when given a NULL username
 * and a NULL password. The password is first looked up in:
 *
 * HKCU\Software\Microsoft\Windows\CurrentVersion\Internet Settings\EmailName
 *
 * If this entry is not present it uses the current username as the password.
 *
 */

HINTERNET FTP_Connect(appinfo_t *hIC, LPCWSTR lpszServerName,
	INTERNET_PORT nServerPort, LPCWSTR lpszUserName,
	LPCWSTR lpszPassword, DWORD dwFlags, DWORD_PTR dwContext,
	DWORD dwInternalFlags)
{
    INT nsocket = -1;
    socklen_t sock_namelen;
    BOOL bSuccess = FALSE;
    ftp_session_t *lpwfs = NULL;
#ifdef __REACTOS__
    server_addr_t *server_addr = NULL;
    HINTERNET handle;
    DWORD error;
#else
    server_addr_t *server_addr;
#endif

    TRACE("%p  Server(%s) Port(%d) User(%s) Paswd(%s)\n",
	    hIC, debugstr_w(lpszServerName),
	    nServerPort, debugstr_w(lpszUserName), debugstr_w(lpszPassword));

    assert( hIC->hdr.htype == WH_HINIT );

#ifdef __REACTOS__
    if (!lpszServerName || !*lpszServerName)
    {
        INTERNET_SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
#endif
    if ((!lpszUserName || !*lpszUserName) && lpszPassword && *lpszPassword)
    {
	INTERNET_SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    
    lpwfs = alloc_object(&hIC->hdr, &FTPSESSIONVtbl, sizeof(ftp_session_t));
    if (NULL == lpwfs)
    {
        INTERNET_SetLastError(ERROR_OUTOFMEMORY);
        return NULL;
    }

    if (nServerPort == INTERNET_INVALID_PORT_NUMBER)
        lpwfs->serverport = INTERNET_DEFAULT_FTP_PORT;
    else
        lpwfs->serverport = nServerPort;

    lpwfs->hdr.htype = WH_HFTPSESSION;
    lpwfs->hdr.dwFlags = dwFlags;
    lpwfs->hdr.dwContext = dwContext;
    lpwfs->hdr.dwInternalFlags |= dwInternalFlags;
    lpwfs->download_in_progress = NULL;
    lpwfs->sndSocket = -1;
    lpwfs->lstnSocket = -1;
    lpwfs->pasvSocket = -1;

    WININET_AddRef( &hIC->hdr );
    lpwfs->lpAppInfo = hIC;
#ifdef __REACTOS__
    WININET_AddRef(&lpwfs->hdr);
#endif
    list_add_head( &hIC->hdr.children, &lpwfs->hdr.entry );

    if(hIC->proxy && hIC->accessType == INTERNET_OPEN_TYPE_PROXY) {
        if(wcschr(hIC->proxy, ' '))
            FIXME("Several proxies not implemented.\n");
        if(hIC->proxyBypass)
            FIXME("Proxy bypass is ignored.\n");
    }
    if (!lpszUserName || !lpszUserName[0]) {
        HKEY key;
#ifdef __REACTOS__
        WCHAR szPassword[MAX_PATH + 1];
        DWORD len = sizeof(szPassword) - sizeof(WCHAR), type;
        LONG res;
#else
        WCHAR szPassword[MAX_PATH];
        DWORD len = sizeof(szPassword);
#endif

        lpwfs->lpszUserName = wcsdup(L"anonymous");

#ifdef __REACTOS__
        res = RegOpenKeyW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings", &key);
        if (!res)
        {
            res = RegQueryValueExW(key, L"EmailName", NULL, &type, (LPBYTE)szPassword, &len);
            if (!res && (type != REG_SZ || len % sizeof(WCHAR))) res = ERROR_INVALID_DATA;
            if (!res) szPassword[len / sizeof(WCHAR)] = 0;
            RegCloseKey(key);
        }
        if (res) {
#else
        RegOpenKeyW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings", &key);
        if (RegQueryValueExW(key, L"EmailName", NULL, NULL, (LPBYTE)szPassword, &len)) {
#endif
            /* Nothing in the registry, get the username and use that as the password */
#ifdef __REACTOS__
            len = ARRAY_SIZE(szPassword);
#endif
            if (!GetUserNameW(szPassword, &len)) {
                /* Should never get here, but use an empty password as failsafe */
                lstrcpyW(szPassword, L"");
            }
        }
#ifndef __REACTOS__
        RegCloseKey(key);

#endif
        TRACE("Password used for anonymous ftp : (%s)\n", debugstr_w(szPassword));
        lpwfs->lpszPassword = wcsdup(szPassword);
    }
    else {
        lpwfs->lpszUserName = wcsdup(lpszUserName);
        lpwfs->lpszPassword = wcsdup(lpszPassword ? lpszPassword : L"");
    }
    lpwfs->servername = wcsdup(lpszServerName);
#ifdef __REACTOS__
    if (!lpwfs->lpszUserName || !lpwfs->lpszPassword || !lpwfs->servername)
    {
        INTERNET_SetLastError(ERROR_OUTOFMEMORY);
        goto lerror;
    }
#endif

    /* Don't send a handle created callback if this handle was created with InternetOpenUrl */
    if (!(lpwfs->hdr.dwInternalFlags & INET_OPENURL))
    {
        INTERNET_ASYNC_RESULT iar;

        iar.dwResult = (DWORD_PTR)lpwfs->hdr.hInternet;
        iar.dwError = ERROR_SUCCESS;

        INTERNET_SendCallback(&hIC->hdr, dwContext,
                      INTERNET_STATUS_HANDLE_CREATED, &iar,
                      sizeof(INTERNET_ASYNC_RESULT));
    }
        
    INTERNET_SendCallback(&hIC->hdr, dwContext, INTERNET_STATUS_RESOLVING_NAME,
        (LPWSTR) lpszServerName, (lstrlenW(lpszServerName)+1) * sizeof(WCHAR));

    if (!(server_addr = GetAddress(lpszServerName, lpwfs->serverport)))
    {
	INTERNET_SetLastError(ERROR_INTERNET_NAME_NOT_RESOLVED);
        goto lerror;
    }

    INTERNET_SendCallback(&hIC->hdr, dwContext, INTERNET_STATUS_NAME_RESOLVED,
                      server_addr->addr_str, strlen(server_addr->addr_str)+1);

    init_winsock();
    if ((nsocket = create_connect_socket(server_addr, AF_INET, INFINITE, &hIC->hdr, dwContext)) < 0)
    {
	ERR("Unable to connect (%d)\n", WSAGetLastError());
	INTERNET_SetLastError(ERROR_INTERNET_CANNOT_CONNECT);
    }
    else
    {
        TRACE("Connected to server\n");
	lpwfs->sndSocket = nsocket;
	sock_namelen = sizeof(lpwfs->socketAddress);
#ifdef __REACTOS__
        if (getsockname(nsocket, (struct sockaddr *) &lpwfs->socketAddress, &sock_namelen) == -1)
        {
            INTERNET_SetLastError(WSAGetLastError());
            goto lerror;
        }
#else
	getsockname(nsocket, (struct sockaddr *) &lpwfs->socketAddress, &sock_namelen);
#endif

        if (FTP_ConnectToHost(lpwfs))
        {
            TRACE("Successfully logged into server\n");
            bSuccess = TRUE;
        }
    }

lerror:
#ifdef __REACTOS__
    error = bSuccess ? ERROR_SUCCESS : INTERNET_GetLastError();
#endif
    free(server_addr);
#ifdef __REACTOS__
    if (bSuccess && !lpwfs->hdr.valid_handle)
#else
    if (!bSuccess)
#endif
    {
#ifdef __REACTOS__
        bSuccess = FALSE;
        error = ERROR_INTERNET_OPERATION_CANCELLED;
#else
        WININET_Release(&lpwfs->hdr);
        return NULL;
#endif
    }
#ifdef __REACTOS__
    handle = bSuccess ? lpwfs->hdr.hInternet : NULL;
    if (!bSuccess)
        InternetCloseHandle(lpwfs->hdr.hInternet);
    WININET_Release(&lpwfs->hdr);
    if (!bSuccess) INTERNET_SetLastError(error);
    return handle;
#else

    return lpwfs->hdr.hInternet;
#endif
}


/***********************************************************************
 *           FTP_ConnectToHost (internal)
 *
 * Connect to a ftp server
 *
 * RETURNS
 *   TRUE on success
 *   NULL on failure
 *
 */
static BOOL FTP_ConnectToHost(ftp_session_t *lpwfs)
{
    INT nResCode;
    BOOL bSuccess = FALSE;

    TRACE("\n");
    FTP_ReceiveResponse(lpwfs, lpwfs->hdr.dwContext);

    if (!FTP_SendCommand(lpwfs->sndSocket, FTP_CMD_USER, lpwfs->lpszUserName, 0, 0, 0))
        goto lend;

    nResCode = FTP_ReceiveResponse(lpwfs, lpwfs->hdr.dwContext);
    if (nResCode)
    {
        /* Login successful... */
        if (nResCode == 230)
            bSuccess = TRUE;
        /* User name okay, need password... */
        else if (nResCode == 331)
            bSuccess = FTP_SendPassword(lpwfs);
        /* Need account for login... */
        else if (nResCode == 332)
            bSuccess = FTP_SendAccount(lpwfs);
        else
            FTP_SetResponseError(nResCode);
    }

    TRACE("Returning %d\n", bSuccess);
lend:
    return bSuccess;
}

/***********************************************************************
 *           FTP_GetNextLine  (internal)
 *
 * Parse next line in directory string listing
 *
 * RETURNS
 *   Pointer to beginning of next line
 *   NULL on failure
 *
 */

static LPSTR FTP_GetNextLine(INT nSocket, LPDWORD dwLen)
{
    struct timeval tv = {RESPONSE_TIMEOUT,0};
    FD_SET set;
    INT nRecv = 0;
#ifdef __REACTOS__
    LPSTR lpszBuffer;
#else
    LPSTR lpszBuffer = INTERNET_GetResponseBuffer();
#endif

    TRACE("\n");

#ifdef __REACTOS__
    lpszBuffer = INTERNET_GetResponseBuffer();
    if (!lpszBuffer) return NULL;
    lpszBuffer[0] = '\0';

#endif
    FD_ZERO(&set);
    FD_SET(nSocket, &set);

#ifdef __REACTOS__
    for (;;)
#else
    while (nRecv < MAX_REPLY_LEN)
#endif
    {
#ifdef __REACTOS__
        char ch;
        int res = select(0, &set, NULL, NULL, &tv);

        if (res > 0)
#else
        if (select(nSocket+1, &set, NULL, NULL, &tv) > 0)
#endif
        {
#ifdef __REACTOS__
            res = sock_recv(nSocket, &ch, 1, 0);
            if (res <= 0)
#else
            if (sock_recv(nSocket, &lpszBuffer[nRecv], 1, 0) <= 0)
#endif
            {
#ifdef __REACTOS__
                INTERNET_SetLastError(res < 0 ? WSAGetLastError() :
                                      nRecv ? ERROR_INTERNET_CONNECTION_ABORTED : ERROR_NO_MORE_FILES);
#else
                INTERNET_SetLastError(ERROR_FTP_TRANSFER_IN_PROGRESS);
#endif
                return NULL;
            }

#ifdef __REACTOS__
            if (ch == '\n')
#else
            if (lpszBuffer[nRecv] == '\n')
#endif
            {
#ifdef __REACTOS__
                *dwLen = nRecv;
#else
                lpszBuffer[nRecv] = '\0';
                *dwLen = nRecv - 1;
#endif
                TRACE(":%d %s\n", nRecv, lpszBuffer);
                return lpszBuffer;
            }
#ifdef __REACTOS__
            if (ch != '\r')
            {
                if (nRecv == MAX_REPLY_LEN - 1)
                {
                    INTERNET_SetLastError(ERROR_INTERNET_EXTENDED_ERROR);
                    return NULL;
                }
                lpszBuffer[nRecv++] = ch;
                lpszBuffer[nRecv] = '\0';
            }
#else
            if (lpszBuffer[nRecv] != '\r')
                nRecv++;
#endif
        }
	else
	{
#ifdef __REACTOS__
            INTERNET_SetLastError(res < 0 ? WSAGetLastError() : ERROR_INTERNET_TIMEOUT);
#else
            INTERNET_SetLastError(ERROR_INTERNET_TIMEOUT);
#endif
            return NULL;
        }
    }
#ifndef __REACTOS__

    return NULL;
#endif
}

/***********************************************************************
 *           FTP_SendCommandA (internal)
 *
 * Send command to server
 *
 * RETURNS
 *   TRUE on success
 *   NULL on failure
 *
 */
static BOOL FTP_SendCommandA(INT nSocket, FTP_COMMAND ftpCmd, LPCSTR lpszParam,
	INTERNET_STATUS_CALLBACK lpfnStatusCB, object_header_t *hdr, DWORD_PTR dwContext)
{
#ifdef __REACTOS__
	size_t len, param_len;
#else
    	DWORD len;
#endif
	CHAR *buf;
	DWORD nBytesSent = 0;
#ifdef __REACTOS__
        DWORD error = ERROR_SUCCESS;
#else
	int nRC = 0;
	DWORD dwParamLen;
#endif

	TRACE("%d: (%s) %d\n", ftpCmd, debugstr_a(lpszParam), nSocket);

	if (lpfnStatusCB)
        {
            lpfnStatusCB(hdr->hInternet, dwContext, INTERNET_STATUS_SENDING_REQUEST, NULL, 0);
        }

#ifdef __REACTOS__
	param_len = lpszParam ? strlen(lpszParam) : 0;
	len = strlen(szFtpCommands[ftpCmd]) + strlen(szCRLF) + !!lpszParam;
        if (param_len > INT_MAX - len)
        {
            INTERNET_SetLastError(ERROR_OUTOFMEMORY);
            return FALSE;
        }
        len += param_len;
#else
	dwParamLen = lpszParam?strlen(lpszParam)+1:0;
	len = dwParamLen + strlen(szFtpCommands[ftpCmd]) + strlen(szCRLF);
#endif
	if (NULL == (buf = malloc(len + 1)))
	{
	    INTERNET_SetLastError(ERROR_OUTOFMEMORY);
	    return FALSE;
	}
#ifdef __REACTOS__
	sprintf(buf, "%s%s%s%s", szFtpCommands[ftpCmd], lpszParam ? " " : "",
		lpszParam ? lpszParam : "", szCRLF);
#else
	sprintf(buf, "%s%s%s%s", szFtpCommands[ftpCmd], dwParamLen ? " " : "",
		dwParamLen ? lpszParam : "", szCRLF);
#endif

#ifdef __REACTOS__
	TRACE("Sending (%s) len(%Iu)\n", debugstr_a(buf), len);
	while (nBytesSent < len)
#else
	TRACE("Sending (%s) len(%ld)\n", debugstr_a(buf), len);
	while((nBytesSent < len) && (nRC != -1))
#endif
	{
#ifdef __REACTOS__
		int nRC = sock_send(nSocket, buf+nBytesSent, len - nBytesSent, 0);

                if (nRC <= 0)
                {
                    error = nRC < 0 ? WSAGetLastError() : ERROR_INTERNET_CONNECTION_ABORTED;
                    break;
                }
#else
		nRC = sock_send(nSocket, buf+nBytesSent, len - nBytesSent, 0);
#endif
		nBytesSent += nRC;
	}
    free(buf);

	if (lpfnStatusCB)
        {
            lpfnStatusCB(hdr->hInternet, dwContext, INTERNET_STATUS_REQUEST_SENT,
                         &nBytesSent, sizeof(DWORD));
        }

	TRACE("Sent %ld bytes\n", nBytesSent);
#ifdef __REACTOS__
        if (error != ERROR_SUCCESS) INTERNET_SetLastError(error);
	return error == ERROR_SUCCESS;
#else
	return (nRC != -1);
#endif
}

/***********************************************************************
 *           FTP_SendCommand (internal)
 *
 * Send command to server
 *
 * RETURNS
 *   TRUE on success
 *   NULL on failure
 *
 */
static BOOL FTP_SendCommand(INT nSocket, FTP_COMMAND ftpCmd, LPCWSTR lpszParam,
	INTERNET_STATUS_CALLBACK lpfnStatusCB, object_header_t *hdr, DWORD_PTR dwContext)
{
    BOOL ret;
    char *lpszParamA = strdupWtoA(lpszParam);
#ifdef __REACTOS__
    if (lpszParam && !lpszParamA)
    {
        INTERNET_SetLastError(ERROR_OUTOFMEMORY);
        return FALSE;
    }
#endif
    ret = FTP_SendCommandA(nSocket, ftpCmd, lpszParamA, lpfnStatusCB, hdr, dwContext);
    free(lpszParamA);
    return ret;
}

/***********************************************************************
 *           FTP_ReceiveResponse (internal)
 *
 * Receive response from server
 *
 * RETURNS
 *   Reply code on success
 *   0 on failure
 *
 */
INT FTP_ReceiveResponse(ftp_session_t *lpwfs, DWORD_PTR dwContext)
{
#ifdef __REACTOS__
    LPSTR lpszResponse;
#else
    LPSTR lpszResponse = INTERNET_GetResponseBuffer();
#endif
    DWORD nRecv;
#ifdef __REACTOS__
    DWORD error = ERROR_SUCCESS;
#endif
    INT rc = 0;
    char firstprefix[5];
    BOOL multiline = FALSE;

    TRACE("socket(%d)\n", lpwfs->sndSocket);

    INTERNET_SendCallback(&lpwfs->hdr, dwContext, INTERNET_STATUS_RECEIVING_RESPONSE, NULL, 0);

    while(1)
    {
#ifdef __REACTOS__
	lpszResponse = FTP_GetNextLine(lpwfs->sndSocket, &nRecv);
        if (!lpszResponse)
        {
            error = INTERNET_GetLastError();
            if (error == ERROR_NO_MORE_FILES) error = ERROR_INTERNET_CONNECTION_ABORTED;
            goto lerror;
        }
#else
	if (!FTP_GetNextLine(lpwfs->sndSocket, &nRecv))
	    goto lerror;
#endif

#ifdef __REACTOS__
        if (nRecv >= 4)
#else
        if (nRecv >= 3)
#endif
	{
	    if(!multiline)
	    {
#ifdef __REACTOS__
                if (lpszResponse[0] < '1' || lpszResponse[0] > '5' ||
                    lpszResponse[1] < '0' || lpszResponse[1] > '9' ||
                    lpszResponse[2] < '0' || lpszResponse[2] > '9' ||
                    (lpszResponse[3] != ' ' && lpszResponse[3] != '-'))
                {
                    error = ERROR_INTERNET_EXTENDED_ERROR;
                    goto lerror;
                }
#endif
	        if(lpszResponse[3] != '-')
		    break;
		else
		{  /* Start of multiline response.  Loop until we get "nnn " */
		    multiline = TRUE;
		    memcpy(firstprefix, lpszResponse, 3);
		    firstprefix[3] = ' ';
		    firstprefix[4] = '\0';
		}
	    }
	    else
	    {
	        if(!memcmp(firstprefix, lpszResponse, 4))
		    break;
	    }
	}
#ifdef __REACTOS__
        else if (!multiline)
        {
            error = ERROR_INTERNET_EXTENDED_ERROR;
            goto lerror;
        }
#endif
    }

#ifdef __REACTOS__
    if (nRecv >= 4)
#else
    if (nRecv >= 3)
#endif
    {
#ifdef __REACTOS__
        rc = (lpszResponse[0] - '0') * 100 + (lpszResponse[1] - '0') * 10 + lpszResponse[2] - '0';
#else
        rc = atoi(lpszResponse);
#endif

        INTERNET_SendCallback(&lpwfs->hdr, dwContext, INTERNET_STATUS_RESPONSE_RECEIVED,
		    &nRecv, sizeof(DWORD));
    }

lerror:
    TRACE("return %d\n", rc);
#ifdef __REACTOS__
    if (!rc) INTERNET_SetLastError(error);
#endif
    return rc;
}


/***********************************************************************
 *           FTP_SendPassword (internal)
 *
 * Send password to ftp server
 *
 * RETURNS
 *   TRUE on success
 *   NULL on failure
 *
 */
static BOOL FTP_SendPassword(ftp_session_t *lpwfs)
{
    INT nResCode;
    BOOL bSuccess = FALSE;

    TRACE("\n");
    if (!FTP_SendCommand(lpwfs->sndSocket, FTP_CMD_PASS, lpwfs->lpszPassword, 0, 0, 0))
        goto lend;

    nResCode = FTP_ReceiveResponse(lpwfs, lpwfs->hdr.dwContext);
    if (nResCode)
    {
        TRACE("Received reply code %d\n", nResCode);
        /* Login successful... */
        if (nResCode == 230)
            bSuccess = TRUE;
        /* Command not implemented, superfluous at the server site... */
        /* Need account for login... */
        else if (nResCode == 332)
            bSuccess = FTP_SendAccount(lpwfs);
        else
            FTP_SetResponseError(nResCode);
    }

lend:
    TRACE("Returning %d\n", bSuccess);
    return bSuccess;
}


/***********************************************************************
 *           FTP_SendAccount (internal)
 *
 *
 *
 * RETURNS
 *   TRUE on success
 *   FALSE on failure
 *
 */
static BOOL FTP_SendAccount(ftp_session_t *lpwfs)
{
    INT nResCode;
    BOOL bSuccess = FALSE;

    TRACE("\n");
    if (!FTP_SendCommand(lpwfs->sndSocket, FTP_CMD_ACCT, L"noaccount", 0, 0, 0))
        goto lend;

    nResCode = FTP_ReceiveResponse(lpwfs, lpwfs->hdr.dwContext);
    if (nResCode)
        bSuccess = TRUE;
    else
        FTP_SetResponseError(nResCode);

lend:
    return bSuccess;
}


/***********************************************************************
 *           FTP_SendStore (internal)
 *
 * Send request to upload file to ftp server
 *
 * RETURNS
 *   TRUE on success
 *   FALSE on failure
 *
 */
static BOOL FTP_SendStore(ftp_session_t *lpwfs, LPCWSTR lpszRemoteFile, DWORD dwType)
{
    INT nResCode;
    BOOL bSuccess = FALSE;

    TRACE("\n");
    if (!FTP_InitListenSocket(lpwfs))
        goto lend;

    if (!FTP_SendType(lpwfs, dwType))
        goto lend;

    if (!FTP_SendPortOrPasv(lpwfs))
        goto lend;

    if (!FTP_SendCommand(lpwfs->sndSocket, FTP_CMD_STOR, lpszRemoteFile, 0, 0, 0))
	    goto lend;
    nResCode = FTP_ReceiveResponse(lpwfs, lpwfs->hdr.dwContext);
    if (nResCode)
    {
        if (nResCode == 150 || nResCode == 125)
            bSuccess = TRUE;
	else
            FTP_SetResponseError(nResCode);
    }

lend:
    if (!bSuccess && lpwfs->lstnSocket != -1)
    {
        closesocket(lpwfs->lstnSocket);
        lpwfs->lstnSocket = -1;
    }

    return bSuccess;
}


/***********************************************************************
 *           FTP_InitListenSocket (internal)
 *
 * Create a socket to listen for server response
 *
 * RETURNS
 *   TRUE on success
 *   FALSE on failure
 *
 */
static BOOL FTP_InitListenSocket(ftp_session_t *lpwfs)
{
    BOOL bSuccess = FALSE;
    socklen_t namelen = sizeof(lpwfs->lstnSocketAddress);

    TRACE("\n");

    init_winsock();
    lpwfs->lstnSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (lpwfs->lstnSocket == -1)
    {
#ifdef __REACTOS__
        INTERNET_SetLastError(WSAGetLastError());
#endif
        TRACE("Unable to create listening socket\n");
            goto lend;
    }

    /* We obtain our ip addr from the name of the command channel socket */
    lpwfs->lstnSocketAddress = lpwfs->socketAddress;

    /* and get the system to assign us a port */
    lpwfs->lstnSocketAddress.sin_port = htons(0);

    if (bind(lpwfs->lstnSocket,(struct sockaddr *) &lpwfs->lstnSocketAddress, sizeof(lpwfs->lstnSocketAddress)) == -1)
    {
#ifdef __REACTOS__
        INTERNET_SetLastError(WSAGetLastError());
#endif
        TRACE("Unable to bind socket\n");
        goto lend;
    }

    if (listen(lpwfs->lstnSocket, MAX_BACKLOG) == -1)
    {
#ifdef __REACTOS__
        INTERNET_SetLastError(WSAGetLastError());
#endif
        TRACE("listen failed\n");
        goto lend;
    }

    if (getsockname(lpwfs->lstnSocket, (struct sockaddr *) &lpwfs->lstnSocketAddress, &namelen) != -1)
        bSuccess = TRUE;
#ifdef __REACTOS__
    else
        INTERNET_SetLastError(WSAGetLastError());
#endif

lend:
    if (!bSuccess && lpwfs->lstnSocket != -1)
    {
        closesocket(lpwfs->lstnSocket);
        lpwfs->lstnSocket = -1;
    }

    return bSuccess;
}


/***********************************************************************
 *           FTP_SendType (internal)
 *
 * Tell server type of data being transferred
 *
 * RETURNS
 *   TRUE on success
 *   FALSE on failure
 *
 * W98SE doesn't cache the type that's currently set
 * (i.e. it sends it always),
 * so we probably don't want to do that either.
 */
static BOOL FTP_SendType(ftp_session_t *lpwfs, DWORD dwType)
{
    INT nResCode;
    WCHAR type[] = L"I";
    BOOL bSuccess = FALSE;

    TRACE("\n");
    if (dwType & INTERNET_FLAG_TRANSFER_ASCII)
        type[0] = 'A';

    if (!FTP_SendCommand(lpwfs->sndSocket, FTP_CMD_TYPE, type, 0, 0, 0))
        goto lend;

    nResCode = FTP_ReceiveResponse(lpwfs, lpwfs->hdr.dwContext)/100;
    if (nResCode)
    {
        if (nResCode == 2)
            bSuccess = TRUE;
	else
            FTP_SetResponseError(nResCode);
    }

lend:
    return bSuccess;
}


#if 0  /* FIXME: should probably be used for FtpGetFileSize */
/***********************************************************************
 *           FTP_GetFileSize (internal)
 *
 * Retrieves from the server the size of the given file
 *
 * RETURNS
 *   TRUE on success
 *   FALSE on failure
 *
 */
static BOOL FTP_GetFileSize(ftp_session_t *lpwfs, LPCWSTR lpszRemoteFile, DWORD *dwSize)
{
    INT nResCode;
    BOOL bSuccess = FALSE;

    TRACE("\n");

    if (!FTP_SendCommand(lpwfs->sndSocket, FTP_CMD_SIZE, lpszRemoteFile, 0, 0, 0))
        goto lend;

    nResCode = FTP_ReceiveResponse(lpwfs, lpwfs->hdr.dwContext);
    if (nResCode)
    {
        if (nResCode == 213) {
	    /* Now parses the output to get the actual file size */
	    int i;
	    LPSTR lpszResponseBuffer = INTERNET_GetResponseBuffer();

#ifdef __REACTOS__
	    if (!lpszResponseBuffer) return FALSE;
#endif
	    for (i = 0; (lpszResponseBuffer[i] != ' ') && (lpszResponseBuffer[i] != '\0'); i++) ;
	    if (lpszResponseBuffer[i] == '\0') return FALSE;
	    *dwSize = atol(&(lpszResponseBuffer[i + 1]));
	    
            bSuccess = TRUE;
	} else {
            FTP_SetResponseError(nResCode);
	}
    }

lend:
    return bSuccess;
}
#endif


/***********************************************************************
 *           FTP_SendPort (internal)
 *
 * Tell server which port to use
 *
 * RETURNS
 *   TRUE on success
 *   FALSE on failure
 *
 */
static BOOL FTP_SendPort(ftp_session_t *lpwfs)
{
    INT nResCode;
    WCHAR szIPAddress[64];
    BOOL bSuccess = FALSE;
    TRACE("\n");

    swprintf(szIPAddress, ARRAY_SIZE(szIPAddress), L"%d,%d,%d,%d,%d,%d",
	 lpwfs->lstnSocketAddress.sin_addr.S_un.S_addr&0x000000FF,
        (lpwfs->lstnSocketAddress.sin_addr.S_un.S_addr&0x0000FF00)>>8,
        (lpwfs->lstnSocketAddress.sin_addr.S_un.S_addr&0x00FF0000)>>16,
        (lpwfs->lstnSocketAddress.sin_addr.S_un.S_addr&0xFF000000)>>24,
        lpwfs->lstnSocketAddress.sin_port & 0xFF,
        (lpwfs->lstnSocketAddress.sin_port & 0xFF00)>>8);

    if (!FTP_SendCommand(lpwfs->sndSocket, FTP_CMD_PORT, szIPAddress, 0, 0, 0))
        goto lend;

    nResCode = FTP_ReceiveResponse(lpwfs, lpwfs->hdr.dwContext);
    if (nResCode)
    {
        if (nResCode == 200)
            bSuccess = TRUE;
        else
            FTP_SetResponseError(nResCode);
    }

lend:
    return bSuccess;
}


/***********************************************************************
 *           FTP_DoPassive (internal)
 *
 * Tell server that we want to do passive transfers
 * and connect data socket
 *
 * RETURNS
 *   TRUE on success
 *   FALSE on failure
 *
 */
static BOOL FTP_DoPassive(ftp_session_t *lpwfs)
{
    INT nResCode;
    BOOL bSuccess = FALSE;

    TRACE("\n");
    if (!FTP_SendCommand(lpwfs->sndSocket, FTP_CMD_PASV, NULL, 0, 0, 0))
        goto lend;

    nResCode = FTP_ReceiveResponse(lpwfs, lpwfs->hdr.dwContext);
    if (nResCode)
    {
        if (nResCode == 227)
	{
	    LPSTR lpszResponseBuffer = INTERNET_GetResponseBuffer();
	    LPSTR p;
	    int f[6];
	    int i;
	    char *pAddr, *pPort;
	    INT nsocket = -1;
	    struct sockaddr_in dataSocketAddress;

#ifdef __REACTOS__
	    if (!lpszResponseBuffer) goto lend;
#endif
	    p = lpszResponseBuffer+4; /* skip status code */
	    while (*p != '\0' && (*p < '0' || *p > '9')) p++;

	    if (*p == '\0')
	    {
#ifdef __REACTOS__
		INTERNET_SetLastError(ERROR_INTERNET_EXTENDED_ERROR);
#endif
		ERR("no address found in response, aborting\n");
		goto lend;
	    }

#ifdef __REACTOS__
            for (i = 0; i < 6; i++)
            {
                char *end;
                unsigned long value;

                while (isspace((unsigned char)*p)) p++;
                value = strtoul(p, &end, 10);
                if (*p < '0' || *p > '9' || value > 0xff || (i < 5 && *end != ','))
                {
                    INTERNET_SetLastError(ERROR_INTERNET_EXTENDED_ERROR);
                    ERR("unknown response address format '%s', aborting\n", p);
                    goto lend;
                }
                f[i] = value;
                p = end;
                if (i < 5) p++;
            }
#else
	    if (sscanf(p, "%d,%d,%d,%d,%d,%d",  &f[0], &f[1], &f[2], &f[3],
				    		&f[4], &f[5]) != 6)
	    {
		ERR("unknown response address format '%s', aborting\n", p);
		goto lend;
	    }
	    for (i=0; i < 6; i++)
		f[i] = f[i] & 0xff;
#endif

	    dataSocketAddress = lpwfs->socketAddress;
	    pAddr = (char *)&(dataSocketAddress.sin_addr.S_un.S_addr);
	    pPort = (char *)&(dataSocketAddress.sin_port);
            pAddr[0] = f[0];
            pAddr[1] = f[1];
            pAddr[2] = f[2];
            pAddr[3] = f[3];
	    pPort[0] = f[4];
	    pPort[1] = f[5];

            nsocket = socket(AF_INET,SOCK_STREAM,0);
            if (nsocket == -1)
#ifdef __REACTOS__
            {
                INTERNET_SetLastError(WSAGetLastError());
#endif
                goto lend;
#ifdef __REACTOS__
            }
#endif

	    if (connect(nsocket, (struct sockaddr *)&dataSocketAddress, sizeof(dataSocketAddress)))
            {
#ifdef __REACTOS__
                INTERNET_SetLastError(WSAGetLastError());
#endif
	        ERR("can't connect passive FTP data port.\n");
                closesocket(nsocket);
	        goto lend;
            }
	    lpwfs->pasvSocket = nsocket;
            bSuccess = TRUE;
	}
        else
            FTP_SetResponseError(nResCode);
    }

lend:
    return bSuccess;
}


static BOOL FTP_SendPortOrPasv(ftp_session_t *lpwfs)
{
    if (lpwfs->hdr.dwFlags & INTERNET_FLAG_PASSIVE)
    {
        if (!FTP_DoPassive(lpwfs))
            return FALSE;
    }
    else
    {
	if (!FTP_SendPort(lpwfs))
            return FALSE;
    }
    return TRUE;
}


/***********************************************************************
 *           FTP_GetDataSocket (internal)
 *
 * Either accepts an incoming data socket connection from the server
 * or just returns the already opened socket after a PASV command
 * in case of passive FTP.
 *
 *
 * RETURNS
 *   TRUE on success
 *   FALSE on failure
 *
 */
static BOOL FTP_GetDataSocket(ftp_session_t *lpwfs, LPINT nDataSocket)
{
    struct sockaddr_in saddr;
    socklen_t addrlen = sizeof(saddr);

    TRACE("\n");
    if (lpwfs->hdr.dwFlags & INTERNET_FLAG_PASSIVE)
    {
	*nDataSocket = lpwfs->pasvSocket;
	lpwfs->pasvSocket = -1;
#ifdef __REACTOS__
        if (*nDataSocket == -1) INTERNET_SetLastError(ERROR_INTERNET_INCORRECT_HANDLE_STATE);
#endif
    }
    else
    {
        *nDataSocket = accept(lpwfs->lstnSocket, (struct sockaddr *) &saddr, &addrlen);
#ifdef __REACTOS__
        if (*nDataSocket == -1) INTERNET_SetLastError(WSAGetLastError());
#endif
        closesocket(lpwfs->lstnSocket);
        lpwfs->lstnSocket = -1;
    }
    return *nDataSocket != -1;
}


/***********************************************************************
 *           FTP_SendData (internal)
 *
 * Send data to the server
 *
 * RETURNS
 *   TRUE on success
 *   FALSE on failure
 *
 */
static BOOL FTP_SendData(ftp_session_t *lpwfs, INT nDataSocket, HANDLE hFile)
{
    BY_HANDLE_FILE_INFORMATION fi;
#ifdef __REACTOS__
    BOOL have_file_info;
#endif
    DWORD nBytesRead = 0;
    DWORD nBytesSent = 0;
    DWORD nTotalSent = 0;
    DWORD nBytesToSend, nLen;
    int nRC = 1;
    time_t s_long_time, e_long_time;
    LONG nSeconds;
    CHAR *lpszBuffer;

    TRACE("\n");
    lpszBuffer = calloc(DATA_PACKET_SIZE, 1);
#ifdef __REACTOS__
    if (!lpszBuffer)
    {
        INTERNET_SetLastError(ERROR_OUTOFMEMORY);
        return FALSE;
    }
#endif

    /* Get the size of the file. */
#ifdef __REACTOS__
    have_file_info = GetFileInformationByHandle(hFile, &fi);
#else
    GetFileInformationByHandle(hFile, &fi);
#endif
    time(&s_long_time);

    do
    {
        nBytesToSend = nBytesRead - nBytesSent;

        if (nBytesToSend <= 0)
        {
            /* Read data from file. */
            nBytesSent = 0;
            if (!ReadFile(hFile, lpszBuffer, DATA_PACKET_SIZE, &nBytesRead, 0))
#ifdef __REACTOS__
            {
                INTERNET_SetLastError(GetLastError());
                ERR("Failed reading from file\n");
                nRC = -1;
                break;
            }
#else
            ERR("Failed reading from file\n");
#endif

            if (nBytesRead > 0)
                nBytesToSend = nBytesRead;
            else
                break;
        }

        nLen = DATA_PACKET_SIZE < nBytesToSend ?
            DATA_PACKET_SIZE : nBytesToSend;
#ifdef __REACTOS__
        nRC  = sock_send(nDataSocket, lpszBuffer + nBytesSent, nLen, 0);
#else
        nRC  = sock_send(nDataSocket, lpszBuffer, nLen, 0);
#endif

#ifdef __REACTOS__
        if (nRC > 0)
#else
        if (nRC != -1)
#endif
        {
            nBytesSent += nRC;
            nTotalSent += nRC;
        }
#ifdef __REACTOS__
        else
        {
            INTERNET_SetLastError(nRC == -1 ? WSAGetLastError() : ERROR_INTERNET_CONNECTION_ABORTED);
            nRC = -1;
            break;
        }
#endif

        /* Do some computation to display the status. */
#ifdef __REACTOS__
        if (!have_file_info || !fi.nFileSizeLow || !nTotalSent)
            continue;
#endif
        time(&e_long_time);
        nSeconds = e_long_time - s_long_time;
        if( nSeconds / 60 > 0 )
        {
            TRACE( "%ld bytes of %ld bytes (%ld%%) in %ld min %ld sec estimated remaining time %ld sec\n",
            nTotalSent, fi.nFileSizeLow, nTotalSent*100/fi.nFileSizeLow, nSeconds / 60,
            nSeconds % 60, (fi.nFileSizeLow - nTotalSent) * nSeconds / nTotalSent );
        }
        else
        {
            TRACE( "%ld bytes of %ld bytes (%ld%%) in %ld sec estimated remaining time %ld sec\n",
            nTotalSent, fi.nFileSizeLow, nTotalSent*100/fi.nFileSizeLow, nSeconds,
            (fi.nFileSizeLow - nTotalSent) * nSeconds / nTotalSent);
        }
    } while (nRC != -1);

    TRACE("file transfer complete!\n");

    free(lpszBuffer);
#ifdef __REACTOS__
    return nRC != -1;
#else
    return nTotalSent;
#endif
}


/***********************************************************************
 *           FTP_SendRetrieve (internal)
 *
 * Send request to retrieve a file
 *
 * RETURNS
 *   Number of bytes to be received on success
 *   0 on failure
 *
 */
static BOOL FTP_SendRetrieve(ftp_session_t *lpwfs, LPCWSTR lpszRemoteFile, DWORD dwType)
{
    INT nResCode;
    BOOL ret;

    TRACE("\n");
    if (!(ret = FTP_InitListenSocket(lpwfs)))
        goto lend;

    if (!(ret = FTP_SendType(lpwfs, dwType)))
        goto lend;

    if (!(ret = FTP_SendPortOrPasv(lpwfs)))
        goto lend;

    if (!(ret = FTP_SendCommand(lpwfs->sndSocket, FTP_CMD_RETR, lpszRemoteFile, 0, 0, 0)))
        goto lend;

    nResCode = FTP_ReceiveResponse(lpwfs, lpwfs->hdr.dwContext);
    if ((nResCode != 125) && (nResCode != 150)) {
	/* That means that we got an error getting the file. */
        FTP_SetResponseError(nResCode);
	ret = FALSE;
    }

lend:
    if (!ret && lpwfs->lstnSocket != -1)
    {
        closesocket(lpwfs->lstnSocket);
        lpwfs->lstnSocket = -1;
    }

    return ret;
}


/***********************************************************************
 *           FTP_RetrieveData  (internal)
 *
 * Retrieve data from server
 *
 * RETURNS
 *   TRUE on success
 *   FALSE on failure
 *
 */
static BOOL FTP_RetrieveFileData(ftp_session_t *lpwfs, INT nDataSocket, HANDLE hFile)
{
    DWORD nBytesWritten;
    INT nRC = 0;
    CHAR *lpszBuffer;

    TRACE("\n");

    lpszBuffer = calloc(DATA_PACKET_SIZE, 1);
    if (NULL == lpszBuffer)
    {
        INTERNET_SetLastError(ERROR_OUTOFMEMORY);
        return FALSE;
    }

    while (nRC != -1)
    {
        nRC = sock_recv(nDataSocket, lpszBuffer, DATA_PACKET_SIZE, 0);
        if (nRC != -1)
        {
#ifdef __REACTOS__
            DWORD offset = 0;

#endif
            /* other side closed socket. */
            if (nRC == 0)
                goto recv_end;
#ifdef __REACTOS__
            while (offset < nRC)
            {
                if (!WriteFile(hFile, lpszBuffer + offset, nRC - offset, &nBytesWritten, NULL))
                {
                    INTERNET_SetLastError(GetLastError());
                    nRC = -1;
                    goto recv_end;
                }
                if (!nBytesWritten)
                {
                    INTERNET_SetLastError(ERROR_WRITE_FAULT);
                    nRC = -1;
                    goto recv_end;
                }
                offset += nBytesWritten;
            }
#else
            WriteFile(hFile, lpszBuffer, nRC, &nBytesWritten, NULL);
#endif
        }
#ifdef __REACTOS__
        else
            INTERNET_SetLastError(WSAGetLastError());
#endif
    }

    TRACE("Data transfer complete\n");

recv_end:
    free(lpszBuffer);
    return (nRC != -1);
}

/***********************************************************************
 *           FTPFINDNEXT_Destroy (internal)
 *
 * Deallocate session handle
 */
static void FTPFINDNEXT_Destroy(object_header_t *hdr)
{
    LPWININETFTPFINDNEXTW lpwfn = (LPWININETFTPFINDNEXTW) hdr;
    DWORD i;

    TRACE("\n");

    WININET_Release(&lpwfn->lpFtpSession->hdr);

    for (i = 0; i < lpwfn->size; i++)
    {
        free(lpwfn->lpafp[i].lpszName);
    }
    free(lpwfn->lpafp);
}

#ifdef __REACTOS__
static DWORD FTPFINDNEXT_FindNextFileProc(WININETFTPFINDNEXTW *find, LPVOID data, BOOL unicode)
#else
static DWORD FTPFINDNEXT_FindNextFileProc(WININETFTPFINDNEXTW *find, LPVOID data)
#endif
{
#ifdef __REACTOS__
    WIN32_FIND_DATAW converted_data, *find_data = unicode ? data : &converted_data;
#else
    WIN32_FIND_DATAW *find_data = data;
#endif
    DWORD res = ERROR_SUCCESS;

    TRACE("index(%ld) size(%ld)\n", find->index, find->size);

    ZeroMemory(find_data, sizeof(WIN32_FIND_DATAW));

    if (find->index < find->size) {
        FTP_ConvertFileProp(&find->lpafp[find->index], find_data);
        find->index++;

        TRACE("Name: %s\nSize: %ld\n", debugstr_w(find_data->cFileName), find_data->nFileSizeLow);
#ifdef __REACTOS__
        if (!unicode)
            WININET_find_data_WtoA(find_data, data);
#endif
    }else {
        res = ERROR_NO_MORE_FILES;
    }

#ifdef __REACTOS__
    if (find->lpFtpSession->lpAppInfo->hdr.dwFlags & INTERNET_FLAG_ASYNC)
#else
    if (find->hdr.dwFlags & INTERNET_FLAG_ASYNC)
#endif
    {
        INTERNET_ASYNC_RESULT iar;

        iar.dwResult = (res == ERROR_SUCCESS);
        iar.dwError = res;

        INTERNET_SendCallback(&find->hdr, find->hdr.dwContext,
                              INTERNET_STATUS_REQUEST_COMPLETE, &iar,
                              sizeof(INTERNET_ASYNC_RESULT));
    }

    return res;
}

typedef struct {
    task_header_t hdr;
#ifdef __REACTOS__
    void *find_data;
    BOOL unicode;
#else
    WIN32_FIND_DATAW *find_data;
#endif
} find_next_task_t;

static void FTPFINDNEXT_AsyncFindNextFileProc(task_header_t *hdr)
{
    find_next_task_t *task = (find_next_task_t*)hdr;

#ifdef __REACTOS__
    FTPFINDNEXT_FindNextFileProc((WININETFTPFINDNEXTW*)task->hdr.hdr, task->find_data, task->unicode);
#else
    FTPFINDNEXT_FindNextFileProc((WININETFTPFINDNEXTW*)task->hdr.hdr, task->find_data);
#endif
}

static DWORD FTPFINDNEXT_QueryOption(object_header_t *hdr, DWORD option, void *buffer, DWORD *size, BOOL unicode)
{
    switch(option) {
    case INTERNET_OPTION_HANDLE_TYPE:
        TRACE("INTERNET_OPTION_HANDLE_TYPE\n");

        if (*size < sizeof(ULONG))
            return ERROR_INSUFFICIENT_BUFFER;

        *size = sizeof(DWORD);
        *(DWORD*)buffer = INTERNET_HANDLE_TYPE_FTP_FIND;
        return ERROR_SUCCESS;
    }

    return INET_QueryOption(hdr, option, buffer, size, unicode);
}

#ifdef __REACTOS__
static DWORD FTPFINDNEXT_FindNextFileW(object_header_t *hdr, void *data, BOOL unicode)
#else
static DWORD FTPFINDNEXT_FindNextFileW(object_header_t *hdr, void *data)
#endif
{
    WININETFTPFINDNEXTW *find = (WININETFTPFINDNEXTW*)hdr;

    if (find->lpFtpSession->lpAppInfo->hdr.dwFlags & INTERNET_FLAG_ASYNC)
    {
        find_next_task_t *task;

        task = alloc_async_task(&find->hdr, FTPFINDNEXT_AsyncFindNextFileProc, sizeof(*task));
#ifdef __REACTOS__
        if (!task)
            return ERROR_OUTOFMEMORY;
#endif
        task->find_data = data;
#ifdef __REACTOS__
        task->unicode = unicode;
#endif

#ifdef __REACTOS__
        return INTERNET_AsyncCall(&task->hdr);
#else
        INTERNET_AsyncCall(&task->hdr);
        return ERROR_SUCCESS;
#endif
    }

#ifdef __REACTOS__
    return FTPFINDNEXT_FindNextFileProc(find, data, unicode);
#else
    return FTPFINDNEXT_FindNextFileProc(find, data);
#endif
}

static const object_vtbl_t FTPFINDNEXTVtbl = {
    FTPFINDNEXT_Destroy,
    NULL,
    FTPFINDNEXT_QueryOption,
    INET_SetOption,
    NULL,
    NULL,
    NULL,
    NULL,
    FTPFINDNEXT_FindNextFileW
};

/***********************************************************************
 *           FTP_ReceiveFileList (internal)
 *
 * Read file list from server
 *
 * RETURNS
 *   Handle to file list on success
 *   NULL on failure
 *
 */
static HINTERNET FTP_ReceiveFileList(ftp_session_t *lpwfs, INT nSocket, LPCWSTR lpszSearchFile,
	LPWIN32_FIND_DATAW lpFindFileData, DWORD_PTR dwContext)
{
    DWORD dwSize = 0;
    LPFILEPROPERTIESW lpafp = NULL;
    LPWININETFTPFINDNEXTW lpwfn = NULL;

    TRACE("(%p,%d,%s,%p,%08Ix)\n", lpwfs, nSocket, debugstr_w(lpszSearchFile), lpFindFileData, dwContext);

    if (FTP_ParseDirectory(lpwfs, nSocket, lpszSearchFile, &lpafp, &dwSize))
    {
        if(lpFindFileData)
            FTP_ConvertFileProp(lpafp, lpFindFileData);

        lpwfn = alloc_object(&lpwfs->hdr, &FTPFINDNEXTVtbl, sizeof(WININETFTPFINDNEXTW));
        if (lpwfn)
        {
            lpwfn->hdr.htype = WH_HFTPFINDNEXT;
            lpwfn->hdr.dwContext = dwContext;
            lpwfn->index = 1; /* Next index is 1 since we return index 0 */
            lpwfn->size = dwSize;
            lpwfn->lpafp = lpafp;

            WININET_AddRef( &lpwfs->hdr );
            lpwfn->lpFtpSession = lpwfs;
            list_add_head( &lpwfs->hdr.children, &lpwfn->hdr.entry );
        }
#ifdef __REACTOS__
        else
        {
            DWORD i;

            for (i = 0; i < dwSize; i++)
                free(lpafp[i].lpszName);
            free(lpafp);
            INTERNET_SetLastError(ERROR_OUTOFMEMORY);
        }
#endif
    }

    TRACE("Matched %ld files\n", dwSize);
    return lpwfn ? lpwfn->hdr.hInternet : NULL;
}


/***********************************************************************
 *           FTP_ConvertFileProp (internal)
 *
 * Converts FILEPROPERTIESW struct to WIN32_FIND_DATAA
 *
 * RETURNS
 *   TRUE on success
 *   FALSE on failure
 *
 */
static BOOL FTP_ConvertFileProp(LPFILEPROPERTIESW lpafp, LPWIN32_FIND_DATAW lpFindFileData)
{
    BOOL bSuccess = FALSE;

    ZeroMemory(lpFindFileData, sizeof(WIN32_FIND_DATAW));

    if (lpafp)
    {
        SystemTimeToFileTime( &lpafp->tmLastModified, &lpFindFileData->ftLastAccessTime );
	lpFindFileData->ftLastWriteTime = lpFindFileData->ftLastAccessTime;
	lpFindFileData->ftCreationTime = lpFindFileData->ftLastAccessTime;
	
        /* Not all fields are filled in */
        lpFindFileData->nFileSizeHigh = 0; /* We do not handle files bigger than 0xFFFFFFFF bytes yet :-) */
        lpFindFileData->nFileSizeLow = lpafp->nSize;

	if (lpafp->bIsDirectory)
	    lpFindFileData->dwFileAttributes |= FILE_ATTRIBUTE_DIRECTORY;

        if (lpafp->lpszName)
            lstrcpynW(lpFindFileData->cFileName, lpafp->lpszName, MAX_PATH);

	bSuccess = TRUE;
    }

    return bSuccess;
}

/***********************************************************************
 *           FTP_ParseNextFile (internal)
 *
 * Parse the next line in file listing
 *
 * RETURNS
 *   TRUE on success
 *   FALSE on failure
 */
#ifdef __REACTOS__
static DWORD FTP_ParseNextFile(INT nSocket, LPCWSTR lpszSearchFile, LPFILEPROPERTIESW lpfp)
#else
static BOOL FTP_ParseNextFile(INT nSocket, LPCWSTR lpszSearchFile, LPFILEPROPERTIESW lpfp)
#endif
{
    static const char szSpace[] = " \t";
    DWORD nBufLen;
    char *pszLine;
    char *pszToken;
    char *pszTmp;
    BOOL found = FALSE;
    int i;
    
    lpfp->lpszName = NULL;
    do {
        if(!(pszLine = FTP_GetNextLine(nSocket, &nBufLen)))
#ifdef __REACTOS__
            return INTERNET_GetLastError();
#else
            return FALSE;
#endif
    
        pszToken = strtok(pszLine, szSpace);
#ifdef __REACTOS__
        if (!pszToken)
            continue;
#endif
        /* ls format
         * <Permissions> <NoLinks> <owner>   <group> <size> <date>  <time or year> <filename>
         *
         * For instance:
         * drwx--s---     2         pcarrier  ens     512    Sep 28  1995           pcarrier
         */
#ifdef __REACTOS__
        if(!isdigit((unsigned char)pszToken[0]) && 10 == strlen(pszToken)) {
#else
        if(!isdigit(pszToken[0]) && 10 == strlen(pszToken)) {
#endif
            if(!FTP_ParsePermission(pszToken, lpfp))
                lpfp->bIsDirectory = FALSE;
            for(i=0; i<=3; i++) {
              if(!(pszToken = strtok(NULL, szSpace)))
                  break;
            }
            if(!pszToken) continue;
            if(lpfp->bIsDirectory) {
                TRACE("Is directory\n");
                lpfp->nSize = 0;
            }
            else {
                TRACE("Size: %s\n", pszToken);
                lpfp->nSize = atol(pszToken);
            }
            
            lpfp->tmLastModified.wSecond = 0;
            lpfp->tmLastModified.wMinute = 0;
            lpfp->tmLastModified.wHour   = 0;
            lpfp->tmLastModified.wDay    = 0;
            lpfp->tmLastModified.wMonth  = 0;
            lpfp->tmLastModified.wYear   = 0;
            
            /* Determine month */
            pszToken = strtok(NULL, szSpace);
            if(!pszToken) continue;
            if(strlen(pszToken) >= 3) {
                pszToken[3] = 0;
                if((pszTmp = StrStrIA(szMonths, pszToken)))
                    lpfp->tmLastModified.wMonth = ((pszTmp - szMonths) / 3)+1;
            }
            /* Determine day */
            pszToken = strtok(NULL, szSpace);
            if(!pszToken) continue;
            lpfp->tmLastModified.wDay = atoi(pszToken);
            /* Determine time or year */
            pszToken = strtok(NULL, szSpace);
            if(!pszToken) continue;
            if((pszTmp = strchr(pszToken, ':'))) {
                SYSTEMTIME curr_time;
                *pszTmp = 0;
                pszTmp++;
                lpfp->tmLastModified.wMinute = atoi(pszTmp);
                lpfp->tmLastModified.wHour = atoi(pszToken);
                GetLocalTime( &curr_time );
                lpfp->tmLastModified.wYear = curr_time.wYear;
            }
            else {
                lpfp->tmLastModified.wYear = atoi(pszToken);
                lpfp->tmLastModified.wHour = 12;
            }
            TRACE("Mod time: %02d:%02d:%02d  %04d/%02d/%02d\n",
                  lpfp->tmLastModified.wHour, lpfp->tmLastModified.wMinute, lpfp->tmLastModified.wSecond,
                  lpfp->tmLastModified.wYear, lpfp->tmLastModified.wMonth, lpfp->tmLastModified.wDay);

            pszToken = strtok(NULL, szSpace);
            if(!pszToken) continue;
            lpfp->lpszName = strdupAtoW(pszToken);
#ifdef __REACTOS__
            if (!lpfp->lpszName) return ERROR_OUTOFMEMORY;
#endif
            TRACE("File: %s\n", debugstr_w(lpfp->lpszName));
        }
        /* NT way of parsing ... :
            
                07-13-03  08:55PM       <DIR>          sakpatch
                05-09-03  06:02PM             12656686 2003-04-21bgm_cmd_e.rgz
        */
#ifdef __REACTOS__
        else if(isdigit((unsigned char)pszToken[0]) && 8 == strlen(pszToken)) {
#else
        else if(isdigit(pszToken[0]) && 8 == strlen(pszToken)) {
#endif
            int mon, mday, year, hour, min;
            lpfp->permissions = 0xFFFF; /* No idea, put full permission :-) */
            
#ifdef __REACTOS__
            if (sscanf(pszToken, "%d-%d-%d", &mon, &mday, &year) != 3)
                continue;
#else
            sscanf(pszToken, "%d-%d-%d", &mon, &mday, &year);
#endif
            lpfp->tmLastModified.wDay   = mday;
            lpfp->tmLastModified.wMonth = mon;
            lpfp->tmLastModified.wYear  = year;

            /* Hacky and bad Y2K protection :-) */
            if (lpfp->tmLastModified.wYear < 70) lpfp->tmLastModified.wYear += 2000;

            pszToken = strtok(NULL, szSpace);
            if(!pszToken) continue;
#ifdef __REACTOS__
            if (strlen(pszToken) < 7 || sscanf(pszToken, "%2d:%2d", &hour, &min) != 2)
                continue;
#else
            sscanf(pszToken, "%d:%d", &hour, &min);
#endif
            lpfp->tmLastModified.wHour   = hour;
            lpfp->tmLastModified.wMinute = min;
            if((pszToken[5] == 'P') && (pszToken[6] == 'M')) {
                lpfp->tmLastModified.wHour += 12;
            }
            lpfp->tmLastModified.wSecond = 0;

            TRACE("Mod time: %02d:%02d:%02d  %04d/%02d/%02d\n",
                  lpfp->tmLastModified.wHour, lpfp->tmLastModified.wMinute, lpfp->tmLastModified.wSecond,
                  lpfp->tmLastModified.wYear, lpfp->tmLastModified.wMonth, lpfp->tmLastModified.wDay);

            pszToken = strtok(NULL, szSpace);
            if(!pszToken) continue;
            if(!stricmp(pszToken, "<DIR>")) {
                lpfp->bIsDirectory = TRUE;
                lpfp->nSize = 0;
                TRACE("Is directory\n");
            }
            else {
                lpfp->bIsDirectory = FALSE;
                lpfp->nSize = atol(pszToken);
                TRACE("Size: %ld\n", lpfp->nSize);
            }
            
            pszToken = strtok(NULL, szSpace);
            if(!pszToken) continue;
            lpfp->lpszName = strdupAtoW(pszToken);
#ifdef __REACTOS__
            if (!lpfp->lpszName) return ERROR_OUTOFMEMORY;
#endif
            TRACE("Name: %s\n", debugstr_w(lpfp->lpszName));
        }
        /* EPLF format - http://cr.yp.to/ftp/list/eplf.html */
        else if(pszToken[0] == '+') {
            FIXME("EPLF Format not implemented\n");
        }
        
        if(lpfp->lpszName) {
            if((lpszSearchFile == NULL) ||
	       (PathMatchSpecW(lpfp->lpszName, lpszSearchFile))) {
                found = TRUE;
                TRACE("Matched: %s\n", debugstr_w(lpfp->lpszName));
            }
            else {
                free(lpfp->lpszName);
                lpfp->lpszName = NULL;
            }
        }
    } while(!found);
#ifdef __REACTOS__
    return ERROR_SUCCESS;
#else
    return TRUE;
#endif
}

/***********************************************************************
 *           FTP_ParseDirectory (internal)
 *
 * Parse string of directory information
 *
 * RETURNS
 *   TRUE on success
 *   FALSE on failure
 */
static BOOL FTP_ParseDirectory(ftp_session_t *lpwfs, INT nSocket, LPCWSTR lpszSearchFile,
    LPFILEPROPERTIESW *lpafp, LPDWORD dwfp)
{
    BOOL bSuccess = TRUE;
    INT sizeFilePropArray = 500;/*20; */
    INT indexFilePropArray = -1;
#ifdef __REACTOS__
    DWORD res = ERROR_NO_MORE_FILES;
#endif

    TRACE("\n");

    /* Allocate initial file properties array */
    *lpafp = calloc(sizeFilePropArray, sizeof(FILEPROPERTIESW));
    if (!*lpafp)
#ifdef __REACTOS__
    {
        INTERNET_SetLastError(ERROR_OUTOFMEMORY);
#endif
        return FALSE;
#ifdef __REACTOS__
    }
#endif

    do {
        if (indexFilePropArray+1 >= sizeFilePropArray)
        {
            LPFILEPROPERTIESW tmpafp;

#ifdef __REACTOS__
            if (sizeFilePropArray > MAXLONG / 2 ||
                (size_t)sizeFilePropArray > (size_t)-1 / sizeof(FILEPROPERTIESW) / 2)
            {
                bSuccess = FALSE;
                res = ERROR_OUTOFMEMORY;
                break;
            }
#endif
            tmpafp = realloc(*lpafp, sizeof(FILEPROPERTIESW) * sizeFilePropArray * 2);
            if (NULL == tmpafp)
            {
                bSuccess = FALSE;
#ifdef __REACTOS__
                res = ERROR_OUTOFMEMORY;
#endif
                break;
            }
            memset(tmpafp + sizeFilePropArray, 0, sizeof(FILEPROPERTIESW) * sizeFilePropArray);

            *lpafp = tmpafp;
            sizeFilePropArray *= 2;
        }
        indexFilePropArray++;
#ifdef __REACTOS__
        res = FTP_ParseNextFile(nSocket, lpszSearchFile, &(*lpafp)[indexFilePropArray]);
    } while (res == ERROR_SUCCESS);

    if (res != ERROR_NO_MORE_FILES) bSuccess = FALSE;
#else
    } while (FTP_ParseNextFile(nSocket, lpszSearchFile, &(*lpafp)[indexFilePropArray]));
#endif

    if (bSuccess && indexFilePropArray)
    {
        if (indexFilePropArray < sizeFilePropArray - 1)
        {
            LPFILEPROPERTIESW tmpafp;

            tmpafp = realloc(*lpafp, sizeof(FILEPROPERTIESW) * indexFilePropArray);
            if (NULL != tmpafp)
                *lpafp = tmpafp;
        }
        *dwfp = indexFilePropArray;
    }
    else
    {
#ifdef __REACTOS__
        INT i;

        for (i = 0; i <= indexFilePropArray; i++)
            free((*lpafp)[i].lpszName);
#endif
        free(*lpafp);
#ifdef __REACTOS__
        *lpafp = NULL;
        INTERNET_SetLastError(bSuccess ? ERROR_NO_MORE_FILES : res);
#else
        INTERNET_SetLastError(ERROR_NO_MORE_FILES);
#endif
        bSuccess = FALSE;
    }

    return bSuccess;
}


/***********************************************************************
 *           FTP_ParsePermission (internal)
 *
 * Parse permission string of directory information
 *
 * RETURNS
 *   TRUE on success
 *   FALSE on failure
 *
 */
static BOOL FTP_ParsePermission(LPCSTR lpszPermission, LPFILEPROPERTIESW lpfp)
{
    BOOL bSuccess = TRUE;
    unsigned short nPermission = 0;
    INT nPos = 1;
    INT nLast  = 9;

    TRACE("\n");
    if ((*lpszPermission != 'd') && (*lpszPermission != '-') && (*lpszPermission != 'l'))
    {
        bSuccess = FALSE;
        return bSuccess;
    }

    lpfp->bIsDirectory = (*lpszPermission == 'd');
    do
    {
        switch (nPos)
        {
            case 1:
                nPermission |= (*(lpszPermission+1) == 'r' ? 1 : 0) << 8;
                break;
            case 2:
                nPermission |= (*(lpszPermission+2) == 'w' ? 1 : 0) << 7;
                break;
            case 3:
                nPermission |= (*(lpszPermission+3) == 'x' ? 1 : 0) << 6;
                break;
            case 4:
                nPermission |= (*(lpszPermission+4) == 'r' ? 1 : 0) << 5;
                break;
            case 5:
                nPermission |= (*(lpszPermission+5) == 'w' ? 1 : 0) << 4;
                break;
            case 6:
                nPermission |= (*(lpszPermission+6) == 'x' ? 1 : 0) << 3;
                break;
            case 7:
                nPermission |= (*(lpszPermission+7) == 'r' ? 1 : 0) << 2;
                break;
            case 8:
                nPermission |= (*(lpszPermission+8) == 'w' ? 1 : 0) << 1;
                break;
            case 9:
                nPermission |= (*(lpszPermission+9) == 'x' ? 1 : 0);
                break;
        }
        nPos++;
    }while (nPos <= nLast);

    lpfp->permissions = nPermission;
    return bSuccess;
}


/***********************************************************************
 *           FTP_SetResponseError (internal)
 *
 * Set the appropriate error code for a given response from the server
 *
 * RETURNS
 *
 */
static DWORD FTP_SetResponseError(DWORD dwResponse)
{
    DWORD dwCode = 0;

    switch(dwResponse)
    {
    case 425: /* Cannot open data connection. */
        dwCode = ERROR_INTERNET_CANNOT_CONNECT;
        break;

    case 426: /* Connection closed, transfer aborted. */
        dwCode = ERROR_INTERNET_CONNECTION_ABORTED;
        break;

    case 530: /* Not logged in. Login incorrect. */
        dwCode = ERROR_INTERNET_LOGIN_FAILURE;
        break;

    case 421: /* Service not available - Server may be shutting down. */
    case 450: /* File action not taken. File may be busy. */
    case 451: /* Action aborted. Server error. */
    case 452: /* Action not taken. Insufficient storage space on server. */
    case 500: /* Syntax error. Command unrecognized. */
    case 501: /* Syntax error. Error in parameters or arguments. */
    case 502: /* Command not implemented. */
    case 503: /* Bad sequence of commands. */
    case 504: /* Command not implemented for that parameter. */
    case 532: /* Need account for storing files */
    case 550: /* File action not taken. File not found or no access. */
    case 551: /* Requested action aborted. Page type unknown */
    case 552: /* Action aborted. Exceeded storage allocation */
    case 553: /* Action not taken. File name not allowed. */

    default:
        dwCode = ERROR_INTERNET_EXTENDED_ERROR;
        break;
    }

    INTERNET_SetLastError(dwCode);
    return dwCode;
}
