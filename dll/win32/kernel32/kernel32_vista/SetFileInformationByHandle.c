#include "k32_vista.h"

#include <ndk/rtlfuncs.h>
#include <ndk/iofuncs.h>

#define NDEBUG
#include <debug.h>

#undef FIXME
#define FIXME DPRINT1

/* Taken from Wine kernelbase/file.c */

/***********************************************************************
 *	SetFileInformationByHandle   (kernelbase.@)
 */
BOOL WINAPI DECLSPEC_HOTPATCH SetFileInformationByHandle( HANDLE file, FILE_INFO_BY_HANDLE_CLASS class,
                                                          void *info, DWORD size )
{
    NTSTATUS status;
    IO_STATUS_BLOCK io;

    switch (class)
    {
    case FileNameInfo:
    case FileAllocationInfo:
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
        FIXME( "%p, %u, %p, %lu\n", file, class, info, size );
        SetLastError( ERROR_CALL_NOT_IMPLEMENTED );
        return FALSE;

    case FileEndOfFileInfo:
        status = NtSetInformationFile( file, &io, info, size, FileEndOfFileInformation );
        break;
    case FileBasicInfo:
        status = NtSetInformationFile( file, &io, info, size, FileBasicInformation );
        break;
    case FileDispositionInfo:
        status = NtSetInformationFile( file, &io, info, size, FileDispositionInformation );
        break;
    case FileDispositionInfoEx:
        status = NtSetInformationFile( file, &io, info, size, FileDispositionInformationEx );
        break;
    case FileIoPriorityHintInfo:
        status = NtSetInformationFile( file, &io, info, size, FileIoPriorityHintInformation );
        break;
    case FileRenameInfo:
    case FileRenameInfoEx:
        {
            FILE_RENAME_INFORMATION *rename_info;
            UNICODE_STRING nt_name;
            ULONG size;

            if ((status = RtlDosPathNameToNtPathName_U_WithStatus( ((FILE_RENAME_INFORMATION *)info)->FileName,
                                                                   &nt_name, NULL, NULL )))
                break;

            size = sizeof(*rename_info) + nt_name.Length;
            if ((rename_info = HeapAlloc( GetProcessHeap(), 0, size )))
            {
                memcpy( rename_info, info, sizeof(*rename_info) );
                memcpy( rename_info->FileName, nt_name.Buffer, nt_name.Length + sizeof(WCHAR) );
                rename_info->FileNameLength = nt_name.Length;
                status = NtSetInformationFile( file, &io, rename_info, size,
                        class == FileRenameInfo ? FileRenameInformation : FileRenameInformationEx );
                HeapFree( GetProcessHeap(), 0, rename_info );
            }
            RtlFreeUnicodeString( &nt_name );
            break;
        }
    case FileStandardInfo:
    case FileCompressionInfo:
    case FileAttributeTagInfo:
    case FileRemoteProtocolInfo:
    default:
        SetLastError( ERROR_INVALID_PARAMETER );
        return FALSE;
    }
#ifdef __REACTOS__
    if (!NT_SUCCESS(status))
    {
        SetLastError( RtlNtStatusToDosError( status ));
        return FALSE;
    }
    return TRUE;
#else
    return set_ntstatus( status );
#endif
}
