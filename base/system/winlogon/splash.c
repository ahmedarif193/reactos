/*
 * PROJECT:     ReactOS Winlogon
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Experimental early desktop PNG background
 */

#include "winlogon.h"

#include <reactos/early_splash.h>
#include <png.h>
#include <setjmp.h>

#define EARLY_SPLASH_MAX_DIMENSION 8192
#define EARLY_SPLASH_MAX_FILE_SIZE (32 * 1024 * 1024)

typedef struct _EARLY_SPLASH_READER
{
    const BYTE *Data;
    SIZE_T Size;
    SIZE_T Offset;
} EARLY_SPLASH_READER, *PEARLY_SPLASH_READER;

typedef struct _EARLY_SPLASH_DATA
{
    PBYTE Bits;
    ULONG Width;
    ULONG Height;
    BITMAPINFO BitmapInfo;
    BOOL Active;
} EARLY_SPLASH_DATA, *PEARLY_SPLASH_DATA;

static EARLY_SPLASH_DATA EarlySplash;

static VOID PNGAPI
EarlySplashPngError(
    _In_ png_structp Png,
    _In_ png_const_charp Message)
{
    ERR("EARLY_SPLASH: PNG_ERROR message=%s\n", Message);
    longjmp(png_jmpbuf(Png), 1);
}

static VOID PNGAPI
EarlySplashPngWarning(
    _In_ png_structp Png,
    _In_ png_const_charp Message)
{
    UNREFERENCED_PARAMETER(Png);
    WARN("EARLY_SPLASH: PNG_WARNING message=%s\n", Message);
}

static VOID PNGAPI
EarlySplashReadPng(
    _In_ png_structp Png,
    _Out_writes_bytes_(Length) png_bytep Buffer,
    _In_ png_size_t Length)
{
    PEARLY_SPLASH_READER Reader = png_get_io_ptr(Png);

    if (Reader->Offset > Reader->Size || Length > Reader->Size - Reader->Offset)
    {
        ERR("EARLY_SPLASH: PNG_READ_FAIL requested=%Iu remaining=%Iu\n", (SIZE_T)Length, Reader->Offset <= Reader->Size ? Reader->Size - Reader->Offset : 0);
        png_error(Png, "Unexpected end of early splash PNG");
    }

    CopyMemory(Buffer, Reader->Data + Reader->Offset, Length);
    Reader->Offset += Length;
}

