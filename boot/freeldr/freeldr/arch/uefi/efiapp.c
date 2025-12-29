/*
 * UEFI EFI Application chainloader boot type for ARM64
 */

#include <freeldr.h>
#include <uefildr.h>
#include <machuefi.h>
#include <fs.h>
#include <ui.h>
#include <inifile.h>
#include <debug.h>

/* External GUID declaration */
extern EFI_GUID gEfiLoadedImageProtocolGuid;

/* Define RTL_NUMBER_OF if not already defined */
#ifndef RTL_NUMBER_OF
#define RTL_NUMBER_OF(A) (sizeof(A)/sizeof((A)[0]))
#endif

static PCSTR GetArgValue(ULONG Argc, PCHAR Argv[], PCSTR Key)
{
    size_t klen = strlen(Key);
    for (ULONG i = 0; i < Argc; ++i)
    {
        if (!Argv[i]) continue;
        if (_strnicmp(Argv[i], Key, klen) == 0 && Argv[i][klen] == '=')
            return Argv[i] + klen + 1;
    }
    return NULL;
}

static VOID AsciiToUcs2(PCSTR s, CHAR16* out, SIZE_T maxchars)
{
    SIZE_T i = 0;
    if (maxchars == 0) return;
    while (s && *s && i + 1 < maxchars)
    {
        out[i++] = (CHAR16)(unsigned char)(*s++);
    }
    out[i] = 0;
}

extern EFI_SYSTEM_TABLE* GlobalSystemTable;
extern EFI_HANDLE GlobalImageHandle;

ARC_STATUS
LoadAndBootEfiApp(
    IN ULONG Argc,
    IN PCHAR Argv[],
    IN PCHAR Envp[])
{
    (void)Envp;
    if (!GlobalSystemTable || !GlobalSystemTable->BootServices)
    {
        UiMessageBoxCritical("UEFI Boot Services not available");
        return EIO;
    }

    PCSTR appPath = GetArgValue(Argc, Argv, "EfiAppPath");
    PCSTR options = GetArgValue(Argc, Argv, "Options");
    if (!appPath || !*appPath)
    {
        UiMessageBoxCritical("Missing EfiAppPath in freeldr.ini");
        return EINVAL;
    }

    /* Open the target file using FreeLDR FS */
    ULONG fileId;
    ARC_STATUS st;
    FILEINFORMATION info;
    PCSTR bootPath = FrLdrGetBootPath();
    st = FsOpenFile((PCHAR)appPath, bootPath, OpenReadOnly, &fileId);
    if (st != ESUCCESS)
    {
        UiMessageBoxCritical("Cannot open EFI application");
        return st;
    }
    st = ArcGetFileInformation(fileId, &info);
    if (st != ESUCCESS || info.EndingAddress.HighPart != 0)
    {
        UiMessageBoxCritical("Cannot stat EFI application");
        return st != ESUCCESS ? st : EINVAL;
    }

    ULONG size = info.EndingAddress.LowPart;
    VOID* buffer = FrLdrHeapAlloc(size, TAG_STRING);
    if (!buffer)
    {
        UiMessageBoxCritical("Out of memory");
        return ENOMEM;
    }
    LARGE_INTEGER pos; pos.QuadPart = 0;
    if (ArcSeek(fileId, &pos, SeekAbsolute) != ESUCCESS)
    {
        UiMessageBoxCritical("Seek failed");
        return EIO;
    }
    ULONG read = 0;
    if (ArcRead(fileId, buffer, size, &read) != ESUCCESS || read != size)
    {
        UiMessageBoxCritical("Read failed");
        return EIO;
    }

    EFI_HANDLE image = NULL;
    EFI_STATUS es = GlobalSystemTable->BootServices->LoadImage(
        FALSE, GlobalImageHandle, NULL, buffer, size, &image);
    if (EFI_ERROR(es) || image == NULL)
    {
        UiMessageBoxCritical("UEFI LoadImage failed");
        return EIO;
    }

    /* Set LoadOptions if provided */
    if (options && *options)
    {
        EFI_LOADED_IMAGE* li = NULL;
        es = GlobalSystemTable->BootServices->HandleProtocol(
            image, &gEfiLoadedImageProtocolGuid, (VOID**)&li);
        if (!EFI_ERROR(es) && li)
        {
            static CHAR16 optbuf[512];
            AsciiToUcs2(options, optbuf, RTL_NUMBER_OF(optbuf));
            li->LoadOptions = optbuf;
            /* Calculate the length of the UCS-2 string */
            UINT32 len = 0;
            for (CHAR16 *p = optbuf; *p; p++) len++;
            li->LoadOptionsSize = (UINT32)(len * sizeof(CHAR16));
        }
    }

    es = GlobalSystemTable->BootServices->StartImage(image, NULL, NULL);
    if (EFI_ERROR(es))
    {
        UiMessageBoxCritical("UEFI StartImage failed");
        return EIO;
    }

    return ESUCCESS;
}

#if defined(HAS_OPTION_MENU_EDIT_CMDLINE) || defined(_M_ARM64) || defined(__aarch64__)
VOID EditCustomBootEfiApp(_Inout_ OperatingSystemItem* OperatingSystem)
{
    CHAR path[260] = {0};
    CHAR opts[260] = {0};
    ULONG_PTR sid = OperatingSystem->SectionId;
    /* Preload existing values */
    IniReadSettingByName(sid, "EfiAppPath", path, sizeof(path));
    IniReadSettingByName(sid, "Options", opts, sizeof(opts));
    if (!UiEditBox("EFI Application Path:", path, sizeof(path))) return;
    UiEditBox("Options (optional):", opts, sizeof(opts));
    IniModifySettingValue(sid, "EfiAppPath", path);
    IniModifySettingValue(sid, "Options", opts);
}
#endif
