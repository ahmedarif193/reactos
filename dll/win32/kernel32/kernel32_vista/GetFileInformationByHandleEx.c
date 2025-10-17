
#include "k32_vista.h"

#include <ndk/rtlfuncs.h>
#include <ndk/iofuncs.h>

#define NDEBUG
#include <debug.h>

NTSYSAPI
NTSTATUS
NTAPI
RtlDosPathNameToNtPathName_U_WithStatus(PCWSTR DosName,
                                        PUNICODE_STRING NtName,
                                        PCWSTR* PartName,
                                        PVOID RelativeName);

#undef FIXME
#define FIXME DPRINT1

/* Taken from Wine kernel32/file.c */

/***********************************************************************
 *	GetFileInformationByHandleEx   (kernelbase.@)
 */
BOOL WINAPI DECLSPEC_HOTPATCH GetFileInformationByHandleEx(HANDLE handle, FILE_INFO_BY_HANDLE_CLASS class,
    LPVOID info, DWORD size)
{
    NTSTATUS status;
    IO_STATUS_BLOCK io;

    switch (class)
    {
    case FileRemoteProtocolInfo:
    case FileStorageInfo:
    case FileDispositionInfoEx:
    case FileRenameInfoEx:
    case FileCaseSensitiveInfo:
    case FileNormalizedNameInfo:
        FIXME("%p, %u, %p, %lu\n", handle, class, info, size);
        SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
        return FALSE;

    case FileStreamInfo:
        status = NtQueryInformationFile(handle, &io, info, size, FileStreamInformation);
        break;

    case FileCompressionInfo:
        status = NtQueryInformationFile(handle, &io, info, size, FileCompressionInformation);
        break;

    case FileAlignmentInfo:
        status = NtQueryInformationFile(handle, &io, info, size, FileAlignmentInformation);
        break;

    case FileAttributeTagInfo:
        status = NtQueryInformationFile(handle, &io, info, size, FileAttributeTagInformation);
        break;

    case FileBasicInfo:
        status = NtQueryInformationFile(handle, &io, info, size, FileBasicInformation);
        break;

    case FileStandardInfo:
        status = NtQueryInformationFile(handle, &io, info, size, FileStandardInformation);
        break;

    case FileNameInfo:
        status = NtQueryInformationFile(handle, &io, info, size, FileNameInformation);
        break;

    case FileIdInfo:
        status = NtQueryInformationFile(handle, &io, info, size, FileIdInformation);
        break;

    case FileIdBothDirectoryRestartInfo:
    case FileIdBothDirectoryInfo:
        status = NtQueryDirectoryFile(handle, NULL, NULL, NULL, &io, info, size,
            FileIdBothDirectoryInformation, FALSE, NULL,
            (class == FileIdBothDirectoryRestartInfo));
        break;

    case FileFullDirectoryInfo:
    case FileFullDirectoryRestartInfo:
        status = NtQueryDirectoryFile(handle, NULL, NULL, NULL, &io, info, size,
            FileFullDirectoryInformation, FALSE, NULL,
            (class == FileFullDirectoryRestartInfo));
        break;

    case FileIdExtdDirectoryInfo:
    case FileIdExtdDirectoryRestartInfo:
        status = NtQueryDirectoryFile(handle, NULL, NULL, NULL, &io, info, size,
            FileIdExtdDirectoryInformation, FALSE, NULL,
            (class == FileIdExtdDirectoryRestartInfo));
        break;

    case FileRenameInfo:
    case FileDispositionInfo:
    case FileAllocationInfo:
    case FileIoPriorityHintInfo:
    case FileEndOfFileInfo:
    default:
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

#ifdef __REACTOS__
    if (!NT_SUCCESS(status))
    {
        SetLastError(RtlNtStatusToDosError(status));
        return FALSE;
    }

    return TRUE;
#else
    return set_ntstatus(status);
#endif
}


/***********************************************************************
 *\tSetFileInformationByHandle   (kernelbase.@)
 */
BOOL WINAPI DECLSPEC_HOTPATCH SetFileInformationByHandle(HANDLE handle,
                                                         FILE_INFO_BY_HANDLE_CLASS class,
                                                         LPVOID info,
                                                         DWORD size)
{
    IO_STATUS_BLOCK io;
    NTSTATUS status;

    switch (class)
    {
        case FileEndOfFileInfo:
            status = NtSetInformationFile(handle, &io, info, size, FileEndOfFileInformation);
            break;

        case FileBasicInfo:
            status = NtSetInformationFile(handle, &io, info, size, FileBasicInformation);
            break;

        case FileDispositionInfo:
            status = NtSetInformationFile(handle, &io, info, size, FileDispositionInformation);
            break;

        case FileDispositionInfoEx:
            status = NtSetInformationFile(handle, &io, info, size, FileDispositionInformationEx);
            break;

        case FileIoPriorityHintInfo:
            status = NtSetInformationFile(handle, &io, info, size, FileIoPriorityHintInformation);
            break;

        case FileRenameInfo:
        {
            FILE_RENAME_INFORMATION *rename_in = info;
            FILE_RENAME_INFORMATION *rename_nt;
            UNICODE_STRING nt_name;
            ULONG rename_size;
            ULONG file_name_length = rename_in->FileNameLength;
            BOOLEAN copy_input = FALSE;
            static const WCHAR NtPrefix[] = L"\\??\\";
            static const WCHAR NativePrefix[] = L"\??\\";

            RtlInitUnicodeString(&nt_name, NULL);

            if (rename_in->RootDirectory)
            {
                copy_input = TRUE;
            }
            else if (file_name_length >= sizeof(NtPrefix) - sizeof(WCHAR) &&
                     RtlCompareMemory(rename_in->FileName,
                                      NtPrefix,
                                      sizeof(NtPrefix) - sizeof(WCHAR)) ==
                         sizeof(NtPrefix) - sizeof(WCHAR))
            {
                copy_input = TRUE;
            }
            else if (file_name_length >= sizeof(NativePrefix) - sizeof(WCHAR) &&
                     RtlCompareMemory(rename_in->FileName,
                                      NativePrefix,
                                      sizeof(NativePrefix) - sizeof(WCHAR)) ==
                         sizeof(NativePrefix) - sizeof(WCHAR))
            {
                copy_input = TRUE;
            }

            if (!copy_input)
            {
                status = RtlDosPathNameToNtPathName_U_WithStatus(rename_in->FileName,
                                                                &nt_name,
                                                                NULL,
                                                                NULL);
                if (!NT_SUCCESS(status))
                {
                    BaseSetLastNTError(status);
                    return FALSE;
                }

                file_name_length = nt_name.Length;
            }

            rename_size = sizeof(*rename_nt) + file_name_length;
            rename_nt = RtlAllocateHeap(RtlGetProcessHeap(), HEAP_ZERO_MEMORY, rename_size);
            if (!rename_nt)
            {
                SetLastError(ERROR_NOT_ENOUGH_MEMORY);
                return FALSE;
            }

            rename_nt->ReplaceIfExists = rename_in->ReplaceIfExists;
            rename_nt->RootDirectory = rename_in->RootDirectory;
            rename_nt->FileNameLength = file_name_length;

            if (file_name_length != 0)
            {
                if (copy_input)
                {
                    RtlCopyMemory(rename_nt->FileName,
                                  rename_in->FileName,
                                  file_name_length);
                }
                else
                {
                    RtlCopyMemory(rename_nt->FileName,
                                  nt_name.Buffer,
                                  file_name_length);
                }
            }

            status = NtSetInformationFile(handle,
                                          &io,
                                          rename_nt,
                                          rename_size,
                                          FileRenameInformation);

            if (!copy_input)
                RtlFreeUnicodeString(&nt_name);

            if (!NT_SUCCESS(status))
                RtlZeroMemory(rename_nt, rename_size);

            RtlFreeHeap(RtlGetProcessHeap(), 0, rename_nt);
            break;
        }

        case FileAllocationInfo:
        case FileNameInfo:
        case FileStandardInfo:
        case FileStreamInfo:
        case FileIdBothDirectoryInfo:
        case FileIdBothDirectoryRestartInfo:
        case FileFullDirectoryInfo:
        case FileFullDirectoryRestartInfo:
        case FileStorageInfo:
        case FileAlignmentInfo:
        case FileIdInfo:
        case FileIdExtdDirectoryInfo:
        case FileIdExtdDirectoryRestartInfo:
        case FileRemoteProtocolInfo:
        case FileAttributeTagInfo:
        case FileRenameInfoEx:
        case FileCaseSensitiveInfo:
        case FileNormalizedNameInfo:
            SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
            return FALSE;

        default:
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
    }

    if (!NT_SUCCESS(status))
    {
        BaseSetLastNTError(status);
        return FALSE;
    }

    return TRUE;
}