static BOOL
EarlySplashLoadPng(
    _In_ PCWSTR Path,
    _Out_ PEARLY_SPLASH_DATA Splash)
{
    EARLY_SPLASH_READER Reader;
    HANDLE File = INVALID_HANDLE_VALUE;
    png_structp Png = NULL;
    png_infop Info = NULL;
    png_bytep *Rows = NULL;
    PBYTE FileData = NULL;
    png_bytep TransparentAlpha;
    png_color_16p TransparentColor;
    png_uint_32 Width, Height;
    png_size_t RowBytes;
    LARGE_INTEGER FileSize;
    BYTE Signature[8];
    DWORD BytesRead;
    SIZE_T BitsSize, RowsSize;
    int BitDepth, ColorType, InterlaceMethod, Channels, TransparentCount;
    BOOL HasAlpha, Result = FALSE;
    ULONG Row;

    File = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (File == INVALID_HANDLE_VALUE)
    {
        ERR("EARLY_SPLASH: LOAD_FAIL stage=open path=%S error=%lu\n", Path, GetLastError());
        return FALSE;
    }

    if (!GetFileSizeEx(File, &FileSize) || FileSize.QuadPart < sizeof(Signature) || FileSize.QuadPart > EARLY_SPLASH_MAX_FILE_SIZE)
    {
        ERR("EARLY_SPLASH: LOAD_FAIL stage=size path=%S error=%lu\n", Path, GetLastError());
        goto cleanup;
    }

    FileData = HeapAlloc(GetProcessHeap(), 0, FileSize.LowPart);
    if (!FileData)
    {
        ERR("EARLY_SPLASH: LOAD_FAIL stage=file_allocate bytes=%lu\n", FileSize.LowPart);
        goto cleanup;
    }

    if (!ReadFile(File, FileData, FileSize.LowPart, &BytesRead, NULL) || BytesRead != FileSize.LowPart)
    {
        ERR("EARLY_SPLASH: LOAD_FAIL stage=file_read requested=%lu read=%lu error=%lu\n", FileSize.LowPart, BytesRead, GetLastError());
        goto cleanup;
    }
    CloseHandle(File);
    File = INVALID_HANDLE_VALUE;

    CopyMemory(Signature, FileData, sizeof(Signature));
    if (png_sig_cmp(Signature, 0, sizeof(Signature)))
    {
        ERR("EARLY_SPLASH: LOAD_FAIL stage=signature path=%S\n", Path);
        goto cleanup;
    }

    Png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, EarlySplashPngError, EarlySplashPngWarning);
    if (!Png)
    {
        ERR("EARLY_SPLASH: LOAD_FAIL stage=png_create_read_struct path=%S\n", Path);
        goto cleanup;
    }

    Info = png_create_info_struct(Png);
    if (!Info)
    {
        ERR("EARLY_SPLASH: LOAD_FAIL stage=png_create_info_struct path=%S\n", Path);
        goto cleanup;
    }

    if (setjmp(png_jmpbuf(Png)))
    {
        ERR("EARLY_SPLASH: LOAD_FAIL stage=decode path=%S\n", Path);
        goto cleanup;
    }

    Reader.Data = FileData;
    Reader.Size = FileSize.LowPart;
    Reader.Offset = sizeof(Signature);
    png_set_read_fn(Png, &Reader, EarlySplashReadPng);
    png_set_sig_bytes(Png, sizeof(Signature));
    png_read_info(Png, Info);
    if (!png_get_IHDR(Png, Info, &Width, &Height, &BitDepth, &ColorType, &InterlaceMethod, NULL, NULL))
    {
        ERR("EARLY_SPLASH: LOAD_FAIL stage=header path=%S\n", Path);
        goto cleanup;
    }

    if (!Width || !Height || Width > EARLY_SPLASH_MAX_DIMENSION || Height > EARLY_SPLASH_MAX_DIMENSION || InterlaceMethod != PNG_INTERLACE_NONE)
    {
        ERR("EARLY_SPLASH: LOAD_FAIL stage=dimensions width=%lu height=%lu interlace=%d\n", (ULONG)Width, (ULONG)Height, InterlaceMethod);
        goto cleanup;
    }

    if ((ColorType == PNG_COLOR_TYPE_GRAY || ColorType == PNG_COLOR_TYPE_GRAY_ALPHA) && BitDepth < 8)
    {
        ERR("EARLY_SPLASH: LOAD_FAIL stage=gray_depth depth=%d\n", BitDepth);
        goto cleanup;
    }

    TransparentAlpha = NULL;
    TransparentColor = NULL;
    TransparentCount = 0;
    HasAlpha = ((ColorType & PNG_COLOR_MASK_ALPHA) != 0);
    if (png_get_tRNS(Png, Info, &TransparentAlpha, &TransparentCount, &TransparentColor))
    {
        png_set_tRNS_to_alpha(Png);
        HasAlpha = TRUE;
    }
    if (ColorType == PNG_COLOR_TYPE_PALETTE)
        png_set_palette_to_rgb(Png);
    if (ColorType == PNG_COLOR_TYPE_GRAY || ColorType == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(Png);
    if (BitDepth == 16)
        png_set_scale_16(Png);
    if (!HasAlpha)
        png_set_filler(Png, 0xff, PNG_FILLER_AFTER);
    png_set_bgr(Png);
    png_read_update_info(Png, Info);

    Channels = png_get_channels(Png, Info);
    RowBytes = png_get_rowbytes(Png, Info);
    if (Channels != 4 || RowBytes != (png_size_t)Width * 4)
    {
        ERR("EARLY_SPLASH: LOAD_FAIL stage=format channels=%d rowbytes=%Iu\n", Channels, (SIZE_T)RowBytes);
        goto cleanup;
    }

    if (Height > MAXULONG_PTR / RowBytes)
    {
        ERR("EARLY_SPLASH: LOAD_FAIL stage=overflow width=%lu height=%lu\n", (ULONG)Width, (ULONG)Height);
        goto cleanup;
    }
    BitsSize = RowBytes * Height;
    RowsSize = sizeof(*Rows) * Height;
    Splash->Bits = HeapAlloc(GetProcessHeap(), 0, BitsSize);
    Rows = HeapAlloc(GetProcessHeap(), 0, RowsSize);
    if (!Splash->Bits || !Rows)
    {
        ERR("EARLY_SPLASH: LOAD_FAIL stage=allocate bytes=%Iu\n", BitsSize + RowsSize);
        goto cleanup;
    }

    for (Row = 0; Row < Height; ++Row)
        Rows[Row] = Splash->Bits + Row * RowBytes;
    png_read_image(Png, Rows);
    png_read_end(Png, Info);

    Splash->Width = Width;
    Splash->Height = Height;
    ZeroMemory(&Splash->BitmapInfo, sizeof(Splash->BitmapInfo));
    Splash->BitmapInfo.bmiHeader.biSize = sizeof(Splash->BitmapInfo.bmiHeader);
    Splash->BitmapInfo.bmiHeader.biWidth = Width;
    Splash->BitmapInfo.bmiHeader.biHeight = -(LONG)Height;
    Splash->BitmapInfo.bmiHeader.biPlanes = 1;
    Splash->BitmapInfo.bmiHeader.biBitCount = 32;
    Splash->BitmapInfo.bmiHeader.biCompression = BI_RGB;
    Splash->BitmapInfo.bmiHeader.biSizeImage = BitsSize;
    Result = TRUE;
    TRACE("EARLY_SPLASH: DECODE_OK path=%S file_bytes=%lu width=%lu height=%lu decoded_bytes=%Iu\n", Path, FileSize.LowPart, Splash->Width, Splash->Height, BitsSize);

cleanup:
    if (Rows)
        HeapFree(GetProcessHeap(), 0, Rows);
    if (Png)
        png_destroy_read_struct(&Png, Info ? &Info : NULL, NULL);
    if (FileData)
        HeapFree(GetProcessHeap(), 0, FileData);
    if (File != INVALID_HANDLE_VALUE)
        CloseHandle(File);
    if (!Result && Splash->Bits)
    {
        HeapFree(GetProcessHeap(), 0, Splash->Bits);
        Splash->Bits = NULL;
    }
    return Result;
}

BOOL
EarlySplashCreate(VOID)
{
    WCHAR Path[MAX_PATH];
    DWORD Length;
    HBITMAP Bitmap;
    HWND DesktopWindow;
    HDC DesktopDc;

    ZeroMemory(&EarlySplash, sizeof(EarlySplash));
    Length = GetWindowsDirectoryW(Path, ARRAYSIZE(Path));
    if (!Length || Length >= ARRAYSIZE(Path) || FAILED(StringCchCatW(Path, ARRAYSIZE(Path), REACTOS_EARLY_SPLASH_RELATIVE_PATH)))
    {
        ERR("EARLY_SPLASH: LOAD_FAIL stage=path error=%lu\n", GetLastError());
        return FALSE;
    }

    TRACE("EARLY_SPLASH: START path=%S\n", Path);
    if (!EarlySplashLoadPng(Path, &EarlySplash))
        return FALSE;

    DesktopWindow = GetDesktopWindow();
    DesktopDc = GetDC(DesktopWindow);
    if (!DesktopDc)
    {
        ERR("EARLY_SPLASH: WALLPAPER_FAIL stage=desktop_dc error=%lu\n", GetLastError());
        goto failure;
    }

    Bitmap = CreateDIBitmap(DesktopDc, &EarlySplash.BitmapInfo.bmiHeader, CBM_INIT, EarlySplash.Bits, &EarlySplash.BitmapInfo, DIB_RGB_COLORS);
    ReleaseDC(DesktopWindow, DesktopDc);
    if (!Bitmap)
    {
        ERR("EARLY_SPLASH: WALLPAPER_FAIL stage=create_bitmap error=%lu\n", GetLastError());
        goto failure;
    }

    if (!SystemParametersInfoW(REACTOS_SPI_SET_EARLY_WALLPAPER, 0, (PVOID)Bitmap, 0))
    {
        ERR("EARLY_SPLASH: WALLPAPER_FAIL stage=install error=%lu\n", GetLastError());
        DeleteObject(Bitmap);
        goto failure;
    }

    HeapFree(GetProcessHeap(), 0, EarlySplash.Bits);
    EarlySplash.Bits = NULL;
    EarlySplash.Active = TRUE;
    TRACE("EARLY_SPLASH: WALLPAPER_OK source=%lux%lu owner=win32k mode=stretch\n", EarlySplash.Width, EarlySplash.Height);
    return TRUE;

failure:
    if (EarlySplash.Bits)
    {
        HeapFree(GetProcessHeap(), 0, EarlySplash.Bits);
        EarlySplash.Bits = NULL;
    }
    return FALSE;
}

VOID
EarlySplashDestroy(VOID)
{
    EarlySplash.Active = FALSE;
    TRACE("EARLY_SPLASH: RELEASED\n");
}

HANDLE
EarlySplashCreateUserDesktopReadyEvent(
    _In_ PWLSESSION Session)
{
    HANDLE ReadyEvent;
    BOOL Impersonated = FALSE;

    if (!EarlySplash.Active)
    {
        TRACE("EARLY_SPLASH: READY_EVENT_SKIP reason=inactive\n");
        return NULL;
    }

    if (Session->UserToken)
    {
        Impersonated = ImpersonateLoggedOnUser(Session->UserToken);
        if (!Impersonated)
            ERR("EARLY_SPLASH: READY_EVENT_WARN stage=impersonate error=%lu\n", GetLastError());
    }

    ReadyEvent = CreateEventW(NULL, TRUE, FALSE, REACTOS_EARLY_SPLASH_READY_EVENT);
    if (Impersonated && !RevertToSelf())
        ERR("EARLY_SPLASH: READY_EVENT_WARN stage=revert error=%lu\n", GetLastError());
    if (!ReadyEvent)
    {
        ERR("EARLY_SPLASH: READY_EVENT_FAIL error=%lu\n", GetLastError());
        return NULL;
    }

    ResetEvent(ReadyEvent);
    TRACE("EARLY_SPLASH: READY_EVENT_OK name=%S\n", REACTOS_EARLY_SPLASH_READY_EVENT);
    return ReadyEvent;
}

DWORD
EarlySplashWaitForUserDesktopReady(
    _In_ HANDLE ReadyEvent)
{
    DWORD Result = WaitForSingleObject(ReadyEvent, REACTOS_EARLY_SPLASH_READY_TIMEOUT_MS);

    if (Result == WAIT_OBJECT_0)
        TRACE("EARLY_SPLASH: HANDOFF_READY result=signaled\n");
    else if (Result == WAIT_TIMEOUT)
        WARN("EARLY_SPLASH: HANDOFF_TIMEOUT timeout_ms=%lu\n", (DWORD)REACTOS_EARLY_SPLASH_READY_TIMEOUT_MS);
    else
        ERR("EARLY_SPLASH: HANDOFF_FAIL result=%lu error=%lu\n", Result, GetLastError());
    return Result;
}
