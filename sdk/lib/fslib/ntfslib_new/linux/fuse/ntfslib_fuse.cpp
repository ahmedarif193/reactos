/*
 * FUSE frontend and image inspection tool for ntfslib_new.
 *
 * Mounting intentionally stays single-threaded because the shared library
 * currently lazy-loads per-volume metadata without locking. Mounts are
 * read-only unless --writable is given; writable mounts publish mutations
 * through the shared core in a readable order but are not crash-atomic
 * until LFS transactions exist, so use disposable images only. The direct
 * image commands exercise the shared lookup/read/write paths without
 * requiring /dev/fuse or mount privileges. Mutation commands open the backing
 * image writable only for the duration of one explicit operation.
 */

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <time.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <sys/xattr.h>
#include <unistd.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <ntfslib_new.h>
#include <ntfs_linux.h>

typedef std::vector<std::string> NtfsDirectoryListing;


extern "C" unsigned long long
NtfsQueryTicks(void)
{
    /* Diagnostics only; the host build has no use for it. */
    return 0;
}

#if NTFSLIB_HAVE_FUSE
#define FUSE_USE_VERSION 31
#include <fuse.h>
#endif

struct NtfsFuseState
{
    PNtfsVolume Volume;
    uid_t Owner;
    gid_t Group;
    bool Writable;
    /* Values are shared with open directory handles so a cache
     * invalidation cannot free a listing a reader still holds.
     */
    std::unordered_map<std::string,
                       std::shared_ptr<NtfsDirectoryListing>>
        DirectoryCache;
    /* Open-record cache for mounted operation: repeated opens of the same
     * path reuse one validated record instead of re-walking the $I30
     * tree. Entries are owned by the cache (release skips them via the
     * low fh bit) and the map is dropped on any mutation or unmount.
     */
    std::unordered_map<std::string, PNtfsFileRecord> RecordCache;
};

#define NTFS_FUSE_CACHED_RECORD_BIT ((uint64_t)1)

static int
NtStatusToErrno(NTSTATUS Status)
{
    switch (Status)
    {
        case STATUS_SUCCESS:
            return 0;
        case STATUS_NOT_FOUND:
            return ENOENT;
        case STATUS_NO_MORE_FILES:
            return ENOENT;
        case STATUS_END_OF_FILE:
            return EIO;
        case STATUS_INVALID_PARAMETER:
        case STATUS_OBJECT_NAME_INVALID:
        case STATUS_NOT_A_REPARSE_POINT:
        case STATUS_IO_REPARSE_TAG_INVALID:
        case STATUS_IO_REPARSE_TAG_MISMATCH:
        case STATUS_IO_REPARSE_DATA_INVALID:
        case STATUS_REPARSE_ATTRIBUTE_CONFLICT:
        case STATUS_INVALID_EA_NAME:
            return EINVAL;
        case STATUS_NOT_A_DIRECTORY:
            return ENOTDIR;
        case STATUS_FILE_IS_A_DIRECTORY:
            return EISDIR;
        case STATUS_OBJECT_NAME_COLLISION:
            return EEXIST;
        case STATUS_DIRECTORY_NOT_EMPTY:
            return ENOTEMPTY;
        case STATUS_EA_TOO_LARGE:
            return E2BIG;
        case STATUS_NO_EAS_ON_FILE:
        case STATUS_OBJECT_NOT_EXTERNALLY_BACKED:
            return ENODATA;
        case STATUS_NAME_TOO_LONG:
            return ENAMETOOLONG;
        case STATUS_INSUFFICIENT_RESOURCES:
            return ENOMEM;
        case STATUS_ACCESS_DENIED:
        case STATUS_MEDIA_WRITE_PROTECTED:
            return EROFS;
        case STATUS_DISK_FULL:
            return ENOSPC;
        case STATUS_NOT_IMPLEMENTED:
            return ENOSYS;
        case STATUS_IO_REPARSE_TAG_NOT_HANDLED:
        case STATUS_EAS_NOT_SUPPORTED:
            return EOPNOTSUPP;
        case STATUS_FILE_TOO_LARGE:
            return EFBIG;
        case STATUS_UNRECOGNIZED_VOLUME:
            return ENODEV;
        case STATUS_FILE_CORRUPT_ERROR:
        default:
            return EIO;
    }
}

static bool
AppendUtf16(uint32_t CodePoint, std::vector<WCHAR>& Output)
{
    if (CodePoint > 0x10ffff ||
        (CodePoint >= 0xd800 && CodePoint <= 0xdfff))
    {
        return false;
    }

    if (CodePoint < 0x10000)
    {
        Output.push_back((WCHAR)CodePoint);
    }
    else
    {
        CodePoint -= 0x10000;
        Output.push_back((WCHAR)(0xd800 + (CodePoint >> 10)));
        Output.push_back((WCHAR)(0xdc00 + (CodePoint & 0x3ff)));
    }
    return true;
}

static bool
Utf8ToNtfsString(const char* Input,
                 bool TranslateSeparators,
                 bool AllowEmpty,
                 std::vector<WCHAR>& Output)
{
    const unsigned char* Cursor = (const unsigned char*)Input;

    Output.clear();
    if (!Input || (!AllowEmpty && *Input == '\0'))
        return false;

    while (*Cursor)
    {
        uint32_t CodePoint;
        unsigned Needed;

        if (*Cursor < 0x80)
        {
            CodePoint = *Cursor++;
            Needed = 0;
        }
        else if ((*Cursor & 0xe0) == 0xc0)
        {
            CodePoint = *Cursor++ & 0x1f;
            Needed = 1;
            if (CodePoint < 2)
                return false;
        }
        else if ((*Cursor & 0xf0) == 0xe0)
        {
            CodePoint = *Cursor++ & 0x0f;
            Needed = 2;
        }
        else if ((*Cursor & 0xf8) == 0xf0)
        {
            CodePoint = *Cursor++ & 0x07;
            Needed = 3;
        }
        else
        {
            return false;
        }

        for (unsigned Index = 0; Index < Needed; Index++)
        {
            if ((Cursor[Index] & 0xc0) != 0x80)
                return false;
            CodePoint = (CodePoint << 6) | (Cursor[Index] & 0x3f);
        }
        Cursor += Needed;

        if ((Needed == 2 && CodePoint < 0x800) ||
            (Needed == 3 && CodePoint < 0x10000))
        {
            return false;
        }

        if (TranslateSeparators && CodePoint == '/')
            CodePoint = '\\';
        if (!AppendUtf16(CodePoint, Output))
            return false;
    }

    Output.push_back(L'\0');
    return true;
}

static bool
Utf8ToNtfsPath(const char* Path, std::vector<WCHAR>& Output)
{
    return Utf8ToNtfsString(Path, true, false, Output);
}

static bool
AppendUtf8(uint32_t CodePoint, std::string& Output)
{
    if (CodePoint <= 0x7f)
    {
        Output.push_back((char)CodePoint);
    }
    else if (CodePoint <= 0x7ff)
    {
        Output.push_back((char)(0xc0 | (CodePoint >> 6)));
        Output.push_back((char)(0x80 | (CodePoint & 0x3f)));
    }
    else if (CodePoint <= 0xffff)
    {
        if (CodePoint >= 0xd800 && CodePoint <= 0xdfff)
            return false;
        Output.push_back((char)(0xe0 | (CodePoint >> 12)));
        Output.push_back((char)(0x80 | ((CodePoint >> 6) & 0x3f)));
        Output.push_back((char)(0x80 | (CodePoint & 0x3f)));
    }
    else if (CodePoint <= 0x10ffff)
    {
        Output.push_back((char)(0xf0 | (CodePoint >> 18)));
        Output.push_back((char)(0x80 | ((CodePoint >> 12) & 0x3f)));
        Output.push_back((char)(0x80 | ((CodePoint >> 6) & 0x3f)));
        Output.push_back((char)(0x80 | (CodePoint & 0x3f)));
    }
    else
    {
        return false;
    }
    return true;
}

static bool
Utf16ToUtf8(const WCHAR* Input, size_t Length, std::string& Output)
{
    Output.clear();
    for (size_t Index = 0; Index < Length; Index++)
    {
        uint32_t CodePoint = (UINT16)Input[Index];
        if (CodePoint >= 0xd800 && CodePoint <= 0xdbff)
        {
            uint32_t Low;
            if (++Index >= Length)
                return false;
            Low = (UINT16)Input[Index];
            if (Low < 0xdc00 || Low > 0xdfff)
                return false;
            CodePoint = 0x10000 +
                        ((CodePoint - 0xd800) << 10) +
                        (Low - 0xdc00);
        }
        else if (CodePoint >= 0xdc00 && CodePoint <= 0xdfff)
        {
            return false;
        }

        if (!AppendUtf8(CodePoint, Output))
            return false;
    }
    return true;
}

static uint16_t
ReadUtf16Le(const UCHAR* Input, size_t Index)
{
    return (uint16_t)Input[Index * sizeof(WCHAR)] |
           ((uint16_t)Input[Index * sizeof(WCHAR) + 1] << 8);
}

static bool
Utf16LeToUtf8(const UCHAR* Input, size_t Length, std::string& Output)
{
    Output.clear();
    for (size_t Index = 0; Index < Length; Index++)
    {
        uint32_t CodePoint = ReadUtf16Le(Input, Index);

        if (CodePoint >= 0xd800 && CodePoint <= 0xdbff)
        {
            uint32_t Low;

            if (++Index >= Length)
                return false;
            Low = ReadUtf16Le(Input, Index);
            if (Low < 0xdc00 || Low > 0xdfff)
                return false;
            CodePoint = 0x10000 +
                        ((CodePoint - 0xd800) << 10) +
                        (Low - 0xdc00);
        }
        else if (CodePoint >= 0xdc00 && CodePoint <= 0xdfff)
        {
            return false;
        }

        if (!AppendUtf8(CodePoint, Output))
            return false;
    }
    return true;
}

static NTSTATUS
Lookup(PNtfsVolume Volume,
       const char* Path,
       PNtfsFileRecord* File)
{
    std::vector<WCHAR> NtfsPath;

    *File = NULL;
    if (!Utf8ToNtfsPath(Path, NtfsPath))
        return STATUS_OBJECT_NAME_INVALID;
    if (NtfsPath.size() > MAXUSHORT / sizeof(WCHAR))
        return STATUS_NAME_TOO_LONG;

    return NtfsMasterFileTableGetFileRecordFromQuery(
        NtfsVolumeGetMft(Volume),
        NtfsPath.data(),
        File);
}

static int
ProbeReparseLookup(PNtfsVolume Volume,
                   const char* Path,
                   bool OpenFinal)
{
    std::vector<WCHAR> NtfsPath;
    PNtfsFileRecord File = NULL;
    ULONG RemainingNameLength = 0;
    NTSTATUS Status;

    if (!Utf8ToNtfsPath(Path, NtfsPath))
        return 1;
    if (NtfsPath.empty() ||
        NtfsPath.size() - 1 > MAXULONG)
    {
        return 1;
    }
    ULONG QueryLength =
        (ULONG)(NtfsPath.size() - 1);
    NtfsPath.back() = L'X';
    Status =
        NtfsMasterFileTableGetFileRecordFromQueryEx(
            NtfsVolumeGetMft(Volume),
            NtfsPath.data(),
            QueryLength,
            OpenFinal,
            &RemainingNameLength,
            &File);
    printf("0x%08" PRIx32 " %u %u\n",
           (uint32_t)Status,
           RemainingNameLength,
           File
               ? NtfsFileRecordGetHeader(File)->
                    MFTRecordNumber
               : 0);
    NtfsFileRecordDestroy(File);
    return Status == STATUS_REPARSE ||
           NT_SUCCESS(Status)
        ? 0
        : 1;
}

static NTSTATUS
LookupAttribute(PNtfsVolume Volume,
                const char* Path,
                PNtfsFileRecord* File,
                AttributeType* Type,
                PWSTR* Stream)
{
    std::vector<WCHAR> NtfsPath;
    UNICODE_STRING CountedPath;
    NTSTATUS Status;

    *File = NULL;
    *Type = TypeData;
    *Stream = NULL;
    if (!Utf8ToNtfsPath(Path, NtfsPath))
        return STATUS_OBJECT_NAME_INVALID;
    if (NtfsPath.size() > MAXUSHORT / sizeof(WCHAR))
        return STATUS_NAME_TOO_LONG;

    CountedPath.Buffer = NtfsPath.data();
    CountedPath.Length =
        (USHORT)((NtfsPath.size() - 1) * sizeof(WCHAR));
    CountedPath.MaximumLength =
        (USHORT)(NtfsPath.size() * sizeof(WCHAR));
    Status = NtfsVolumeGetADSPreference(
        Volume,
        &CountedPath,
        Type,
        Stream);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = NtfsMasterFileTableGetFileRecordFromQuery(
        NtfsVolumeGetMft(Volume),
        NtfsPath.data(),
        File);
    if (!NT_SUCCESS(Status))
    {
        delete[] *Stream;
        *Stream = NULL;
    }
    return Status;
}

static UINT64
AttributeSize(PAttribute Attribute)
{
    if (!Attribute)
        return 0;
    return Attribute->IsNonResident
        ? Attribute->NonResident.DataSize
        : Attribute->Resident.DataLength;
}

static NTSTATUS
GetReparseTarget(PNtfsFileRecord File, std::string& Target)
{
    NtfsReparseNameView View;
    std::vector<UCHAR> RawData;
    const UCHAR* Name;
    ULONG NameLength;
    ULONG RawLength = 0;
    NTSTATUS Status;

    Target.clear();
    Status = NtfsFileRecordReadReparsePoint(File, NULL, &RawLength);
    if (Status != STATUS_BUFFER_TOO_SMALL)
        return Status;
    if (RawLength == 0 ||
        RawLength > NTFS_MAXIMUM_REPARSE_DATA_BUFFER_SIZE)
    {
        return STATUS_IO_REPARSE_DATA_INVALID;
    }

    RawData.resize(RawLength);
    Status = NtfsFileRecordReadReparsePoint(
        File,
        RawData.data(),
        &RawLength);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = NtfsParseNameSurrogateReparsePoint(
        RawData.data(),
        RawLength,
        &View);
    if (!NT_SUCCESS(Status))
        return Status;

    /*
     * The substitute name drives relative links. For an absolute symlink or
     * junction, prefer its display-safe print name when one is present.
     */
    Name = View.SubstituteName;
    NameLength = View.SubstituteNameLength;
    if (!(View.Flags & NTFS_SYMLINK_FLAG_RELATIVE) &&
        View.PrintNameLength != 0)
    {
        Name = View.PrintName;
        NameLength = View.PrintNameLength;
    }
    else if (NameLength >= 4 &&
             ReadUtf16Le(Name, 0) == L'\\' &&
             ReadUtf16Le(Name, 1) == L'?' &&
             ReadUtf16Le(Name, 2) == L'?' &&
             ReadUtf16Le(Name, 3) == L'\\')
    {
        Name += 4 * sizeof(WCHAR);
        NameLength -= 4;
    }

    if (!Utf16LeToUtf8(Name, NameLength, Target))
        return STATUS_IO_REPARSE_DATA_INVALID;
    for (char& Character : Target)
    {
        if (Character == '\\')
            Character = '/';
    }
    return STATUS_SUCCESS;
}

#if NTFSLIB_HAVE_FUSE
static struct timespec
NtfsTimeToTimespec(UINT64 NtfsTime)
{
    const UINT64 EpochDifference = UINT64_C(116444736000000000);
    struct timespec Result = {};

    if (NtfsTime <= EpochDifference)
        return Result;

    NtfsTime -= EpochDifference;
    Result.tv_sec = (time_t)(NtfsTime / UINT64_C(10000000));
    Result.tv_nsec = (long)((NtfsTime % UINT64_C(10000000)) * 100);
    return Result;
}

static NTSTATUS
FillStatFromRecord(PNtfsFileRecord File,
                   const NtfsFuseState* State,
                   struct stat* Stat)
{
    PFileRecordHeader Header = NtfsFileRecordGetHeader(File);
    PAttribute Data = NtfsFileRecordGetAttribute(File, TypeData, NULL);
    PStandardInformationEx Standard = NULL;
    PUCHAR StandardData = NULL;
    NTSTATUS StandardStatus;

    memset(Stat, 0, sizeof(*Stat));
    Stat->st_ino = Header->MFTRecordNumber;
    Stat->st_uid = State->Owner;
    Stat->st_gid = State->Group;
    Stat->st_blksize =
        NtfsVolumeGetBytesPerSector(State->Volume) *
        NtfsVolumeGetSectorsPerCluster(State->Volume);
    Stat->st_nlink = Header->HardLinkCount ? Header->HardLinkCount : 1;

    if (Header->Flags & FR_IS_DIRECTORY)
    {
        Stat->st_mode = S_IFDIR | 0555;
    }
    else
    {
        Stat->st_mode = S_IFREG | 0444;
        Stat->st_size = (off_t)AttributeSize(Data);
        Stat->st_blocks = (blkcnt_t)((Stat->st_size + 511) / 512);
    }

    StandardStatus = NtfsFileRecordGetAttributeData(
        File,
        TypeStandardInformation,
        NULL,
        &StandardData);
    if (NT_SUCCESS(StandardStatus))
    {
        Standard = (PStandardInformationEx)StandardData;
        Stat->st_ctim = NtfsTimeToTimespec(Standard->ChangeTime);
        Stat->st_mtim = NtfsTimeToTimespec(Standard->LastWriteTime);
        Stat->st_atim = NtfsTimeToTimespec(Standard->LastAccessTime);

        if (Standard->FilePermissions & FILE_PERM_REPARSE_PT)
        {
            std::string Target;
            NTSTATUS Status = GetReparseTarget(File, Target);

            if (NT_SUCCESS(Status))
            {
                Stat->st_mode = S_IFLNK | 0777;
                Stat->st_size = (off_t)Target.size();
                Stat->st_blocks = 0;
            }
            else if (Status != STATUS_IO_REPARSE_TAG_NOT_HANDLED)
            {
                return Status;
            }
        }
    }
    return StandardStatus;
}

#endif

static NTSTATUS
OpenImageWithMode(const char* Image,
                  bool ShowMetadata,
                  bool Writable,
                  NtfsFuseState* State)
{
    ULONG BytesPerSector;
    NTSTATUS Status;

    static_assert(sizeof(WCHAR) == 2, "NTFS requires 16-bit WCHAR");
    static_assert(sizeof(BootSector) == 512, "Boot sector layout changed");
    static_assert(sizeof(FileRecordHeader) == 48,
                  "File record layout changed");
    static_assert(FIELD_OFFSET(FileNameEx, Name) == 0x42,
                  "File name attribute layout changed");
    static_assert(FIELD_OFFSET(IndexEntry, IndexStream) == 0x10,
                  "Index entry layout changed");

    State->Volume = NULL;
    State->Owner = getuid();
    State->Group = getgid();

    Status = Writable
        ? NtfsDiskInitializeLinuxWritable(Image, &BytesPerSector)
        : NtfsDiskInitializeLinux(Image, &BytesPerSector);
    if (!NT_SUCCESS(Status))
        return Status;

    NtfsSetShowMetadataFiles(ShowMetadata ? TRUE : FALSE);
    NtfsSetReadOnlyMode(Writable ? FALSE : TRUE);
    Status = NtfsProbePartitionAndOpenVolume(BytesPerSector, &State->Volume);
    if (!NT_SUCCESS(Status))
        NtfsDiskCloseLinux();
    return Status;
}

static NTSTATUS
OpenImage(const char* Image,
          bool ShowMetadata,
          NtfsFuseState* State)
{
    return OpenImageWithMode(Image, ShowMetadata, false, State);
}

static NTSTATUS
OpenImageWritable(const char* Image,
                  NtfsFuseState* State)
{
    return OpenImageWithMode(Image, false, true, State);
}

static void
DropRecordCache(NtfsFuseState* State)
{
    for (auto& Entry : State->RecordCache)
        NtfsFileRecordDestroy(Entry.second);
    State->RecordCache.clear();
}

static void
CloseImage(NtfsFuseState* State)
{
    State->DirectoryCache.clear();
    DropRecordCache(State);
    NtfsVolumeDestroy(State->Volume);
    State->Volume = NULL;
    NtfsDiskCloseLinux();
}

static int
ListDirectory(NtfsFuseState* State,
              const char* Path,
              bool IncludeAllocation)
{
    PNtfsFileRecord File = NULL;
    PNtfsDirectory Directory = NULL;
    NtfsDirectoryEntry Entry;
    BOOLEAN Restart = TRUE;
    NTSTATUS Status;
    int Result = 1;

    Status = Lookup(State->Volume, Path, &File);
    if (!NT_SUCCESS(Status))
        goto Done;
    if (!(NtfsFileRecordGetHeader(File)->Flags & FR_IS_DIRECTORY))
    {
        errno = ENOTDIR;
        goto Done;
    }

    Directory = NtfsDirectoryCreate(State->Volume);
    if (!Directory)
    {
        errno = ENOMEM;
        goto Done;
    }

    Status = NtfsDirectoryLoadDirectory(Directory, File);
    if (!NT_SUCCESS(Status))
        goto Done;

    for (;;)
    {
        std::string Name;
        Status = NtfsDirectoryReadNext(Directory, Restart, &Entry);
        Restart = FALSE;
        if (Status == STATUS_NO_MORE_FILES)
        {
            Result = 0;
            break;
        }
        if (!NT_SUCCESS(Status))
            break;
        if (!Utf16ToUtf8(Entry.Name, Entry.NameLength, Name))
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            break;
        }

        if (IncludeAllocation)
        {
            printf("%c %10" PRIu64 " %10" PRIu64
                   " %s %" PRIu16 " 0x%08" PRIx32 "\n",
                   Entry.FileAttributes & FN_DIRECTORY ? 'd' : '-',
                   (uint64_t)Entry.EndOfFile,
                   (uint64_t)Entry.AllocationSize,
                   Name.c_str(),
                   Entry.EaSize,
                   Entry.ReparseTag);
        }
        else
        {
            printf("%c %10" PRIu64 " %s\n",
                   Entry.FileAttributes & FN_DIRECTORY ? 'd' : '-',
                   (uint64_t)Entry.EndOfFile,
                   Name.c_str());
        }
    }

Done:
    if (Result != 0)
    {
        if (NT_SUCCESS(Status))
            fprintf(stderr, "%s: %s\n", Path, strerror(errno));
        else
            fprintf(stderr,
                    "%s: ntfslib status 0x%08" PRIx32 " (%s)\n",
                    Path,
                    (uint32_t)Status,
                    strerror(NtStatusToErrno(Status)));
    }
    NtfsDirectoryDestroy(Directory);
    NtfsFileRecordDestroy(File);
    return Result;
}

static int
CatFile(NtfsFuseState* State,
        const char* Path,
        UINT64 StartOffset,
        UINT64 MaximumLength)
{
    PNtfsFileRecord File = NULL;
    PAttribute Attribute;
    AttributeType RequestedType = TypeData;
    PWSTR RequestedStream = NULL;
    NTSTATUS Status;
    UINT64 Offset;
    UINT64 EndOffset;
    UINT64 Size;
    UCHAR Buffer[64 * 1024];
    int Result = 1;

    Status = LookupAttribute(State->Volume,
                             Path,
                             &File,
                             &RequestedType,
                             &RequestedStream);
    if (!NT_SUCCESS(Status))
        goto Done;
    if ((NtfsFileRecordGetHeader(File)->Flags & FR_IS_DIRECTORY) &&
        RequestedType == TypeData &&
        !RequestedStream)
    {
        errno = EISDIR;
        Status = STATUS_SUCCESS;
        goto Done;
    }

    Attribute = NtfsFileRecordGetAttribute(File,
                                           RequestedType,
                                           RequestedStream);
    if (!Attribute)
    {
        Status = STATUS_NOT_FOUND;
        goto Done;
    }
    if (Attribute->Flags & ATTR_ENCRYPTED)
    {
        errno = EOPNOTSUPP;
        Status = STATUS_SUCCESS;
        goto Done;
    }

    Size = AttributeSize(Attribute);
    if (StartOffset > Size)
    {
        Status = STATUS_END_OF_FILE;
        goto Done;
    }
    Offset = StartOffset;
    EndOffset = Size;
    if (MaximumLength < Size - StartOffset)
        EndOffset = StartOffset + MaximumLength;

    while (Offset < EndOffset)
    {
        ULONG Requested = (ULONG)((EndOffset - Offset) < sizeof(Buffer)
                                  ? EndOffset - Offset
                                  : sizeof(Buffer));
        ULONG Remaining = Requested;
        ULONG Completed;

        Status = NtfsFileRecordCopyData(File,
                                        RequestedType,
                                        RequestedStream,
                                        Buffer,
                                        &Remaining,
                                        Offset);
        if (!NT_SUCCESS(Status))
            goto Done;
        Completed = Requested - Remaining;
        if (Completed == 0)
        {
            Status = STATUS_END_OF_FILE;
            goto Done;
        }

        ULONG Written = 0;
        while (Written < Completed)
        {
            ssize_t Count = write(STDOUT_FILENO,
                                  Buffer + Written,
                                  Completed - Written);
            if (Count > 0)
            {
                Written += (ULONG)Count;
                continue;
            }
            if (Count < 0 && errno == EINTR)
                continue;
            Status = STATUS_SUCCESS;
            goto Done;
        }
        Offset += Completed;
    }

    Result = 0;

Done:
    if (Result != 0)
    {
        if (NT_SUCCESS(Status))
            fprintf(stderr, "%s: %s\n", Path, strerror(errno));
        else
            fprintf(stderr,
                    "%s: ntfslib status 0x%08" PRIx32 " (%s)\n",
                    Path,
                    (uint32_t)Status,
                    strerror(NtStatusToErrno(Status)));
    }
    NtfsFileRecordDestroy(File);
    delete[] RequestedStream;
    return Result;
}

static int
ReadLink(NtfsFuseState* State, const char* Path)
{
    PNtfsFileRecord File = NULL;
    std::string Target;
    NTSTATUS Status;

    Status = Lookup(State->Volume, Path, &File);
    if (NT_SUCCESS(Status))
        Status = GetReparseTarget(File, Target);

    if (NT_SUCCESS(Status))
    {
        printf("%s\n", Target.c_str());
    }
    else
    {
        fprintf(stderr,
                "%s: ntfslib status 0x%08" PRIx32 " (%s)\n",
                Path,
                (uint32_t)Status,
                strerror(NtStatusToErrno(Status)));
    }

    NtfsFileRecordDestroy(File);
    return NT_SUCCESS(Status) ? 0 : 1;
}

static int
DumpReparsePoint(NtfsFuseState* State, const char* Path)
{
    PNtfsFileRecord File = NULL;
    std::vector<UCHAR> RawData;
    ULONG RawLength = 0;
    ULONG Written = 0;
    NTSTATUS Status;

    Status = Lookup(State->Volume, Path, &File);
    if (NT_SUCCESS(Status))
    {
        Status = NtfsFileRecordReadReparsePoint(
            File,
            NULL,
            &RawLength);
    }
    if (Status == STATUS_BUFFER_TOO_SMALL)
    {
        RawData.resize(RawLength);
        Status = NtfsFileRecordReadReparsePoint(
            File,
            RawData.data(),
            &RawLength);
    }

    while (NT_SUCCESS(Status) && Written < RawLength)
    {
        ssize_t Count = write(STDOUT_FILENO,
                              RawData.data() + Written,
                              RawLength - Written);
        if (Count > 0)
        {
            Written += (ULONG)Count;
            continue;
        }
        if (Count < 0 && errno == EINTR)
            continue;
        Status = STATUS_ACCESS_DENIED;
    }

    if (!NT_SUCCESS(Status))
    {
        fprintf(stderr,
                "%s: ntfslib status 0x%08" PRIx32
                " (%s)\n",
                Path,
                (uint32_t)Status,
                strerror(NtStatusToErrno(Status)));
    }

    NtfsFileRecordDestroy(File);
    return NT_SUCCESS(Status) ? 0 : 1;
}

static int
ListExtendedAttributes(NtfsFuseState* State, const char* Path)
{
    PNtfsFileRecord File = NULL;
    EAInformationEx Information = {};
    NtfsExtendedAttributeView View;
    std::vector<UCHAR> RawData;
    ULONG Offset = 0;
    ULONG RawLength = 0;
    NTSTATUS Status;

    Status = Lookup(State->Volume, Path, &File);
    if (NT_SUCCESS(Status))
    {
        Status = NtfsFileRecordReadExtendedAttributes(
            File,
            NULL,
            &RawLength,
            &Information);
    }
    if (Status == STATUS_BUFFER_TOO_SMALL)
    {
        RawData.resize(RawLength);
        Status = NtfsFileRecordReadExtendedAttributes(
            File,
            RawData.data(),
            &RawLength,
            &Information);
    }
    else if (Status == STATUS_NO_EAS_ON_FILE)
    {
        Status = STATUS_SUCCESS;
    }

    while (NT_SUCCESS(Status))
    {
        Status = NtfsGetNextExtendedAttribute(
            RawData.data(),
            RawLength,
            &Offset,
            &View);
        if (Status == STATUS_NO_MORE_EAS)
        {
            Status = STATUS_SUCCESS;
            break;
        }
        if (!NT_SUCCESS(Status))
            break;

        printf("%c ",
               View.Flags & NTFS_EA_FLAG_NEED_EA ? '!' : '-');
        fwrite(View.Name, 1, View.NameLength, stdout);
        printf(" %" PRIu16 " ", View.ValueLength);
        for (ULONG Index = 0; Index < View.ValueLength; Index++)
            printf("%02x", View.Value[Index]);
        putchar('\n');
    }

    if (!NT_SUCCESS(Status))
    {
        fprintf(stderr,
                "%s: ntfslib status 0x%08" PRIx32 " (%s)\n",
                Path,
                (uint32_t)Status,
                strerror(NtStatusToErrno(Status)));
    }

    NtfsFileRecordDestroy(File);
    return NT_SUCCESS(Status) ? 0 : 1;
}

static int
DumpSecurityDescriptor(NtfsFuseState* State, const char* Path)
{
    PNtfsFileRecord File = NULL;
    std::vector<UCHAR> Descriptor;
    ULONG DescriptorLength = 0;
    ULONG Written = 0;
    NTSTATUS Status;

    Status = Lookup(State->Volume, Path, &File);
    if (NT_SUCCESS(Status))
    {
        Status = NtfsFileRecordReadSecurityDescriptor(
            File,
            NULL,
            &DescriptorLength);
    }
    if (Status == STATUS_BUFFER_TOO_SMALL)
    {
        Descriptor.resize(DescriptorLength);
        Status = NtfsFileRecordReadSecurityDescriptor(
            File,
            Descriptor.data(),
            &DescriptorLength);
    }

    while (NT_SUCCESS(Status) && Written < DescriptorLength)
    {
        ssize_t Count = write(
            STDOUT_FILENO,
            Descriptor.data() + Written,
            DescriptorLength - Written);
        if (Count > 0)
        {
            Written += (ULONG)Count;
            continue;
        }
        if (Count < 0 && errno == EINTR)
            continue;
        Status = STATUS_ACCESS_DENIED;
    }

    if (!NT_SUCCESS(Status))
    {
        fprintf(stderr,
                "%s: ntfslib status 0x%08" PRIx32 " (%s)\n",
                Path,
                (uint32_t)Status,
                strerror(NtStatusToErrno(Status)));
    }

    NtfsFileRecordDestroy(File);
    return NT_SUCCESS(Status) ? 0 : 1;
}

static int
ProbeImage(NtfsFuseState* State)
{
    WCHAR Label[32] = {};
    USHORT LabelBytes = sizeof(Label);
    LARGE_INTEGER FreeClusters = {};
    std::string LabelUtf8;
    NTSTATUS LabelStatus;
    NTSTATUS FreeStatus;

    LabelStatus = NtfsVolumeGetVolumeLabel(State->Volume,
                                           Label,
                                           &LabelBytes);
    if (!NT_SUCCESS(LabelStatus) ||
        !Utf16ToUtf8(Label, LabelBytes / sizeof(WCHAR), LabelUtf8))
    {
        LabelUtf8 = "<unavailable>";
    }
    FreeStatus = NtfsVolumeGetFreeClusters(State->Volume, &FreeClusters);

    printf("NTFS %" PRIu16 ".%" PRIu16 "\n",
           (uint16_t)NtfsVolumeGetMajorVersion(State->Volume),
           (uint16_t)NtfsVolumeGetMinorVersion(State->Volume));
    printf("label: %s\n", LabelUtf8.c_str());
    printf("serial: %016" PRIx64 "\n",
           (uint64_t)NtfsVolumeGetSerialNumber(State->Volume));
    printf("sector size: %" PRIu32 "\n",
           (uint32_t)NtfsVolumeGetBytesPerSector(State->Volume));
    printf("cluster size: %" PRIu32 "\n",
           (uint32_t)(NtfsVolumeGetBytesPerSector(State->Volume) *
                      NtfsVolumeGetSectorsPerCluster(State->Volume)));
    printf("clusters: %" PRIu64 "\n",
           (uint64_t)NtfsVolumeGetClustersInVolume(State->Volume));
    if (NT_SUCCESS(FreeStatus))
        printf("free clusters: %" PRIi64 "\n",
               (int64_t)FreeClusters.QuadPart);
    else
        printf("free clusters: unavailable (0x%08" PRIx32 ")\n",
               (uint32_t)FreeStatus);
    return 0;
}

static int
SetImageLabel(NtfsFuseState* State, const char* Label)
{
    std::vector<WCHAR> Utf16Label;
    ULONG LabelLength;
    NTSTATUS Status;

    if (!Utf8ToNtfsString(Label,
                          false,
                          true,
                          Utf16Label))
    {
        fprintf(stderr, "invalid UTF-8 volume label\n");
        return 1;
    }

    LabelLength =
        (ULONG)((Utf16Label.size() - 1) * sizeof(WCHAR));
    if (LabelLength > MAXIMUM_VOLUME_LABEL_LENGTH)
    {
        fprintf(stderr, "volume label exceeds 32 UTF-16 code units\n");
        return 1;
    }

    Status = NtfsVolumeSetVolumeLabel(
        State->Volume,
        Utf16Label.data(),
        LabelLength);
    if (!NT_SUCCESS(Status))
    {
        fprintf(stderr,
                "volume label: ntfslib status 0x%08" PRIx32
                " (%s)\n",
                (uint32_t)Status,
                strerror(NtStatusToErrno(Status)));
        return 1;
    }

    return 0;
}

static int
PrintBasicInformation(NtfsFuseState* State,
                      const char* Path)
{
    PNtfsFileRecord File = NULL;
    NtfsFileBasicInformation Information = {};
    NTSTATUS Status;

    Status = Lookup(State->Volume, Path, &File);
    if (NT_SUCCESS(Status))
    {
        Status = NtfsFileRecordGetBasicInformation(
            File,
            &Information);
    }

    if (NT_SUCCESS(Status))
    {
        printf("%" PRIu64 " %" PRIu64 " %" PRIu64
               " %" PRIu64 " 0x%08" PRIx32 "\n",
               Information.CreationTime,
               Information.LastAccessTime,
               Information.LastWriteTime,
               Information.ChangeTime,
               Information.FileAttributes);
    }
    else
    {
        fprintf(stderr,
                "%s: ntfslib status 0x%08" PRIx32
                " (%s)\n",
                Path,
                (uint32_t)Status,
                strerror(NtStatusToErrno(Status)));
    }

    NtfsFileRecordDestroy(File);
    return NT_SUCCESS(Status) ? 0 : 1;
}

static int
SetBasicInformation(NtfsFuseState* State,
                    const char* Path,
                    const NtfsFileBasicInformation* Information)
{
    PNtfsFileRecord File = NULL;
    NTSTATUS Status;

    Status = Lookup(State->Volume, Path, &File);
    if (NT_SUCCESS(Status))
    {
        Status = NtfsFileRecordSetBasicInformation(
            File,
            Information);
    }
    if (!NT_SUCCESS(Status))
    {
        fprintf(stderr,
                "%s: ntfslib status 0x%08" PRIx32
                " (%s)\n",
                Path,
                (uint32_t)Status,
                strerror(NtStatusToErrno(Status)));
    }

    NtfsFileRecordDestroy(File);
    return NT_SUCCESS(Status) ? 0 : 1;
}

static int
CreateNode(NtfsFuseState* State,
           const char* Path,
           bool IsDirectory,
           ULONG FileAttributes)
{
    std::vector<WCHAR> NtfsPath;
    PNtfsFileRecord File = NULL;
    NTSTATUS Status;

    if (!Utf8ToNtfsPath(Path, NtfsPath) ||
        NtfsPath.size() < 2 ||
        NtfsPath.size() - 1 > MAXULONG)
    {
        Status = STATUS_OBJECT_NAME_INVALID;
    }
    else
    {
        Status = NtfsMasterFileTableCreateFile(
            NtfsVolumeGetMft(State->Volume),
            NtfsPath.data(),
            (ULONG)(NtfsPath.size() - 1),
            IsDirectory,
            FileAttributes,
            &File);
    }
    if (!NT_SUCCESS(Status))
    {
        fprintf(stderr,
                "%s: ntfslib status 0x%08" PRIx32
                " (%s)\n",
                Path,
                (uint32_t)Status,
                strerror(NtStatusToErrno(Status)));
        return 1;
    }

    printf("%u\n",
           NtfsFileRecordGetHeader(File)->
               MFTRecordNumber);
    NtfsFileRecordDestroy(File);
    return 0;
}

static int
RemoveNode(NtfsFuseState* State,
           const char* Path,
           bool IsDirectory)
{
    std::vector<WCHAR> NtfsPath;
    NTSTATUS Status;

    if (!Utf8ToNtfsPath(Path, NtfsPath) ||
        NtfsPath.size() < 2 ||
        NtfsPath.size() - 1 > MAXULONG)
    {
        Status = STATUS_OBJECT_NAME_INVALID;
    }
    else
    {
        Status = NtfsMasterFileTableDeleteFile(
            NtfsVolumeGetMft(State->Volume),
            NtfsPath.data(),
            (ULONG)(NtfsPath.size() - 1),
            IsDirectory);
    }
    if (!NT_SUCCESS(Status))
    {
        fprintf(stderr,
                "%s: ntfslib status 0x%08" PRIx32
                " (%s)\n",
                Path,
                (uint32_t)Status,
                strerror(NtStatusToErrno(Status)));
        return 1;
    }
    return 0;
}

static int
RenameNode(NtfsFuseState* State,
           const char* OldPath,
           const char* NewPath,
           bool HardLink)
{
    std::vector<WCHAR> OldNtfsPath;
    std::vector<WCHAR> NewNtfsPath;
    NTSTATUS Status;

    if (!Utf8ToNtfsPath(OldPath, OldNtfsPath) ||
        !Utf8ToNtfsPath(NewPath, NewNtfsPath) ||
        OldNtfsPath.size() < 2 ||
        NewNtfsPath.size() < 2 ||
        OldNtfsPath.size() - 1 > MAXULONG ||
        NewNtfsPath.size() - 1 > MAXULONG)
    {
        Status = STATUS_OBJECT_NAME_INVALID;
    }
    else if (HardLink)
    {
        Status =
            NtfsMasterFileTableCreateHardLink(
                NtfsVolumeGetMft(State->Volume),
                OldNtfsPath.data(),
                (ULONG)(OldNtfsPath.size() - 1),
                NewNtfsPath.data(),
                (ULONG)(NewNtfsPath.size() - 1));
    }
    else
    {
        Status = NtfsMasterFileTableRenameFile(
            NtfsVolumeGetMft(State->Volume),
            OldNtfsPath.data(),
            (ULONG)(OldNtfsPath.size() - 1),
            NewNtfsPath.data(),
            (ULONG)(NewNtfsPath.size() - 1));
    }
    if (!NT_SUCCESS(Status))
    {
        fprintf(stderr,
                "%s -> %s: ntfslib status "
                "0x%08" PRIx32 " (%s)\n",
                OldPath,
                NewPath,
                (uint32_t)Status,
                strerror(NtStatusToErrno(Status)));
        return 1;
    }
    return 0;
}

static int
SetSecurityFromHost(NtfsFuseState* State,
                    const char* Path,
                    const char* SourcePath)
{
    PNtfsFileRecord File = NULL;
    std::vector<UCHAR> Value;
    struct stat SourceStat;
    ULONG Completed = 0;
    NTSTATUS Status;
    int Source = -1;
    int Result = 1;

    Source = open(SourcePath, O_RDONLY | O_CLOEXEC);
    if (Source < 0 || fstat(Source, &SourceStat) != 0 ||
        SourceStat.st_size <= 0 ||
        (uint64_t)SourceStat.st_size > MAXULONG)
    {
        fprintf(stderr, "%s: %s\n", SourcePath, strerror(errno));
        goto Done;
    }
    Value.resize((size_t)SourceStat.st_size);
    while (Completed < Value.size())
    {
        ssize_t Count = read(Source,
                             Value.data() + Completed,
                             Value.size() - Completed);
        if (Count > 0)
        {
            Completed += (ULONG)Count;
            continue;
        }
        if (Count < 0 && errno == EINTR)
            continue;
        fprintf(stderr, "%s: incomplete input\n", SourcePath);
        goto Done;
    }

    Status = Lookup(State->Volume, Path, &File);
    if (NT_SUCCESS(Status))
    {
        Status = NtfsFileRecordSetSecurityDescriptor(
            File,
            Value.data(),
            (ULONG)Value.size());
    }
    if (!NT_SUCCESS(Status))
    {
        fprintf(stderr,
                "%s: ntfslib status 0x%08" PRIx32 " (%s)\n",
                Path,
                (uint32_t)Status,
                strerror(NtStatusToErrno(Status)));
        goto Done;
    }
    Result = 0;

Done:
    NtfsFileRecordDestroy(File);
    if (Source >= 0)
        close(Source);
    return Result;
}

static int
UpdateExtendedAttribute(NtfsFuseState* State,
                        const char* Path,
                        UINT8 Flags,
                        const char* Name,
                        const char* SourcePath,
                        bool Remove)
{
    PNtfsFileRecord File = NULL;
    NtfsExtendedAttributeUpdate Update = {};
    std::vector<UCHAR> Value;
    struct stat SourceStat;
    size_t NameLength;
    ULONG Completed = 0;
    NTSTATUS Status = STATUS_SUCCESS;
    int Source = -1;
    int Result = 1;

    NameLength = Name ? strlen(Name) : 0;
    if (NameLength == 0 || NameLength >= 0xff)
    {
        fprintf(stderr, "EA name must contain 1-254 ASCII bytes\n");
        goto Done;
    }

    if (!Remove)
    {
        Source = open(SourcePath, O_RDONLY | O_CLOEXEC);
        if (Source < 0 || fstat(Source, &SourceStat) != 0)
        {
            fprintf(stderr,
                    "%s: %s\n",
                    SourcePath,
                    strerror(errno));
            goto Done;
        }
        if (SourceStat.st_size < 0 ||
            (uint64_t)SourceStat.st_size > MAXUSHORT)
        {
            fprintf(stderr,
                    "%s: EA value exceeds 65535 bytes\n",
                    SourcePath);
            goto Done;
        }

        Value.resize((size_t)SourceStat.st_size);
        while (Completed < Value.size())
        {
            ssize_t Count = read(Source,
                                 Value.data() + Completed,
                                 Value.size() - Completed);
            if (Count > 0)
            {
                Completed += (ULONG)Count;
                continue;
            }
            if (Count < 0 && errno == EINTR)
                continue;
            fprintf(stderr, "%s: incomplete input\n", SourcePath);
            goto Done;
        }
    }

    Status = Lookup(State->Volume, Path, &File);
    if (!NT_SUCCESS(Status))
        goto NtfsError;

    Update.Operation = Remove
        ? NTFS_EA_UPDATE_REMOVE
        : NTFS_EA_UPDATE_SET;
    Update.Flags = Flags;
    Update.NameLength = (UINT8)NameLength;
    Update.ValueLength = (UINT16)Value.size();
    Update.Name = (const UCHAR*)Name;
    Update.Value =
        Value.empty() ? NULL : Value.data();
    Status = NtfsFileRecordUpdateExtendedAttributes(
        File,
        &Update,
        1);
    if (!NT_SUCCESS(Status))
        goto NtfsError;

    Result = 0;
    goto Done;

NtfsError:
    fprintf(stderr,
            "%s: ntfslib status 0x%08" PRIx32
            " (%s)\n",
            Path,
            (uint32_t)Status,
            strerror(NtStatusToErrno(Status)));

Done:
    NtfsFileRecordDestroy(File);
    if (Source >= 0)
        close(Source);
    return Result;
}

static int
UpdateReparsePointFromHost(NtfsFuseState* State,
                           const char* Path,
                           const char* SourcePath,
                           bool Delete)
{
    PNtfsFileRecord File = NULL;
    std::vector<UCHAR> Buffer;
    struct stat SourceStat;
    ULONG Completed = 0;
    NTSTATUS Status = STATUS_SUCCESS;
    int Source = -1;
    int Result = 1;

    Source = open(SourcePath, O_RDONLY | O_CLOEXEC);
    if (Source < 0 || fstat(Source, &SourceStat) != 0)
    {
        fprintf(stderr,
                "%s: %s\n",
                SourcePath,
                strerror(errno));
        goto Done;
    }
    if (SourceStat.st_size <= 0 ||
        (uint64_t)SourceStat.st_size >
            NTFS_MAXIMUM_REPARSE_DATA_BUFFER_SIZE)
    {
        fprintf(stderr,
                "%s: reparse buffer must contain 1-%u bytes\n",
                SourcePath,
                NTFS_MAXIMUM_REPARSE_DATA_BUFFER_SIZE);
        goto Done;
    }

    Buffer.resize((size_t)SourceStat.st_size);
    while (Completed < Buffer.size())
    {
        ssize_t Count = read(Source,
                             Buffer.data() + Completed,
                             Buffer.size() - Completed);
        if (Count > 0)
        {
            Completed += (ULONG)Count;
            continue;
        }
        if (Count < 0 && errno == EINTR)
            continue;
        fprintf(stderr, "%s: incomplete input\n", SourcePath);
        goto Done;
    }

    Status = Lookup(State->Volume, Path, &File);
    if (!NT_SUCCESS(Status))
        goto NtfsError;
    Status = Delete
        ? NtfsFileRecordDeleteReparsePoint(
            File,
            Buffer.data(),
            (ULONG)Buffer.size())
        : NtfsFileRecordSetReparsePoint(
            File,
            Buffer.data(),
            (ULONG)Buffer.size());
    if (!NT_SUCCESS(Status))
        goto NtfsError;

    Result = 0;
    goto Done;

NtfsError:
    fprintf(stderr,
            "%s: ntfslib status 0x%08" PRIx32
            " (%s)\n",
            Path,
            (uint32_t)Status,
            strerror(NtStatusToErrno(Status)));

Done:
    NtfsFileRecordDestroy(File);
    if (Source >= 0)
        close(Source);
    return Result;
}

static bool
ParseUnsigned(const char* Text,
              ULONGLONG Maximum,
              ULONGLONG* Value)
{
    char* End;
    ULONGLONG Parsed;

    if (!Text || !Value ||
        *Text == '\0' || *Text == '-')
    {
        return false;
    }
    errno = 0;
    Parsed = strtoull(Text, &End, 0);
    if (errno != 0 || *End != '\0' ||
        Parsed > Maximum)
    {
        return false;
    }
    *Value = Parsed;
    return true;
}

static bool
ParseOptionalUnsigned(const char* Text,
                      ULONGLONG Maximum,
                      ULONGLONG* Value,
                      bool* Present)
{
    char* End;
    ULONGLONG Parsed;

    if (!Text || !Value || !Present)
        return false;
    if (strcmp(Text, "-") == 0)
    {
        *Present = false;
        *Value = 0;
        return true;
    }
    if (*Text == '\0' || *Text == '-')
        return false;

    errno = 0;
    Parsed = strtoull(Text, &End, 0);
    if (errno != 0 || *End != '\0' ||
        Parsed > Maximum)
    {
        return false;
    }

    *Present = true;
    *Value = Parsed;
    return true;
}

static int
WriteFileFromHost(NtfsFuseState* State,
                  const char* Path,
                  ULONGLONG Offset,
                  const char* SourcePath)
{
    PNtfsFileRecord File = NULL;
    AttributeType RequestedType = TypeData;
    PWSTR RequestedStream = NULL;
    std::vector<UCHAR> Buffer;
    struct stat SourceStat;
    LARGE_INTEGER ByteOffset;
    ULONG Length;
    ULONG Completed = 0;
    NTSTATUS Status;
    int Source = -1;
    int Result = 1;

    Source = open(SourcePath, O_RDONLY | O_CLOEXEC);
    if (Source < 0 || fstat(Source, &SourceStat) != 0)
    {
        fprintf(stderr, "%s: %s\n", SourcePath, strerror(errno));
        goto Done;
    }
    if (SourceStat.st_size < 0 ||
        (uint64_t)SourceStat.st_size > MAXULONG)
    {
        fprintf(stderr, "%s: input is too large\n", SourcePath);
        goto Done;
    }

    Length = (ULONG)SourceStat.st_size;
    Buffer.resize(Length);
    while (Completed < Length)
    {
        ssize_t Count = read(Source,
                             Buffer.data() + Completed,
                             Length - Completed);
        if (Count > 0)
        {
            Completed += (ULONG)Count;
            continue;
        }
        if (Count < 0 && errno == EINTR)
            continue;
        fprintf(stderr, "%s: incomplete input\n", SourcePath);
        goto Done;
    }

    Status = LookupAttribute(State->Volume,
                             Path,
                             &File,
                             &RequestedType,
                             &RequestedStream);
    if (!NT_SUCCESS(Status))
        goto NtfsError;
    if (RequestedType != TypeData ||
        ((NtfsFileRecordGetHeader(File)->Flags & FR_IS_DIRECTORY) &&
         !RequestedStream))
    {
        Status = STATUS_INVALID_PARAMETER;
        goto NtfsError;
    }

    ByteOffset.QuadPart = (LONGLONG)Offset;
    Status = NtfsFileRecordWriteFileData(
        File,
        RequestedType,
        RequestedStream,
        Buffer.data(),
        &Length,
        &ByteOffset);
    if (!NT_SUCCESS(Status))
        goto NtfsError;
    if (Length != (ULONG)SourceStat.st_size)
    {
        Status = STATUS_END_OF_FILE;
        goto NtfsError;
    }

    Result = 0;
    goto Done;

NtfsError:
    fprintf(stderr,
            "%s: ntfslib status 0x%08" PRIx32 " (%s)\n",
            Path,
            (uint32_t)Status,
            strerror(NtStatusToErrno(Status)));

Done:
    NtfsFileRecordDestroy(File);
    delete[] RequestedStream;
    if (Source >= 0)
        close(Source);
    return Result;
}

static std::string
NormalizeImagePath(const std::string& Input)
{
    std::string Path = Input;

    for (char& Character : Path)
    {
        if (Character == '\\')
            Character = '/';
    }
    while (!Path.empty() && Path.front() == '/')
        Path.erase(Path.begin());
    while (!Path.empty() && Path.back() == '/')
        Path.pop_back();
    return Path;
}

static NTSTATUS
EnsureImageDirectory(NtfsFuseState* State,
                     const std::string& Directory)
{
    std::string Path = NormalizeImagePath(Directory);
    size_t End = 0;

    while (End < Path.size())
    {
        PNtfsFileRecord File = NULL;
        std::vector<WCHAR> NtfsPath;
        std::string Component;
        NTSTATUS Status;

        End = Path.find('/', End);
        if (End == std::string::npos)
            End = Path.size();
        Component = Path.substr(0, End);

        Status = Lookup(State->Volume, Component.c_str(), &File);
        if (NT_SUCCESS(Status))
        {
            if (!(NtfsFileRecordGetHeader(File)->Flags &
                  FR_IS_DIRECTORY))
            {
                NtfsFileRecordDestroy(File);
                return STATUS_NOT_A_DIRECTORY;
            }
            NtfsFileRecordDestroy(File);
        }
        else
        {
            if (Status != STATUS_NOT_FOUND)
                return Status;
            if (!Utf8ToNtfsPath(Component.c_str(), NtfsPath) ||
                NtfsPath.size() < 2 ||
                NtfsPath.size() - 1 > MAXULONG)
            {
                return STATUS_OBJECT_NAME_INVALID;
            }
            Status = NtfsMasterFileTableCreateFile(
                NtfsVolumeGetMft(State->Volume),
                NtfsPath.data(),
                (ULONG)(NtfsPath.size() - 1),
                TRUE,
                0,
                &File);
            NtfsFileRecordDestroy(File);
            if (!NT_SUCCESS(Status))
                return Status;
        }

        if (End < Path.size())
            End++;
    }
    return STATUS_SUCCESS;
}

static NTSTATUS
PrepareImageFile(NtfsFuseState* State,
                 const std::string& Path)
{
    PNtfsFileRecord File = NULL;
    std::vector<WCHAR> NtfsPath;
    NTSTATUS Status;

    Status = Lookup(State->Volume, Path.c_str(), &File);
    if (NT_SUCCESS(Status))
    {
        if (NtfsFileRecordGetHeader(File)->Flags &
            FR_IS_DIRECTORY)
        {
            NtfsFileRecordDestroy(File);
            return STATUS_FILE_IS_A_DIRECTORY;
        }
        Status = NtfsFileRecordSetFileDataSize(
            File,
            TypeData,
            NULL,
            0);
        NtfsFileRecordDestroy(File);
        return Status;
    }
    if (Status != STATUS_NOT_FOUND)
        return Status;
    if (!Utf8ToNtfsPath(Path.c_str(), NtfsPath) ||
        NtfsPath.size() < 2 ||
        NtfsPath.size() - 1 > MAXULONG)
    {
        return STATUS_OBJECT_NAME_INVALID;
    }

    Status = NtfsMasterFileTableCreateFile(
        NtfsVolumeGetMft(State->Volume),
        NtfsPath.data(),
        (ULONG)(NtfsPath.size() - 1),
        FALSE,
        0,
        &File);
    NtfsFileRecordDestroy(File);
    return Status;
}

static NTSTATUS
CopyHostFileToImage(NtfsFuseState* State,
                    const std::string& ImagePath,
                    const std::string& SourcePath)
{
    static const size_t CopyBufferSize = 1024 * 1024;
    PNtfsFileRecord File = NULL;
    std::vector<UCHAR> Buffer(CopyBufferSize);
    LARGE_INTEGER Offset = {};
    FILE* Source;
    NTSTATUS Status;

    Source = fopen(SourcePath.c_str(), "rb");
    if (!Source)
        return STATUS_NOT_FOUND;

    Status = Lookup(State->Volume, ImagePath.c_str(), &File);
    if (!NT_SUCCESS(Status))
        goto Done;

    for (;;)
    {
        size_t ReadLength = fread(Buffer.data(),
                                  1,
                                  Buffer.size(),
                                  Source);
        if (ReadLength != 0)
        {
            ULONG WriteLength = (ULONG)ReadLength;

            Status = NtfsFileRecordWriteFileData(
                File,
                TypeData,
                NULL,
                Buffer.data(),
                &WriteLength,
                &Offset);
            if (!NT_SUCCESS(Status))
                goto Done;
            if (WriteLength != ReadLength)
            {
                Status = STATUS_END_OF_FILE;
                goto Done;
            }
            Offset.QuadPart += WriteLength;
        }
        if (ReadLength != Buffer.size())
        {
            Status = ferror(Source)
                ? STATUS_INVALID_DEVICE_STATE
                : STATUS_SUCCESS;
            break;
        }
    }

Done:
    NtfsFileRecordDestroy(File);
    fclose(Source);
    return Status;
}

static int
AddFilesFromList(NtfsFuseState* State,
                 const char* ListPath)
{
    FILE* List = fopen(ListPath, "rb");
    char* Line = NULL;
    size_t Capacity = 0;
    unsigned long LineNumber = 0;
    int Result = 1;

    if (!List)
    {
        fprintf(stderr, "%s: %s\n", ListPath, strerror(errno));
        return 1;
    }

    for (;;)
    {
        struct stat SourceStat;
        std::string Destination;
        std::string Parent;
        std::string Source;
        char* Separator;
        ssize_t Length;
        NTSTATUS Status;

        errno = 0;
        Length = getline(&Line, &Capacity, List);
        if (Length < 0)
        {
            if (feof(List))
            {
                Result = 0;
                break;
            }
            fprintf(stderr, "%s: %s\n", ListPath, strerror(errno));
            break;
        }
        LineNumber++;
        while (Length > 0 &&
               (Line[Length - 1] == '\n' ||
                Line[Length - 1] == '\r'))
        {
            Line[--Length] = '\0';
        }
        if (Length == 0)
            continue;

        Separator = strchr(Line, '=');
        if (!Separator)
        {
            fprintf(stderr,
                    "%s:%lu: missing '='\n",
                    ListPath,
                    LineNumber);
            break;
        }
        *Separator = '\0';
        Destination = NormalizeImagePath(Line);
        Source = Separator + 1;
        if (Destination.empty() || Source.empty())
        {
            fprintf(stderr,
                    "%s:%lu: empty path\n",
                    ListPath,
                    LineNumber);
            break;
        }
        if (stat(Source.c_str(), &SourceStat) != 0)
        {
            fprintf(stderr,
                    "%s:%lu: %s: %s\n",
                    ListPath,
                    LineNumber,
                    Source.c_str(),
                    strerror(errno));
            break;
        }

        if (S_ISDIR(SourceStat.st_mode))
        {
            Status = EnsureImageDirectory(State, Destination);
        }
        else if (S_ISREG(SourceStat.st_mode))
        {
            size_t Slash = Destination.rfind('/');

            if (Slash != std::string::npos)
                Parent = Destination.substr(0, Slash);
            Status = EnsureImageDirectory(State, Parent);
            if (NT_SUCCESS(Status))
                Status = PrepareImageFile(State, Destination);
            if (NT_SUCCESS(Status))
            {
                Status = CopyHostFileToImage(
                    State,
                    Destination,
                    Source);
            }
        }
        else
        {
            Status = STATUS_INVALID_PARAMETER;
        }

        if (!NT_SUCCESS(Status))
        {
            fprintf(stderr,
                    "%s:%lu: %s: ntfslib status 0x%08" PRIx32
                    " (%s)\n",
                    ListPath,
                    LineNumber,
                    Destination.c_str(),
                    (uint32_t)Status,
                    strerror(NtStatusToErrno(Status)));
            break;
        }
    }

    free(Line);
    fclose(List);
    return Result;
}

static int
SetFileSize(NtfsFuseState* State,
            const char* Path,
            ULONGLONG NewSize,
            bool AllocationOnly)
{
    PNtfsFileRecord File = NULL;
    PAttribute DataAttribute;
    AttributeType RequestedType = TypeData;
    PWSTR RequestedStream = NULL;
    NTSTATUS Status;
    int Result = 1;

    Status = LookupAttribute(State->Volume,
                             Path,
                             &File,
                             &RequestedType,
                             &RequestedStream);
    if (!NT_SUCCESS(Status))
        goto NtfsError;
    if (RequestedType != TypeData ||
        ((NtfsFileRecordGetHeader(File)->Flags &
          FR_IS_DIRECTORY) &&
         !RequestedStream))
    {
        Status = STATUS_INVALID_PARAMETER;
        goto NtfsError;
    }

    DataAttribute = NtfsFileRecordGetAttribute(
        File,
        RequestedType,
        RequestedStream);
    if (!DataAttribute)
    {
        Status = STATUS_NOT_FOUND;
        goto NtfsError;
    }

    Status = AllocationOnly
        ? NtfsFileRecordSetFileAllocationSize(
            File,
            RequestedType,
            RequestedStream,
            NewSize)
        : NtfsFileRecordSetFileDataSize(
            File,
            RequestedType,
            RequestedStream,
            NewSize);
    if (!NT_SUCCESS(Status))
        goto NtfsError;

    Result = 0;
    goto Done;

NtfsError:
    fprintf(stderr,
            "%s: ntfslib status 0x%08" PRIx32
            " (%s)\n",
            Path,
            (uint32_t)Status,
            strerror(NtStatusToErrno(Status)));

Done:
    NtfsFileRecordDestroy(File);
    delete[] RequestedStream;
    return Result;
}

static int
SetSparseState(NtfsFuseState* State,
               const char* Path,
               bool SetSparse)
{
    PNtfsFileRecord File = NULL;
    AttributeType RequestedType = TypeData;
    PWSTR RequestedStream = NULL;
    NTSTATUS Status;

    Status = LookupAttribute(State->Volume,
                             Path,
                             &File,
                             &RequestedType,
                             &RequestedStream);
    if (NT_SUCCESS(Status))
    {
        Status = NtfsFileRecordSetSparse(
            File,
            RequestedType,
            RequestedStream,
            SetSparse ? TRUE : FALSE);
    }
    if (!NT_SUCCESS(Status))
    {
        fprintf(stderr,
                "%s: ntfslib status 0x%08" PRIx32
                " (%s)\n",
                Path,
                (uint32_t)Status,
                strerror(NtStatusToErrno(Status)));
    }

    NtfsFileRecordDestroy(File);
    delete[] RequestedStream;
    return NT_SUCCESS(Status) ? 0 : 1;
}

static int
SetZeroDataRange(NtfsFuseState* State,
                 const char* Path,
                 ULONGLONG FileOffset,
                 ULONGLONG BeyondFinalZero)
{
    PNtfsFileRecord File = NULL;
    AttributeType RequestedType = TypeData;
    PWSTR RequestedStream = NULL;
    NTSTATUS Status;

    Status = LookupAttribute(State->Volume,
                             Path,
                             &File,
                             &RequestedType,
                             &RequestedStream);
    if (NT_SUCCESS(Status))
    {
        Status = NtfsFileRecordSetZeroData(
            File,
            RequestedType,
            RequestedStream,
            FileOffset,
            BeyondFinalZero);
    }
    if (!NT_SUCCESS(Status))
    {
        fprintf(stderr,
                "%s: ntfslib status 0x%08" PRIx32
                " (%s)\n",
                Path,
                (uint32_t)Status,
                strerror(NtStatusToErrno(Status)));
    }

    NtfsFileRecordDestroy(File);
    delete[] RequestedStream;
    return NT_SUCCESS(Status) ? 0 : 1;
}

static int
PrintAllocatedRanges(NtfsFuseState* State,
                     const char* Path,
                     ULONGLONG FileOffset,
                     ULONGLONG Length)
{
    PNtfsFileRecord File = NULL;
    AttributeType RequestedType = TypeData;
    PWSTR RequestedStream = NULL;
    std::vector<NtfsAllocatedRange> Ranges(16);
    ULONG Count;
    NTSTATUS Status;

    Status = LookupAttribute(State->Volume,
                             Path,
                             &File,
                             &RequestedType,
                             &RequestedStream);
    if (!NT_SUCCESS(Status))
        goto Done;

    for (;;)
    {
        Count = (ULONG)Ranges.size();
        Status = NtfsFileRecordQueryAllocatedRanges(
            File,
            RequestedType,
            RequestedStream,
            FileOffset,
            Length,
            Ranges.data(),
            &Count);
        if (Status != STATUS_BUFFER_TOO_SMALL &&
            Status != STATUS_BUFFER_OVERFLOW)
        {
            break;
        }
        if (Ranges.size() >
            MAXULONG / (2 * sizeof(NtfsAllocatedRange)))
        {
            Status = STATUS_FILE_TOO_LARGE;
            break;
        }
        Ranges.resize(Ranges.size() * 2);
    }

    if (NT_SUCCESS(Status))
    {
        for (ULONG Index = 0; Index < Count; Index++)
        {
            printf("%" PRIu64 " %" PRIu64 "\n",
                   (uint64_t)Ranges[Index].FileOffset,
                   (uint64_t)Ranges[Index].Length);
        }
    }

Done:
    if (!NT_SUCCESS(Status))
    {
        fprintf(stderr,
                "%s: ntfslib status 0x%08" PRIx32
                " (%s)\n",
                Path,
                (uint32_t)Status,
                strerror(NtStatusToErrno(Status)));
    }
    NtfsFileRecordDestroy(File);
    delete[] RequestedStream;
    return NT_SUCCESS(Status) ? 0 : 1;
}

static int
PrintRetrievalPointers(NtfsFuseState* State,
                       const char* Path,
                       ULONGLONG StartingVcn)
{
    PNtfsFileRecord File = NULL;
    AttributeType RequestedType = TypeData;
    PWSTR RequestedStream = NULL;
    /* Start with one entry so focused fragmented-stream tests also exercise
     * the STATUS_BUFFER_OVERFLOW continuation contract.
     */
    std::vector<NtfsRetrievalExtent> Extents(1);
    ULONGLONG ReturnedStartingVcn = 0;
    ULONG Count;
    NTSTATUS Status;

    Status = LookupAttribute(State->Volume,
                             Path,
                             &File,
                             &RequestedType,
                             &RequestedStream);
    if (!NT_SUCCESS(Status))
        goto Done;

    for (;;)
    {
        Count = (ULONG)Extents.size();
        Status = NtfsFileRecordQueryRetrievalPointers(
            File,
            RequestedType,
            RequestedStream,
            StartingVcn,
            &ReturnedStartingVcn,
            Extents.data(),
            &Count);
        if (Status != STATUS_BUFFER_TOO_SMALL &&
            Status != STATUS_BUFFER_OVERFLOW)
        {
            break;
        }
        if (Extents.size() >
            MAXULONG /
                (2 * sizeof(NtfsRetrievalExtent)))
        {
            Status = STATUS_FILE_TOO_LARGE;
            break;
        }
        Extents.resize(Extents.size() * 2);
    }

    if (NT_SUCCESS(Status))
    {
        printf("start %" PRIu64 "\n",
               (uint64_t)ReturnedStartingVcn);
        for (ULONG Index = 0; Index < Count; Index++)
        {
            printf("%" PRIu64 " %" PRId64 "\n",
                   (uint64_t)Extents[Index].NextVcn,
                   (int64_t)Extents[Index].Lcn);
        }
    }

Done:
    if (!NT_SUCCESS(Status))
    {
        fprintf(stderr,
                "%s: ntfslib status 0x%08" PRIx32
                " (%s)\n",
                Path,
                (uint32_t)Status,
                strerror(NtStatusToErrno(Status)));
    }
    NtfsFileRecordDestroy(File);
    delete[] RequestedStream;
    return NT_SUCCESS(Status) ? 0 : 1;
}

static int
PrintVolumeBitmap(NtfsFuseState* State,
                  ULONGLONG StartingLcn,
                  ULONG ByteCount)
{
    std::vector<UCHAR> Bitmap(ByteCount);
    ULONGLONG ReturnedStartingLcn = 0;
    ULONGLONG BitmapSize = 0;
    ULONG Length = ByteCount;
    NTSTATUS Status;

    Status = NtfsVolumeReadBitmap(
        State->Volume,
        StartingLcn,
        &ReturnedStartingLcn,
        &BitmapSize,
        Bitmap.empty() ? NULL : Bitmap.data(),
        &Length);
    if (NT_SUCCESS(Status) ||
        Status == STATUS_BUFFER_OVERFLOW)
    {
        printf("%" PRIu64 " %" PRIu64 " ",
               (uint64_t)ReturnedStartingLcn,
               (uint64_t)BitmapSize);
        for (ULONG Index = 0; Index < Length; Index++)
            printf("%02x", Bitmap[Index]);
        putchar('\n');
        return 0;
    }

    fprintf(stderr,
            "volume bitmap: ntfslib status 0x%08"
            PRIx32 " (%s)\n",
            (uint32_t)Status,
            strerror(NtStatusToErrno(Status)));
    return 1;
}

static int
PrintVolumeData(NtfsFuseState* State)
{
    NtfsVolumeInformation Information = {};
    NTSTATUS Status;

    Status = NtfsVolumeQueryInformation(
        State->Volume,
        &Information);
    if (!NT_SUCCESS(Status))
    {
        fprintf(stderr,
                "volume data: ntfslib status 0x%08"
                PRIx32 " (%s)\n",
                (uint32_t)Status,
                strerror(NtStatusToErrno(Status)));
        return 1;
    }

    printf("serial %" PRIu64 "\n",
           (uint64_t)Information.VolumeSerialNumber);
    printf("sectors %" PRIu64 "\n",
           (uint64_t)Information.NumberSectors);
    printf("clusters %" PRIu64 "\n",
           (uint64_t)Information.TotalClusters);
    printf("free-clusters %" PRIu64 "\n",
           (uint64_t)Information.FreeClusters);
    printf("reserved-clusters %" PRIu64 "\n",
           (uint64_t)Information.TotalReserved);
    printf("bytes-per-sector %" PRIu32 "\n",
           (uint32_t)Information.BytesPerSector);
    printf("bytes-per-cluster %" PRIu32 "\n",
           (uint32_t)Information.BytesPerCluster);
    printf("bytes-per-file-record %" PRIu32 "\n",
           (uint32_t)Information.BytesPerFileRecordSegment);
    printf("clusters-per-file-record %" PRIu32 "\n",
           (uint32_t)Information.ClustersPerFileRecordSegment);
    printf("mft-valid-bytes %" PRIu64 "\n",
           (uint64_t)Information.MftValidDataLength);
    printf("mft-lcn %" PRIu64 "\n",
           (uint64_t)Information.MftStartLcn);
    printf("mftmirr-lcn %" PRIu64 "\n",
           (uint64_t)Information.Mft2StartLcn);
    printf("mft-zone-start %" PRIu64 "\n",
           (uint64_t)Information.MftZoneStart);
    printf("mft-zone-end %" PRIu64 "\n",
           (uint64_t)Information.MftZoneEnd);
    printf("version %" PRIu16 ".%" PRIu16 "\n",
           (uint16_t)Information.MajorVersion,
           (uint16_t)Information.MinorVersion);
    return 0;
}

static int
PrintDataStreams(NtfsFuseState* State,
                 const char* Path)
{
    PNtfsFileRecord File = NULL;
    AttributeType RequestedType = TypeData;
    PWSTR RequestedStream = NULL;
    std::vector<NtfsDataStreamInformation> Streams(1);
    ULONG Count;
    NTSTATUS Status;

    Status = LookupAttribute(State->Volume,
                             Path,
                             &File,
                             &RequestedType,
                             &RequestedStream);
    if (!NT_SUCCESS(Status))
        goto Done;

    for (;;)
    {
        Count = (ULONG)Streams.size();
        Status = NtfsFileRecordQueryDataStreams(
            File,
            Streams.data(),
            &Count);
        if (Status != STATUS_BUFFER_TOO_SMALL &&
            Status != STATUS_BUFFER_OVERFLOW)
        {
            break;
        }
        if (Streams.size() >
            MAXULONG /
                (2 * sizeof(NtfsDataStreamInformation)))
        {
            Status = STATUS_FILE_TOO_LARGE;
            break;
        }
        Streams.resize(Streams.size() * 2);
    }

    if (NT_SUCCESS(Status))
    {
        for (ULONG Index = 0; Index < Count; Index++)
        {
            std::string Name;

            if (!Utf16ToUtf8(
                    Streams[Index].Name,
                    Streams[Index].NameLength,
                    Name))
            {
                Status = STATUS_FILE_CORRUPT_ERROR;
                break;
            }
            printf(":%s:$DATA %" PRIu64 " %" PRIu64 "\n",
                   Name.c_str(),
                   (uint64_t)Streams[Index].DataSize,
                   (uint64_t)Streams[Index].AllocationSize);
        }
    }

Done:
    if (!NT_SUCCESS(Status))
    {
        fprintf(stderr,
                "%s: ntfslib status 0x%08" PRIx32
                " (%s)\n",
                Path,
                (uint32_t)Status,
                strerror(NtStatusToErrno(Status)));
    }
    NtfsFileRecordDestroy(File);
    delete[] RequestedStream;
    return NT_SUCCESS(Status) ? 0 : 1;
}

static int
DumpMftRecord(NtfsFuseState* State,
              ULONGLONG RequestedFileReference)
{
    PNtfsMasterFileTable Mft =
        NtfsVolumeGetMft(State->Volume);
    std::vector<UCHAR> Record;
    ULONGLONG ReturnedFileReference = 0;
    ULONG Length = 0;
    NTSTATUS Status;

    Status = NtfsMasterFileTableReadFileRecord(
        Mft,
        RequestedFileReference,
        &ReturnedFileReference,
        NULL,
        &Length);
    if (Status != STATUS_BUFFER_TOO_SMALL ||
        Length == 0)
    {
        goto Failed;
    }

    Record.resize(Length);
    Status = NtfsMasterFileTableReadFileRecord(
        Mft,
        RequestedFileReference,
        &ReturnedFileReference,
        Record.data(),
        &Length);
    if (!NT_SUCCESS(Status))
        goto Failed;
    if (fwrite(Record.data(), 1, Length, stdout) !=
        Length)
    {
        fprintf(stderr,
                "MFT record: %s\n",
                strerror(errno));
        return 1;
    }
    fprintf(stderr,
            "file-record %" PRIu64 " %" PRIu32 "\n",
            (uint64_t)ReturnedFileReference,
            (uint32_t)Length);
    return 0;

Failed:
    fprintf(stderr,
            "MFT record: ntfslib status 0x%08"
            PRIx32 " (%s)\n",
            (uint32_t)Status,
            strerror(NtStatusToErrno(Status)));
    return 1;
}

static int
DeleteExternalBacking(NtfsFuseState* State,
                      const char* Path)
{
    PNtfsFileRecord File = NULL;
    AttributeType RequestedType = TypeData;
    PWSTR RequestedStream = NULL;
    NTSTATUS Status;
    int Result = 1;

    Status = LookupAttribute(State->Volume,
                             Path,
                             &File,
                             &RequestedType,
                             &RequestedStream);
    if (!NT_SUCCESS(Status))
        goto NtfsError;
    if (RequestedType != TypeData ||
        RequestedStream ||
        (NtfsFileRecordGetHeader(File)->Flags &
         FR_IS_DIRECTORY))
    {
        Status = STATUS_INVALID_PARAMETER;
        goto NtfsError;
    }

    Status = NtfsFileRecordDeleteExternalBacking(
        File);
    if (!NT_SUCCESS(Status))
        goto NtfsError;

    Result = 0;
    goto Done;

NtfsError:
    fprintf(stderr,
            "%s: ntfslib status 0x%08" PRIx32
            " (%s)\n",
            Path,
            (uint32_t)Status,
            strerror(NtStatusToErrno(Status)));

Done:
    NtfsFileRecordDestroy(File);
    delete[] RequestedStream;
    return Result;
}

#if NTFSLIB_HAVE_FUSE
static NtfsFuseState*
FuseState(void)
{
    return (NtfsFuseState*)fuse_get_context()->private_data;
}

static void*
NtfsFuseInit(struct fuse_conn_info* Connection,
             struct fuse_config* Configuration)
{
    NtfsFuseState* State = FuseState();

    /* Readdir stays plain (entry stats are served by getattr through the
     * kernel attribute cache), so directory listings only carry names.
     */
    Connection->want &= ~FUSE_CAP_READDIRPLUS;
    Connection->want &= ~FUSE_CAP_READDIRPLUS_AUTO;
    UNREFERENCED_PARAMETER(Configuration);
    return State;
}

static int
NtfsFuseGetattr(const char* Path,
                struct stat* Stat,
                struct fuse_file_info* FileInfo)
{
    NtfsFuseState* State = FuseState();
    PNtfsFileRecord File = NULL;
    NTSTATUS Status;

    /* FileInfo->fh holds a file record for open files but a directory
     * listing for opendir handles, so always resolve by path.
     */
    UNREFERENCED_PARAMETER(FileInfo);

    Status = Lookup(State->Volume, Path, &File);
    if (!NT_SUCCESS(Status))
        return -NtStatusToErrno(Status);

    Status = FillStatFromRecord(File, State, Stat);
    NtfsFileRecordDestroy(File);
    return NT_SUCCESS(Status) ? 0 : -NtStatusToErrno(Status);
}

static int
NtfsFuseReadlink(const char* Path, char* Buffer, size_t Size)
{
    PNtfsFileRecord File = NULL;
    std::string Target;
    NTSTATUS Status;
    size_t CopyLength;

    if (Size == 0)
        return -ERANGE;

    Status = Lookup(FuseState()->Volume, Path, &File);
    if (NT_SUCCESS(Status))
        Status = GetReparseTarget(File, Target);
    NtfsFileRecordDestroy(File);
    if (!NT_SUCCESS(Status))
        return -NtStatusToErrno(Status);

    CopyLength = Target.size() < Size - 1 ? Target.size() : Size - 1;
    memcpy(Buffer, Target.data(), CopyLength);
    Buffer[CopyLength] = '\0';
    return 0;
}

static int
NtfsFuseAccess(const char* Path, int Mask)
{
    PNtfsFileRecord File = NULL;
    NTSTATUS Status;

    if ((Mask & W_OK) && !FuseState()->Writable)
        return -EROFS;
    Status = Lookup(FuseState()->Volume, Path, &File);
    NtfsFileRecordDestroy(File);
    return NT_SUCCESS(Status) ? 0 : -NtStatusToErrno(Status);
}

static PNtfsFileRecord
HandleRecord(struct fuse_file_info* FileInfo)
{
    return (PNtfsFileRecord)(uintptr_t)
        (FileInfo->fh & ~NTFS_FUSE_CACHED_RECORD_BIT);
}

static int
NtfsFuseOpen(const char* Path, struct fuse_file_info* FileInfo)
{
    NtfsFuseState* State = FuseState();
    PNtfsFileRecord File = NULL;
    PAttribute Data;
    bool Cached = false;
    NTSTATUS Status;

    if ((FileInfo->flags & O_ACCMODE) != O_RDONLY &&
        !State->Writable)
    {
        return -EROFS;
    }

    /* Read-only mounts never mutate, so records can be reused across
     * opens for the mount lifetime. Writable mounts skip the cache: a
     * path mutation would otherwise have to drop records that open
     * handles still reference.
     */
    if (!State->Writable)
    {
        auto Existing = State->RecordCache.find(Path);
        if (Existing != State->RecordCache.end())
        {
            File = Existing->second;
            Cached = true;
        }
    }
    if (!File)
    {
        Status = Lookup(State->Volume, Path, &File);
        if (!NT_SUCCESS(Status))
            return -NtStatusToErrno(Status);
    }
    if (NtfsFileRecordGetHeader(File)->Flags & FR_IS_DIRECTORY)
    {
        if (!Cached)
            NtfsFileRecordDestroy(File);
        return -EISDIR;
    }

    Data = NtfsFileRecordGetAttribute(File, TypeData, NULL);
    if (!Data)
    {
        if (!Cached)
            NtfsFileRecordDestroy(File);
        return -ENOENT;
    }
    if (Data->Flags & ATTR_ENCRYPTED)
    {
        if (!Cached)
            NtfsFileRecordDestroy(File);
        return -EOPNOTSUPP;
    }

    if (!State->Writable && !Cached)
    {
        State->RecordCache.emplace(Path, File);
        Cached = true;
    }

    FileInfo->fh = (uint64_t)(uintptr_t)File;
    if (Cached)
        FileInfo->fh |= NTFS_FUSE_CACHED_RECORD_BIT;
    /* Writable mounts bypass the kernel page/attribute data cache so a
     * truncate or write through one handle cannot leave stale cached pages
     * behind another; read-only mounts keep the fast cached path.
     */
    if (State->Writable)
        FileInfo->direct_io = 1;
    else
        FileInfo->keep_cache = 1;
    return 0;
}

static int
NtfsFuseRelease(const char* Path, struct fuse_file_info* FileInfo)
{
    UNREFERENCED_PARAMETER(Path);
    if (!(FileInfo->fh & NTFS_FUSE_CACHED_RECORD_BIT))
        NtfsFileRecordDestroy(HandleRecord(FileInfo));
    FileInfo->fh = 0;
    return 0;
}

static int
NtfsFuseRead(const char* Path,
             char* Buffer,
             size_t Size,
             off_t Offset,
             struct fuse_file_info* FileInfo)
{
    PNtfsFileRecord File;
    ULONG Requested;
    ULONG Remaining;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Path);
    if (Offset < 0)
        return -EINVAL;
    if (Size == 0)
        return 0;

    File = HandleRecord(FileInfo);
    if (!File)
        return -EBADF;

    Requested = Size > MAXULONG ? MAXULONG : (ULONG)Size;
    Remaining = Requested;
    Status = NtfsFileRecordCopyData(File,
                                    TypeData,
                                    NULL,
                                    (PUCHAR)Buffer,
                                    &Remaining,
                                    (ULONGLONG)Offset);
    if (Status == STATUS_END_OF_FILE)
        return 0;
    if (!NT_SUCCESS(Status))
        return -NtStatusToErrno(Status);
    return (int)(Requested - Remaining);
}

static int
NtfsFuseOpendir(const char* Path, struct fuse_file_info* FileInfo)
{
    NtfsFuseState* State = FuseState();
    PNtfsFileRecord File = NULL;
    PNtfsDirectory Directory = NULL;
    NtfsDirectoryEntry Entry;
    BOOLEAN Restart = TRUE;
    NTSTATUS Status;
    int Result = 0;
    NtfsDirectoryListing Listing;
    auto Cached = State->DirectoryCache.find(Path);

    if (Cached == State->DirectoryCache.end())
    {
        Status = Lookup(State->Volume, Path, &File);
        if (!NT_SUCCESS(Status))
        {
            Result = -NtStatusToErrno(Status);
            goto Done;
        }
        if (!(NtfsFileRecordGetHeader(File)->Flags & FR_IS_DIRECTORY))
        {
            Result = -ENOTDIR;
            goto Done;
        }

        Directory = NtfsDirectoryCreate(State->Volume);
        if (!Directory)
        {
            Result = -ENOMEM;
            goto Done;
        }
        Status = NtfsDirectoryLoadDirectory(Directory, File);
        if (!NT_SUCCESS(Status))
        {
            Result = -NtStatusToErrno(Status);
            goto Done;
        }

        for (;;)
        {
            std::string Name;

            Status = NtfsDirectoryReadNext(Directory, Restart, &Entry);
            Restart = FALSE;
            if (Status == STATUS_NO_MORE_FILES)
                break;
            if (!NT_SUCCESS(Status))
            {
                Result = -NtStatusToErrno(Status);
                goto Done;
            }
            if (!Utf16ToUtf8(Entry.Name, Entry.NameLength, Name))
            {
                Result = -EILSEQ;
                goto Done;
            }
            Listing.push_back(std::move(Name));
        }

        Cached = State->DirectoryCache
                     .emplace(Path,
                              std::make_shared<NtfsDirectoryListing>(
                                  std::move(Listing)))
                     .first;
    }

    /* The handle owns a reference to the snapshot, so namespace mutations
     * can clear the cache without freeing a listing a reader still holds.
     */
    FileInfo->fh = (uint64_t)(uintptr_t)
        new std::shared_ptr<NtfsDirectoryListing>(Cached->second);
    FileInfo->keep_cache = 1;
    FileInfo->cache_readdir = 1;

Done:
    NtfsDirectoryDestroy(Directory);
    NtfsFileRecordDestroy(File);
    return Result;
}

static int
NtfsFuseReleasedir(const char* Path, struct fuse_file_info* FileInfo)
{
    UNREFERENCED_PARAMETER(Path);
    delete (std::shared_ptr<NtfsDirectoryListing>*)(uintptr_t)FileInfo->fh;
    FileInfo->fh = 0;
    return 0;
}

static int
NtfsFuseReaddir(const char* Path,
                void* Buffer,
                fuse_fill_dir_t Filler,
                off_t Offset,
                struct fuse_file_info* FileInfo,
                enum fuse_readdir_flags Flags)
{
    const std::shared_ptr<NtfsDirectoryListing>* Handle =
        (const std::shared_ptr<NtfsDirectoryListing>*)(uintptr_t)
            FileInfo->fh;
    const NtfsDirectoryListing* Listing = Handle ? Handle->get() : NULL;
    const enum fuse_fill_dir_flags Plain = (enum fuse_fill_dir_flags)0;

    UNREFERENCED_PARAMETER(Path);
    UNREFERENCED_PARAMETER(Flags);

    if (!Listing || Offset < 0)
        return -EBADF;

    if (Offset <= 0 &&
        Filler(Buffer, ".", NULL, 1, Plain) != 0)
        return 0;
    if (Offset <= 1 &&
        Filler(Buffer, "..", NULL, 2, Plain) != 0)
        return 0;

    for (size_t Index = Offset > 2 ? (size_t)Offset - 2 : 0;
         Index < Listing->size();
         Index++)
    {
        if (Filler(Buffer,
                   (*Listing)[Index].c_str(),
                   NULL,
                   (off_t)Index + 3,
                   Plain) != 0)
        {
            break;
        }
    }

    return 0;
}

static int
NtfsFuseStatfs(const char* Path, struct statvfs* Stat)
{
    NtfsFuseState* State = FuseState();
    LARGE_INTEGER FreeClusters;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Path);
    memset(Stat, 0, sizeof(*Stat));
    Stat->f_bsize =
        NtfsVolumeGetBytesPerSector(State->Volume) *
        NtfsVolumeGetSectorsPerCluster(State->Volume);
    Stat->f_frsize = Stat->f_bsize;
    Stat->f_blocks = NtfsVolumeGetClustersInVolume(State->Volume);
    Status = NtfsVolumeGetFreeClusters(State->Volume, &FreeClusters);
    if (!NT_SUCCESS(Status))
        return -NtStatusToErrno(Status);
    Stat->f_bfree = FreeClusters.QuadPart;
    Stat->f_bavail = FreeClusters.QuadPart;
    Stat->f_namemax = NTFS_MAX_FILE_NAME_LENGTH;
    if (!State->Writable)
        Stat->f_flag = ST_RDONLY;
    return 0;
}

#ifndef RENAME_NOREPLACE
#define RENAME_NOREPLACE (1U << 0)
#endif
#ifndef RENAME_EXCHANGE
#define RENAME_EXCHANGE (1U << 1)
#endif

static UINT64
TimespecToNtfsTime(const struct timespec* Time)
{
    const UINT64 EpochDifference = UINT64_C(116444736000000000);

    return EpochDifference +
           (UINT64)Time->tv_sec * UINT64_C(10000000) +
           (UINT64)Time->tv_nsec / 100;
}

/* Namespace mutations drop every cached listing; open directory handles
 * keep their snapshots alive through shared ownership.
 */
static void
InvalidateDirectoryCache(NtfsFuseState* State)
{
    State->DirectoryCache.clear();
}

static int
RequireWritable(NtfsFuseState* State)
{
    return State->Writable ? 0 : -EROFS;
}

static NTSTATUS
CreateNodeByPath(NtfsFuseState* State,
                 const char* Path,
                 bool IsDirectory)
{
    std::vector<WCHAR> NtfsPath;
    PNtfsFileRecord File = NULL;
    NTSTATUS Status;

    if (!Utf8ToNtfsPath(Path, NtfsPath) ||
        NtfsPath.size() < 2 ||
        NtfsPath.size() - 1 > MAXULONG)
    {
        return STATUS_OBJECT_NAME_INVALID;
    }
    Status = NtfsMasterFileTableCreateFile(
        NtfsVolumeGetMft(State->Volume),
        NtfsPath.data(),
        (ULONG)(NtfsPath.size() - 1),
        IsDirectory,
        0,
        &File);
    NtfsFileRecordDestroy(File);
    if (NT_SUCCESS(Status))
        InvalidateDirectoryCache(State);
    return Status;
}

static NTSTATUS
RemoveNodeByPath(NtfsFuseState* State,
                 const char* Path,
                 bool IsDirectory)
{
    std::vector<WCHAR> NtfsPath;
    NTSTATUS Status;

    if (!Utf8ToNtfsPath(Path, NtfsPath) ||
        NtfsPath.size() < 2 ||
        NtfsPath.size() - 1 > MAXULONG)
    {
        return STATUS_OBJECT_NAME_INVALID;
    }
    Status = NtfsMasterFileTableDeleteFile(
        NtfsVolumeGetMft(State->Volume),
        NtfsPath.data(),
        (ULONG)(NtfsPath.size() - 1),
        IsDirectory);
    if (NT_SUCCESS(Status))
        InvalidateDirectoryCache(State);
    return Status;
}

static NTSTATUS
RenameNodeByPath(NtfsFuseState* State,
                 const char* OldPath,
                 const char* NewPath)
{
    std::vector<WCHAR> OldNtfsPath;
    std::vector<WCHAR> NewNtfsPath;
    NTSTATUS Status;

    if (!Utf8ToNtfsPath(OldPath, OldNtfsPath) ||
        !Utf8ToNtfsPath(NewPath, NewNtfsPath) ||
        OldNtfsPath.size() < 2 ||
        NewNtfsPath.size() < 2 ||
        OldNtfsPath.size() - 1 > MAXULONG ||
        NewNtfsPath.size() - 1 > MAXULONG)
    {
        return STATUS_OBJECT_NAME_INVALID;
    }
    Status = NtfsMasterFileTableRenameFile(
        NtfsVolumeGetMft(State->Volume),
        OldNtfsPath.data(),
        (ULONG)(OldNtfsPath.size() - 1),
        NewNtfsPath.data(),
        (ULONG)(NewNtfsPath.size() - 1));
    if (NT_SUCCESS(Status))
        InvalidateDirectoryCache(State);
    return Status;
}

static int
NtfsFuseCreate(const char* Path,
               mode_t Mode,
               struct fuse_file_info* FileInfo)
{
    NtfsFuseState* State = FuseState();
    NTSTATUS Status;
    int Result;

    UNREFERENCED_PARAMETER(Mode);
    Result = RequireWritable(State);
    if (Result != 0)
        return Result;

    Status = CreateNodeByPath(State, Path, false);
    if (Status == STATUS_OBJECT_NAME_COLLISION &&
        !(FileInfo->flags & O_EXCL))
    {
        Status = STATUS_SUCCESS;
    }
    if (!NT_SUCCESS(Status))
        return -NtStatusToErrno(Status);
    return NtfsFuseOpen(Path, FileInfo);
}

static int
NtfsFuseMknod(const char* Path, mode_t Mode, dev_t Device)
{
    NtfsFuseState* State = FuseState();
    NTSTATUS Status;
    int Result;

    UNREFERENCED_PARAMETER(Device);
    Result = RequireWritable(State);
    if (Result != 0)
        return Result;
    if (!S_ISREG(Mode))
        return -EOPNOTSUPP;

    Status = CreateNodeByPath(State, Path, false);
    return NT_SUCCESS(Status) ? 0 : -NtStatusToErrno(Status);
}

static int
NtfsFuseMkdir(const char* Path, mode_t Mode)
{
    NtfsFuseState* State = FuseState();
    NTSTATUS Status;
    int Result;

    UNREFERENCED_PARAMETER(Mode);
    Result = RequireWritable(State);
    if (Result != 0)
        return Result;

    Status = CreateNodeByPath(State, Path, true);
    return NT_SUCCESS(Status) ? 0 : -NtStatusToErrno(Status);
}

static int
NtfsFuseUnlink(const char* Path)
{
    NtfsFuseState* State = FuseState();
    NTSTATUS Status;
    int Result;

    Result = RequireWritable(State);
    if (Result != 0)
        return Result;

    Status = RemoveNodeByPath(State, Path, false);
    return NT_SUCCESS(Status) ? 0 : -NtStatusToErrno(Status);
}

static int
NtfsFuseRmdir(const char* Path)
{
    NtfsFuseState* State = FuseState();
    NTSTATUS Status;
    int Result;

    Result = RequireWritable(State);
    if (Result != 0)
        return Result;

    Status = RemoveNodeByPath(State, Path, true);
    return NT_SUCCESS(Status) ? 0 : -NtStatusToErrno(Status);
}

static int
NtfsFuseRename(const char* OldPath,
               const char* NewPath,
               unsigned int Flags)
{
    NtfsFuseState* State = FuseState();
    NTSTATUS Status;
    int Result;

    Result = RequireWritable(State);
    if (Result != 0)
        return Result;
    if (Flags & RENAME_EXCHANGE)
        return -EOPNOTSUPP;

    Status = RenameNodeByPath(State, OldPath, NewPath);
    if (Status == STATUS_OBJECT_NAME_COLLISION &&
        !(Flags & RENAME_NOREPLACE))
    {
        PNtfsFileRecord Target = NULL;
        NTSTATUS TargetStatus;
        bool TargetIsDirectory;

        /* POSIX rename replaces an existing destination. Without LFS
         * transactions the remove and rename publish separately; every
         * intermediate state stays readable, but the pair is not atomic.
         */
        TargetStatus = Lookup(State->Volume, NewPath, &Target);
        if (NT_SUCCESS(TargetStatus))
        {
            TargetIsDirectory =
                (NtfsFileRecordGetHeader(Target)->Flags &
                 FR_IS_DIRECTORY) != 0;
            NtfsFileRecordDestroy(Target);
            Status = RemoveNodeByPath(State, NewPath, TargetIsDirectory);
            if (NT_SUCCESS(Status))
                Status = RenameNodeByPath(State, OldPath, NewPath);
        }
    }
    return NT_SUCCESS(Status) ? 0 : -NtStatusToErrno(Status);
}

static int
NtfsFuseLink(const char* ExistingPath, const char* NewPath)
{
    NtfsFuseState* State = FuseState();
    std::vector<WCHAR> ExistingNtfsPath;
    std::vector<WCHAR> NewNtfsPath;
    NTSTATUS Status;
    int Result;

    Result = RequireWritable(State);
    if (Result != 0)
        return Result;

    if (!Utf8ToNtfsPath(ExistingPath, ExistingNtfsPath) ||
        !Utf8ToNtfsPath(NewPath, NewNtfsPath) ||
        ExistingNtfsPath.size() < 2 ||
        NewNtfsPath.size() < 2 ||
        ExistingNtfsPath.size() - 1 > MAXULONG ||
        NewNtfsPath.size() - 1 > MAXULONG)
    {
        return -EINVAL;
    }
    Status = NtfsMasterFileTableCreateHardLink(
        NtfsVolumeGetMft(State->Volume),
        ExistingNtfsPath.data(),
        (ULONG)(ExistingNtfsPath.size() - 1),
        NewNtfsPath.data(),
        (ULONG)(NewNtfsPath.size() - 1));
    if (NT_SUCCESS(Status))
        InvalidateDirectoryCache(State);
    return NT_SUCCESS(Status) ? 0 : -NtStatusToErrno(Status);
}

static int
NtfsFuseWrite(const char* Path,
              const char* Buffer,
              size_t Size,
              off_t Offset,
              struct fuse_file_info* FileInfo)
{
    NtfsFuseState* State = FuseState();
    PNtfsFileRecord File;
    LARGE_INTEGER ByteOffset;
    ULONG Length;
    NTSTATUS Status;
    int Result;

    UNREFERENCED_PARAMETER(Path);
    Result = RequireWritable(State);
    if (Result != 0)
        return Result;
    if (Offset < 0)
        return -EINVAL;
    if (Size == 0)
        return 0;

    File = HandleRecord(FileInfo);
    if (!File)
        return -EBADF;

    Length = Size > MAXULONG ? MAXULONG : (ULONG)Size;
    ByteOffset.QuadPart = (LONGLONG)Offset;
    Status = NtfsFileRecordWriteFileData(
        File,
        TypeData,
        NULL,
        (PUCHAR)const_cast<char*>(Buffer),
        &Length,
        &ByteOffset);
    if (!NT_SUCCESS(Status))
        return -NtStatusToErrno(Status);
    return (int)Length;
}

static int
NtfsFuseTruncate(const char* Path,
                 off_t Size,
                 struct fuse_file_info* FileInfo)
{
    NtfsFuseState* State = FuseState();
    PNtfsFileRecord File;
    PNtfsFileRecord Owned = NULL;
    NTSTATUS Status;
    int Result;

    Result = RequireWritable(State);
    if (Result != 0)
        return Result;
    if (Size < 0)
        return -EINVAL;

    if (FileInfo && FileInfo->fh)
    {
        File = HandleRecord(FileInfo);
    }
    else
    {
        Status = Lookup(State->Volume, Path, &Owned);
        if (!NT_SUCCESS(Status))
            return -NtStatusToErrno(Status);
        if (NtfsFileRecordGetHeader(Owned)->Flags & FR_IS_DIRECTORY)
        {
            NtfsFileRecordDestroy(Owned);
            return -EISDIR;
        }
        File = Owned;
    }

    Status = NtfsFileRecordSetFileDataSize(File,
                                           TypeData,
                                           NULL,
                                           (ULONGLONG)Size);
    NtfsFileRecordDestroy(Owned);
    return NT_SUCCESS(Status) ? 0 : -NtStatusToErrno(Status);
}

static int
NtfsFuseFallocate(const char* Path,
                  int Mode,
                  off_t Offset,
                  off_t Length,
                  struct fuse_file_info* FileInfo)
{
    NtfsFuseState* State = FuseState();
    PNtfsFileRecord File;
    PNtfsFileRecord Owned = NULL;
    struct stat Stat;
    ULONGLONG Target;
    NTSTATUS Status;
    int Result;

    Result = RequireWritable(State);
    if (Result != 0)
        return Result;
    if (Mode != 0)
        return -EOPNOTSUPP;
    if (Offset < 0 || Length <= 0 ||
        (uint64_t)Offset > UINT64_MAX - (uint64_t)Length)
    {
        return -EINVAL;
    }
    Target = (ULONGLONG)Offset + (ULONGLONG)Length;

    if (FileInfo && FileInfo->fh)
    {
        File = HandleRecord(FileInfo);
    }
    else
    {
        Status = Lookup(State->Volume, Path, &Owned);
        if (!NT_SUCCESS(Status))
            return -NtStatusToErrno(Status);
        File = Owned;
    }

    /* Preallocation must never shrink the existing reservation, so only
     * grow the allocation when the requested range exceeds it.
     */
    Status = FillStatFromRecord(File, State, &Stat);
    if (NT_SUCCESS(Status) &&
        Target > (ULONGLONG)Stat.st_blocks * 512)
    {
        Status = NtfsFileRecordSetFileAllocationSize(File,
                                                     TypeData,
                                                     NULL,
                                                     Target);
    }
    NtfsFileRecordDestroy(Owned);
    return NT_SUCCESS(Status) ? 0 : -NtStatusToErrno(Status);
}

static int
NtfsFuseUtimens(const char* Path,
                const struct timespec Times[2],
                struct fuse_file_info* FileInfo)
{
    NtfsFuseState* State = FuseState();
    PNtfsFileRecord File = NULL;
    NtfsFileBasicInformation Information;
    struct timespec Now;
    NTSTATUS Status;
    int Result;

    UNREFERENCED_PARAMETER(FileInfo);
    Result = RequireWritable(State);
    if (Result != 0)
        return Result;

    Status = Lookup(State->Volume, Path, &File);
    if (!NT_SUCCESS(Status))
        return -NtStatusToErrno(Status);

    Status = NtfsFileRecordGetBasicInformation(File, &Information);
    if (NT_SUCCESS(Status))
    {
        clock_gettime(CLOCK_REALTIME, &Now);
        if (!Times || Times[0].tv_nsec == UTIME_NOW)
            Information.LastAccessTime = TimespecToNtfsTime(&Now);
        else if (Times[0].tv_nsec != UTIME_OMIT)
            Information.LastAccessTime = TimespecToNtfsTime(&Times[0]);
        if (!Times || Times[1].tv_nsec == UTIME_NOW)
            Information.LastWriteTime = TimespecToNtfsTime(&Now);
        else if (Times[1].tv_nsec != UTIME_OMIT)
            Information.LastWriteTime = TimespecToNtfsTime(&Times[1]);
        Information.ChangeTime = TimespecToNtfsTime(&Now);
        Status = NtfsFileRecordSetBasicInformation(File, &Information);
    }
    NtfsFileRecordDestroy(File);
    return NT_SUCCESS(Status) ? 0 : -NtStatusToErrno(Status);
}

/* NTFS has no POSIX mode or ownership bits without an ACL mapping, so
 * these accept and discard the request exactly like NTFS-3G does when no
 * user mapping is configured. Refusing them instead would break common
 * tools such as cp -p and tar.
 */
static int
NtfsFuseChmod(const char* Path,
              mode_t Mode,
              struct fuse_file_info* FileInfo)
{
    UNREFERENCED_PARAMETER(Path);
    UNREFERENCED_PARAMETER(Mode);
    UNREFERENCED_PARAMETER(FileInfo);
    return RequireWritable(FuseState());
}

static int
NtfsFuseChown(const char* Path,
              uid_t Owner,
              gid_t Group,
              struct fuse_file_info* FileInfo)
{
    UNREFERENCED_PARAMETER(Path);
    UNREFERENCED_PARAMETER(Owner);
    UNREFERENCED_PARAMETER(Group);
    UNREFERENCED_PARAMETER(FileInfo);
    return RequireWritable(FuseState());
}

/* Every mutation is synchronous through the shared core and the backing
 * image is a host file, so fsync has nothing left to schedule. flush is
 * deliberately not registered: a no-op hook would still cost one FUSE
 * round trip on every close.
 */
static int
NtfsFuseFsync(const char* Path,
              int DataOnly,
              struct fuse_file_info* FileInfo)
{
    UNREFERENCED_PARAMETER(Path);
    UNREFERENCED_PARAMETER(DataOnly);
    UNREFERENCED_PARAMETER(FileInfo);
    return 0;
}

/*
 * Native NTFS extended attributes are exposed as user.* xattrs. NTFS-3G
 * only offers the packed list through system.ntfs_ea; projecting each EA
 * as its own attribute keeps ordinary getfattr/setfattr tooling usable.
 * EA names are case-insensitive single-byte strings.
 */
static const char NtfsFuseXattrPrefix[] = "user.";

static bool
XattrToEaName(const char* Name,
              const UCHAR** EaName,
              size_t* EaNameLength)
{
    size_t PrefixLength = sizeof(NtfsFuseXattrPrefix) - 1;
    size_t Length;

    if (strncmp(Name, NtfsFuseXattrPrefix, PrefixLength) != 0)
        return false;
    Length = strlen(Name + PrefixLength);
    if (Length == 0 || Length > 0xff)
        return false;
    *EaName = (const UCHAR*)(Name + PrefixLength);
    *EaNameLength = Length;
    return true;
}

static bool
EaNamesEqual(const UCHAR* Left,
             size_t LeftLength,
             const UCHAR* Right,
             size_t RightLength)
{
    if (LeftLength != RightLength)
        return false;
    for (size_t Index = 0; Index < LeftLength; Index++)
    {
        if (toupper(Left[Index]) != toupper(Right[Index]))
            return false;
    }
    return true;
}

static NTSTATUS
ReadAllExtendedAttributes(PNtfsFileRecord File,
                          std::vector<UCHAR>& RawData,
                          ULONG* RawLength)
{
    NTSTATUS Status;

    *RawLength = 0;
    Status = NtfsFileRecordReadExtendedAttributes(File,
                                                  NULL,
                                                  RawLength,
                                                  NULL);
    if (Status == STATUS_NO_EAS_ON_FILE)
    {
        *RawLength = 0;
        return STATUS_SUCCESS;
    }
    if (Status != STATUS_BUFFER_TOO_SMALL || *RawLength == 0)
        return NT_SUCCESS(Status) ? STATUS_SUCCESS : Status;

    RawData.resize(*RawLength);
    return NtfsFileRecordReadExtendedAttributes(File,
                                                RawData.data(),
                                                RawLength,
                                                NULL);
}

static NTSTATUS
FindExtendedAttribute(const std::vector<UCHAR>& RawData,
                      ULONG RawLength,
                      const UCHAR* Name,
                      size_t NameLength,
                      NtfsExtendedAttributeView* Found,
                      bool* Present)
{
    NtfsExtendedAttributeView View;
    ULONG Offset = 0;
    NTSTATUS Status;

    *Present = false;
    for (;;)
    {
        Status = NtfsGetNextExtendedAttribute(RawData.data(),
                                              RawLength,
                                              &Offset,
                                              &View);
        if (Status == STATUS_NO_MORE_EAS)
            return STATUS_SUCCESS;
        if (!NT_SUCCESS(Status))
            return Status;
        if (EaNamesEqual(View.Name,
                         View.NameLength,
                         Name,
                         NameLength))
        {
            *Found = View;
            *Present = true;
            return STATUS_SUCCESS;
        }
    }
}

static int
NtfsFuseGetxattr(const char* Path,
                 const char* Name,
                 char* Value,
                 size_t Size)
{
    NtfsFuseState* State = FuseState();
    PNtfsFileRecord File = NULL;
    std::vector<UCHAR> RawData;
    NtfsExtendedAttributeView View;
    const UCHAR* EaName;
    size_t EaNameLength;
    ULONG RawLength;
    bool Present;
    NTSTATUS Status;

    if (!XattrToEaName(Name, &EaName, &EaNameLength))
        return -ENODATA;

    Status = Lookup(State->Volume, Path, &File);
    if (!NT_SUCCESS(Status))
        return -NtStatusToErrno(Status);
    Status = ReadAllExtendedAttributes(File, RawData, &RawLength);
    if (NT_SUCCESS(Status))
        Status = FindExtendedAttribute(RawData,
                                       RawLength,
                                       EaName,
                                       EaNameLength,
                                       &View,
                                       &Present);
    NtfsFileRecordDestroy(File);
    if (!NT_SUCCESS(Status))
        return -NtStatusToErrno(Status);
    if (!Present)
        return -ENODATA;
    if (Size == 0)
        return (int)View.ValueLength;
    if (Size < View.ValueLength)
        return -ERANGE;
    memcpy(Value, View.Value, View.ValueLength);
    return (int)View.ValueLength;
}

static int
NtfsFuseListxattr(const char* Path, char* List, size_t Size)
{
    NtfsFuseState* State = FuseState();
    PNtfsFileRecord File = NULL;
    std::vector<UCHAR> RawData;
    NtfsExtendedAttributeView View;
    ULONG RawLength;
    ULONG Offset = 0;
    size_t Needed = 0;
    size_t Emitted = 0;
    NTSTATUS Status;

    Status = Lookup(State->Volume, Path, &File);
    if (!NT_SUCCESS(Status))
        return -NtStatusToErrno(Status);
    Status = ReadAllExtendedAttributes(File, RawData, &RawLength);
    NtfsFileRecordDestroy(File);
    if (!NT_SUCCESS(Status))
        return -NtStatusToErrno(Status);

    for (;;)
    {
        Status = NtfsGetNextExtendedAttribute(RawData.data(),
                                              RawLength,
                                              &Offset,
                                              &View);
        if (Status == STATUS_NO_MORE_EAS)
            break;
        if (!NT_SUCCESS(Status))
            return -NtStatusToErrno(Status);

        Needed += sizeof(NtfsFuseXattrPrefix) - 1 +
                  View.NameLength + 1;
        if (Size == 0)
            continue;
        if (Needed > Size)
            return -ERANGE;
        memcpy(List + Emitted,
               NtfsFuseXattrPrefix,
               sizeof(NtfsFuseXattrPrefix) - 1);
        Emitted += sizeof(NtfsFuseXattrPrefix) - 1;
        memcpy(List + Emitted, View.Name, View.NameLength);
        Emitted += View.NameLength;
        List[Emitted++] = '\0';
    }
    return (int)Needed;
}

static int
NtfsFuseSetxattr(const char* Path,
                 const char* Name,
                 const char* Value,
                 size_t Size,
                 int Flags)
{
    NtfsFuseState* State = FuseState();
    PNtfsFileRecord File = NULL;
    NtfsExtendedAttributeUpdate Update = {};
    const UCHAR* EaName;
    size_t EaNameLength;
    NTSTATUS Status;
    int Result;

    Result = RequireWritable(State);
    if (Result != 0)
        return Result;
    if (!XattrToEaName(Name, &EaName, &EaNameLength))
        return -EOPNOTSUPP;
    if (Size > 0xffff)
        return -E2BIG;

    Status = Lookup(State->Volume, Path, &File);
    if (!NT_SUCCESS(Status))
        return -NtStatusToErrno(Status);

    if (Flags & (XATTR_CREATE | XATTR_REPLACE))
    {
        std::vector<UCHAR> RawData;
        NtfsExtendedAttributeView View;
        ULONG RawLength;
        bool Present = false;

        Status = ReadAllExtendedAttributes(File, RawData, &RawLength);
        if (NT_SUCCESS(Status))
            Status = FindExtendedAttribute(RawData,
                                           RawLength,
                                           EaName,
                                           EaNameLength,
                                           &View,
                                           &Present);
        if (NT_SUCCESS(Status) &&
            (Flags & XATTR_CREATE) && Present)
        {
            Status = STATUS_OBJECT_NAME_COLLISION;
        }
        if (NT_SUCCESS(Status) &&
            (Flags & XATTR_REPLACE) && !Present)
        {
            NtfsFileRecordDestroy(File);
            return -ENODATA;
        }
    }

    if (NT_SUCCESS(Status))
    {
        Update.Operation = NTFS_EA_UPDATE_SET;
        Update.Flags = 0;
        Update.NameLength = (UINT8)EaNameLength;
        Update.ValueLength = (UINT16)Size;
        Update.Name = EaName;
        Update.Value = (const UCHAR*)Value;
        Status = NtfsFileRecordUpdateExtendedAttributes(File,
                                                        &Update,
                                                        1);
    }
    NtfsFileRecordDestroy(File);
    return NT_SUCCESS(Status) ? 0 : -NtStatusToErrno(Status);
}

static int
NtfsFuseRemovexattr(const char* Path, const char* Name)
{
    NtfsFuseState* State = FuseState();
    PNtfsFileRecord File = NULL;
    std::vector<UCHAR> RawData;
    NtfsExtendedAttributeView View;
    NtfsExtendedAttributeUpdate Update = {};
    const UCHAR* EaName;
    size_t EaNameLength;
    ULONG RawLength;
    bool Present = false;
    NTSTATUS Status;
    int Result;

    Result = RequireWritable(State);
    if (Result != 0)
        return Result;
    if (!XattrToEaName(Name, &EaName, &EaNameLength))
        return -ENODATA;

    Status = Lookup(State->Volume, Path, &File);
    if (!NT_SUCCESS(Status))
        return -NtStatusToErrno(Status);
    Status = ReadAllExtendedAttributes(File, RawData, &RawLength);
    if (NT_SUCCESS(Status))
        Status = FindExtendedAttribute(RawData,
                                       RawLength,
                                       EaName,
                                       EaNameLength,
                                       &View,
                                       &Present);
    if (NT_SUCCESS(Status) && !Present)
    {
        NtfsFileRecordDestroy(File);
        return -ENODATA;
    }
    if (NT_SUCCESS(Status))
    {
        Update.Operation = NTFS_EA_UPDATE_REMOVE;
        Update.NameLength = (UINT8)EaNameLength;
        Update.Name = EaName;
        Status = NtfsFileRecordUpdateExtendedAttributes(File,
                                                        &Update,
                                                        1);
    }
    NtfsFileRecordDestroy(File);
    return NT_SUCCESS(Status) ? 0 : -NtStatusToErrno(Status);
}

static int
MountImage(int Argc, char** Argv, NtfsFuseState* State)
{
    struct fuse_operations Operations = {};

    Operations.init = NtfsFuseInit;
    Operations.getattr = NtfsFuseGetattr;
    Operations.readlink = NtfsFuseReadlink;
    Operations.access = NtfsFuseAccess;
    Operations.open = NtfsFuseOpen;
    Operations.release = NtfsFuseRelease;
    Operations.read = NtfsFuseRead;
    Operations.opendir = NtfsFuseOpendir;
    Operations.readdir = NtfsFuseReaddir;
    Operations.releasedir = NtfsFuseReleasedir;
    Operations.statfs = NtfsFuseStatfs;
    /* The mutation hooks refuse every request with EROFS unless the mount
     * was opened with --writable.
     */
    Operations.create = NtfsFuseCreate;
    Operations.mknod = NtfsFuseMknod;
    Operations.mkdir = NtfsFuseMkdir;
    Operations.unlink = NtfsFuseUnlink;
    Operations.rmdir = NtfsFuseRmdir;
    Operations.rename = NtfsFuseRename;
    Operations.link = NtfsFuseLink;
    Operations.write = NtfsFuseWrite;
    Operations.truncate = NtfsFuseTruncate;
    Operations.fallocate = NtfsFuseFallocate;
    Operations.utimens = NtfsFuseUtimens;
    Operations.chmod = NtfsFuseChmod;
    Operations.chown = NtfsFuseChown;
    Operations.fsync = NtfsFuseFsync;
    Operations.getxattr = NtfsFuseGetxattr;
    Operations.listxattr = NtfsFuseListxattr;
    Operations.setxattr = NtfsFuseSetxattr;
    Operations.removexattr = NtfsFuseRemovexattr;
    return fuse_main(Argc, Argv, &Operations, State);
}
#endif

static void
PrintUsage(const char* Program)
{
    fprintf(stderr,
            "Usage:\n"
            "  %s [--show-metadata] --probe IMAGE\n"
            "  %s [--show-metadata] --list IMAGE [PATH]\n"
            "  %s [--show-metadata] --list-info IMAGE [PATH]\n"
            "  %s [--show-metadata] --cat IMAGE PATH\n"
            "  %s [--show-metadata] --cat-range IMAGE PATH OFFSET LENGTH\n"
            "  %s [--show-metadata] --readlink IMAGE PATH\n"
            "  %s [--show-metadata] --reparse IMAGE PATH\n"
            "  %s [--show-metadata] --ea IMAGE PATH\n"
            "  %s [--show-metadata] --security IMAGE PATH\n"
            "  %s [--show-metadata] --basic IMAGE PATH\n"
            "  %s --set-label IMAGE LABEL\n"
            "  %s --set-basic IMAGE PATH CREATION ACCESS WRITE CHANGE ATTRS\n"
            "  %s --set-security IMAGE PATH SOURCE\n"
            "  %s --set-reparse IMAGE PATH SOURCE\n"
            "  %s --delete-reparse IMAGE PATH SOURCE\n"
            "  %s --set-ea IMAGE PATH FLAGS NAME SOURCE\n"
            "  %s --remove-ea IMAGE PATH NAME\n"
            "  %s --delete-external-backing IMAGE PATH\n"
            "  %s --create-file IMAGE PATH [ATTRS]\n"
            "  %s --create-dir IMAGE PATH [ATTRS]\n"
            "  %s --remove IMAGE PATH\n"
            "  %s --remove-dir IMAGE PATH\n"
            "  %s --rename IMAGE OLD NEW\n"
            "  %s --link IMAGE EXISTING NEW\n"
            "  %s --write IMAGE PATH OFFSET SOURCE\n"
            "  %s --addfiles IMAGE LIST\n"
            "  %s --truncate IMAGE PATH SIZE\n"
            "  %s --allocate IMAGE PATH SIZE\n"
            "  %s --set-sparse IMAGE PATH 0|1\n"
            "  %s --zero-data IMAGE PATH OFFSET BEYOND_FINAL_ZERO\n"
            "  %s --allocated-ranges IMAGE PATH OFFSET LENGTH\n"
            "  %s --retrieval-pointers IMAGE PATH START_VCN\n"
            "  %s --volume-bitmap IMAGE START_LCN BYTE_COUNT\n"
            "  %s --volume-data IMAGE\n"
            "  %s --mft-record IMAGE FILE_REFERENCE\n"
            "  %s [--show-metadata] --streams IMAGE PATH\n"
            "  %s [--show-metadata] [--writable] IMAGE MOUNTPOINT"
            " [FUSE options]\n",
            Program,
            Program,
            Program,
            Program,
            Program,
            Program,
            Program,
            Program,
            Program,
            Program,
            Program,
            Program,
            Program,
            Program,
            Program,
            Program,
            Program,
            Program,
            Program,
            Program,
            Program,
            Program,
            Program,
            Program,
            Program,
            Program,
            Program,
            Program,
            Program,
            Program,
            Program,
            Program,
            Program,
            Program,
            Program,
            Program,
            Program);
}

int
main(int Argc, char** Argv)
{
    NtfsFuseState State = {};
    bool ShowMetadata = false;
    int First = 1;
    int Result;
    NTSTATUS Status;

    if (First < Argc && strcmp(Argv[First], "--show-metadata") == 0)
    {
        ShowMetadata = true;
        First++;
    }

    if (First >= Argc)
    {
        PrintUsage(Argv[0]);
        return 2;
    }

    if (strcmp(Argv[First], "--probe") == 0)
    {
        if (First + 2 != Argc)
        {
            PrintUsage(Argv[0]);
            return 2;
        }
        Status = OpenImage(Argv[First + 1], ShowMetadata, &State);
        if (!NT_SUCCESS(Status))
        {
            fprintf(stderr,
                    "%s: ntfslib status 0x%08" PRIx32 " (%s)\n",
                    Argv[First + 1],
                    (uint32_t)Status,
                    strerror(NtStatusToErrno(Status)));
            return 1;
        }
        Result = ProbeImage(&State);
        CloseImage(&State);
        return Result;
    }

    if (strcmp(Argv[First], "--list") == 0 ||
        strcmp(Argv[First], "--list-info") == 0)
    {
        bool IncludeAllocation =
            strcmp(Argv[First], "--list-info") == 0;
        const char* Path;
        if (First + 2 > Argc || First + 3 < Argc)
        {
            PrintUsage(Argv[0]);
            return 2;
        }
        Path = First + 2 < Argc ? Argv[First + 2] : "/";
        Status = OpenImage(Argv[First + 1], ShowMetadata, &State);
        if (!NT_SUCCESS(Status))
        {
            fprintf(stderr,
                    "%s: ntfslib status 0x%08" PRIx32 " (%s)\n",
                    Argv[First + 1],
                    (uint32_t)Status,
                    strerror(NtStatusToErrno(Status)));
            return 1;
        }
        Result = ListDirectory(&State,
                               Path,
                               IncludeAllocation);
        CloseImage(&State);
        return Result;
    }

    if (strcmp(Argv[First], "--cat") == 0)
    {
        if (First + 3 != Argc)
        {
            PrintUsage(Argv[0]);
            return 2;
        }
        Status = OpenImage(Argv[First + 1], ShowMetadata, &State);
        if (!NT_SUCCESS(Status))
        {
            fprintf(stderr,
                    "%s: ntfslib status 0x%08" PRIx32 " (%s)\n",
                    Argv[First + 1],
                    (uint32_t)Status,
                    strerror(NtStatusToErrno(Status)));
            return 1;
        }
        Result = CatFile(&State,
                         Argv[First + 2],
                         0,
                         ~(UINT64)0);
        CloseImage(&State);
        return Result;
    }

    if (strcmp(Argv[First], "--cat-range") == 0)
    {
        char* End;
        UINT64 Offset;
        UINT64 Length;

        if (First + 5 != Argc)
        {
            PrintUsage(Argv[0]);
            return 2;
        }
        errno = 0;
        Offset = strtoull(Argv[First + 3], &End, 0);
        if (errno || *Argv[First + 3] == '-' || *End)
        {
            PrintUsage(Argv[0]);
            return 2;
        }
        errno = 0;
        Length = strtoull(Argv[First + 4], &End, 0);
        if (errno || *Argv[First + 4] == '-' || *End)
        {
            PrintUsage(Argv[0]);
            return 2;
        }

        Status = OpenImage(Argv[First + 1], ShowMetadata, &State);
        if (!NT_SUCCESS(Status))
        {
            fprintf(stderr,
                    "%s: ntfslib status 0x%08" PRIx32 " (%s)\n",
                    Argv[First + 1],
                    (uint32_t)Status,
                    strerror(NtStatusToErrno(Status)));
            return 1;
        }
        Result = CatFile(&State,
                         Argv[First + 2],
                         Offset,
                         Length);
        CloseImage(&State);
        return Result;
    }

    if (strcmp(Argv[First],
               "--lookup-reparse") == 0)
    {
        bool OpenFinal;

        if (First + 4 != Argc ||
            (strcmp(Argv[First + 3], "0") != 0 &&
             strcmp(Argv[First + 3], "1") != 0))
        {
            PrintUsage(Argv[0]);
            return 2;
        }
        OpenFinal =
            strcmp(Argv[First + 3], "1") == 0;
        Status = OpenImage(
            Argv[First + 1],
            ShowMetadata,
            &State);
        if (!NT_SUCCESS(Status))
            return 1;
        Result = ProbeReparseLookup(
            State.Volume,
            Argv[First + 2],
            OpenFinal);
        CloseImage(&State);
        return Result;
    }

    if (strcmp(Argv[First], "--readlink") == 0)
    {
        if (First + 3 != Argc)
        {
            PrintUsage(Argv[0]);
            return 2;
        }
        Status = OpenImage(Argv[First + 1], ShowMetadata, &State);
        if (!NT_SUCCESS(Status))
        {
            fprintf(stderr,
                    "%s: ntfslib status 0x%08" PRIx32 " (%s)\n",
                    Argv[First + 1],
                    (uint32_t)Status,
                    strerror(NtStatusToErrno(Status)));
            return 1;
        }
        Result = ReadLink(&State, Argv[First + 2]);
        CloseImage(&State);
        return Result;
    }

    if (strcmp(Argv[First], "--reparse") == 0)
    {
        if (First + 3 != Argc)
        {
            PrintUsage(Argv[0]);
            return 2;
        }
        Status = OpenImage(Argv[First + 1],
                           ShowMetadata,
                           &State);
        if (!NT_SUCCESS(Status))
        {
            fprintf(stderr,
                    "%s: ntfslib status 0x%08" PRIx32
                    " (%s)\n",
                    Argv[First + 1],
                    (uint32_t)Status,
                    strerror(NtStatusToErrno(Status)));
            return 1;
        }
        Result = DumpReparsePoint(
            &State,
            Argv[First + 2]);
        CloseImage(&State);
        return Result;
    }

    if (strcmp(Argv[First], "--ea") == 0)
    {
        if (First + 3 != Argc)
        {
            PrintUsage(Argv[0]);
            return 2;
        }
        Status = OpenImage(Argv[First + 1], ShowMetadata, &State);
        if (!NT_SUCCESS(Status))
        {
            fprintf(stderr,
                    "%s: ntfslib status 0x%08" PRIx32 " (%s)\n",
                    Argv[First + 1],
                    (uint32_t)Status,
                    strerror(NtStatusToErrno(Status)));
            return 1;
        }
        Result = ListExtendedAttributes(&State, Argv[First + 2]);
        CloseImage(&State);
        return Result;
    }

    if (strcmp(Argv[First], "--security") == 0)
    {
        if (First + 3 != Argc)
        {
            PrintUsage(Argv[0]);
            return 2;
        }
        Status = OpenImage(Argv[First + 1],
                           ShowMetadata,
                           &State);
        if (!NT_SUCCESS(Status))
        {
            fprintf(stderr,
                    "%s: ntfslib status 0x%08" PRIx32
                    " (%s)\n",
                    Argv[First + 1],
                    (uint32_t)Status,
                    strerror(NtStatusToErrno(Status)));
            return 1;
        }
        Result = DumpSecurityDescriptor(
            &State,
            Argv[First + 2]);
        CloseImage(&State);
        return Result;
    }

    if (strcmp(Argv[First], "--basic") == 0)
    {
        if (First + 3 != Argc)
        {
            PrintUsage(Argv[0]);
            return 2;
        }
        Status = OpenImage(Argv[First + 1],
                           ShowMetadata,
                           &State);
        if (!NT_SUCCESS(Status))
        {
            fprintf(stderr,
                    "%s: ntfslib status 0x%08" PRIx32
                    " (%s)\n",
                    Argv[First + 1],
                    (uint32_t)Status,
                    strerror(NtStatusToErrno(Status)));
            return 1;
        }
        Result = PrintBasicInformation(
            &State,
            Argv[First + 2]);
        CloseImage(&State);
        return Result;
    }

    if (strcmp(Argv[First], "--set-label") == 0)
    {
        if (First + 3 != Argc || ShowMetadata)
        {
            PrintUsage(Argv[0]);
            return 2;
        }
        Status = OpenImageWritable(Argv[First + 1], &State);
        if (!NT_SUCCESS(Status))
        {
            fprintf(stderr,
                    "%s: ntfslib status 0x%08" PRIx32 " (%s)\n",
                    Argv[First + 1],
                    (uint32_t)Status,
                    strerror(NtStatusToErrno(Status)));
            return 1;
        }
        Result = SetImageLabel(&State, Argv[First + 2]);
        CloseImage(&State);
        return Result;
    }

    if (strcmp(Argv[First], "--set-basic") == 0)
    {
        static const UINT32 Masks[] =
        {
            NTFS_BASIC_INFO_CREATION_TIME,
            NTFS_BASIC_INFO_LAST_ACCESS_TIME,
            NTFS_BASIC_INFO_LAST_WRITE_TIME,
            NTFS_BASIC_INFO_CHANGE_TIME,
            NTFS_BASIC_INFO_FILE_ATTRIBUTES
        };
        NtfsFileBasicInformation Information = {};
        ULONGLONG Values[RTL_NUMBER_OF(Masks)] = {};

        if (First + 8 != Argc || ShowMetadata)
        {
            PrintUsage(Argv[0]);
            return 2;
        }
        for (ULONG Index = 0;
             Index < RTL_NUMBER_OF(Masks);
             Index++)
        {
            bool Present;
            ULONGLONG Maximum =
                Masks[Index] ==
                    NTFS_BASIC_INFO_FILE_ATTRIBUTES
                ? MAXULONG
                : INT64_MAX;

            if (!ParseOptionalUnsigned(
                    Argv[First + 3 + Index],
                    Maximum,
                    &Values[Index],
                    &Present))
            {
                fprintf(stderr,
                        "invalid basic-information value\n");
                return 2;
            }
            if (Present)
                Information.Fields |= Masks[Index];
        }
        Information.CreationTime = Values[0];
        Information.LastAccessTime = Values[1];
        Information.LastWriteTime = Values[2];
        Information.ChangeTime = Values[3];
        Information.FileAttributes =
            (UINT32)Values[4];

        Status = OpenImageWritable(Argv[First + 1],
                                   &State);
        if (!NT_SUCCESS(Status))
        {
            fprintf(stderr,
                    "%s: ntfslib status 0x%08" PRIx32
                    " (%s)\n",
                    Argv[First + 1],
                    (uint32_t)Status,
                    strerror(NtStatusToErrno(Status)));
            return 1;
        }
        Result = SetBasicInformation(
            &State,
            Argv[First + 2],
            &Information);
        CloseImage(&State);
        return Result;
    }

    if (strcmp(Argv[First], "--set-ea") == 0)
    {
        ULONGLONG ParsedFlags;
        char* End;

        if (First + 6 != Argc || ShowMetadata)
        {
            PrintUsage(Argv[0]);
            return 2;
        }
        errno = 0;
        ParsedFlags = strtoull(Argv[First + 3],
                               &End,
                               0);
        if (errno != 0 ||
            *Argv[First + 3] == '\0' ||
            *End != '\0' ||
            (ParsedFlags != 0 &&
             ParsedFlags != NTFS_EA_FLAG_NEED_EA))
        {
            fprintf(stderr,
                    "EA flags must be 0 or 0x80\n");
            return 2;
        }

        Status = OpenImageWritable(Argv[First + 1],
                                   &State);
        if (!NT_SUCCESS(Status))
        {
            fprintf(stderr,
                    "%s: ntfslib status 0x%08" PRIx32
                    " (%s)\n",
                    Argv[First + 1],
                    (uint32_t)Status,
                    strerror(NtStatusToErrno(Status)));
            return 1;
        }
        Result = UpdateExtendedAttribute(
            &State,
            Argv[First + 2],
            (UINT8)ParsedFlags,
            Argv[First + 4],
            Argv[First + 5],
            false);
        CloseImage(&State);
        return Result;
    }

    if (strcmp(Argv[First], "--set-security") == 0)
    {
        if (First + 4 != Argc || ShowMetadata)
        {
            PrintUsage(Argv[0]);
            return 2;
        }

        Status = OpenImageWritable(Argv[First + 1],
                                   &State);
        if (!NT_SUCCESS(Status))
        {
            fprintf(stderr,
                    "%s: ntfslib status 0x%08" PRIx32
                    " (%s)\n",
                    Argv[First + 1],
                    (uint32_t)Status,
                    strerror(NtStatusToErrno(Status)));
            return 1;
        }
        Result = SetSecurityFromHost(
            &State,
            Argv[First + 2],
            Argv[First + 3]);
        CloseImage(&State);
        return Result;
    }

    if (strcmp(Argv[First], "--set-reparse") == 0 ||
        strcmp(Argv[First], "--delete-reparse") == 0)
    {
        bool Delete =
            strcmp(Argv[First], "--delete-reparse") == 0;

        if (First + 4 != Argc || ShowMetadata)
        {
            PrintUsage(Argv[0]);
            return 2;
        }

        Status = OpenImageWritable(Argv[First + 1],
                                   &State);
        if (!NT_SUCCESS(Status))
        {
            fprintf(stderr,
                    "%s: ntfslib status 0x%08" PRIx32
                    " (%s)\n",
                    Argv[First + 1],
                    (uint32_t)Status,
                    strerror(NtStatusToErrno(Status)));
            return 1;
        }
        Result = UpdateReparsePointFromHost(
            &State,
            Argv[First + 2],
            Argv[First + 3],
            Delete);
        CloseImage(&State);
        return Result;
    }

    if (strcmp(Argv[First], "--remove-ea") == 0)
    {
        if (First + 4 != Argc || ShowMetadata)
        {
            PrintUsage(Argv[0]);
            return 2;
        }

        Status = OpenImageWritable(Argv[First + 1],
                                   &State);
        if (!NT_SUCCESS(Status))
        {
            fprintf(stderr,
                    "%s: ntfslib status 0x%08" PRIx32
                    " (%s)\n",
                    Argv[First + 1],
                    (uint32_t)Status,
                    strerror(NtStatusToErrno(Status)));
            return 1;
        }
        Result = UpdateExtendedAttribute(
            &State,
            Argv[First + 2],
            0,
            Argv[First + 3],
            NULL,
            true);
        CloseImage(&State);
        return Result;
    }

    if (strcmp(Argv[First],
               "--delete-external-backing") == 0)
    {
        if (First + 3 != Argc || ShowMetadata)
        {
            PrintUsage(Argv[0]);
            return 2;
        }

        Status = OpenImageWritable(Argv[First + 1],
                                   &State);
        if (!NT_SUCCESS(Status))
        {
            fprintf(stderr,
                    "%s: ntfslib status 0x%08" PRIx32
                    " (%s)\n",
                    Argv[First + 1],
                    (uint32_t)Status,
                    strerror(NtStatusToErrno(Status)));
            return 1;
        }
        Result = DeleteExternalBacking(
            &State,
            Argv[First + 2]);
        CloseImage(&State);
        return Result;
    }

    if (strcmp(Argv[First], "--write") == 0)
    {
        ULONGLONG Offset;
        char* End;

        if (First + 5 != Argc || ShowMetadata)
        {
            PrintUsage(Argv[0]);
            return 2;
        }
        errno = 0;
        Offset = strtoull(Argv[First + 3], &End, 0);
        if (errno != 0 || *Argv[First + 3] == '\0' || *End != '\0' ||
            Offset > INT64_MAX)
        {
            fprintf(stderr, "invalid write offset\n");
            return 2;
        }

        Status = OpenImageWritable(Argv[First + 1], &State);
        if (!NT_SUCCESS(Status))
        {
            fprintf(stderr,
                    "%s: ntfslib status 0x%08" PRIx32 " (%s)\n",
                    Argv[First + 1],
                    (uint32_t)Status,
                    strerror(NtStatusToErrno(Status)));
            return 1;
        }
        Result = WriteFileFromHost(&State,
                                   Argv[First + 2],
                                   Offset,
                                   Argv[First + 4]);
        CloseImage(&State);
        return Result;
    }

    if (strcmp(Argv[First], "--addfiles") == 0)
    {
        if (First + 3 != Argc || ShowMetadata)
        {
            PrintUsage(Argv[0]);
            return 2;
        }

        Status = OpenImageWritable(Argv[First + 1], &State);
        if (!NT_SUCCESS(Status))
        {
            fprintf(stderr,
                    "%s: ntfslib status 0x%08" PRIx32 " (%s)\n",
                    Argv[First + 1],
                    (uint32_t)Status,
                    strerror(NtStatusToErrno(Status)));
            return 1;
        }
        Result = AddFilesFromList(&State, Argv[First + 2]);
        CloseImage(&State);
        return Result;
    }

    if (strcmp(Argv[First], "--create-file") == 0 ||
        strcmp(Argv[First], "--create-dir") == 0)
    {
        bool IsDirectory =
            strcmp(Argv[First],
                   "--create-dir") == 0;
        ULONGLONG FileAttributes = 0;

        if (First + 3 > Argc ||
            First + 4 < Argc ||
            ShowMetadata ||
            (First + 4 == Argc &&
             !ParseUnsigned(Argv[First + 3],
                            MAXULONG,
                            &FileAttributes)))
        {
            PrintUsage(Argv[0]);
            return 2;
        }

        Status = OpenImageWritable(
            Argv[First + 1],
            &State);
        if (!NT_SUCCESS(Status))
        {
            fprintf(stderr,
                    "%s: ntfslib status 0x%08" PRIx32
                    " (%s)\n",
                    Argv[First + 1],
                    (uint32_t)Status,
                    strerror(NtStatusToErrno(Status)));
            return 1;
        }
        Result = CreateNode(
            &State,
            Argv[First + 2],
            IsDirectory,
            (ULONG)FileAttributes);
        CloseImage(&State);
        return Result;
    }

    if (strcmp(Argv[First], "--remove") == 0 ||
        strcmp(Argv[First], "--remove-dir") == 0)
    {
        bool IsDirectory =
            strcmp(Argv[First],
                   "--remove-dir") == 0;

        if (First + 3 != Argc || ShowMetadata)
        {
            PrintUsage(Argv[0]);
            return 2;
        }
        Status = OpenImageWritable(
            Argv[First + 1],
            &State);
        if (!NT_SUCCESS(Status))
        {
            fprintf(stderr,
                    "%s: ntfslib status 0x%08" PRIx32
                    " (%s)\n",
                    Argv[First + 1],
                    (uint32_t)Status,
                    strerror(NtStatusToErrno(Status)));
            return 1;
        }
        Result = RemoveNode(
            &State,
            Argv[First + 2],
            IsDirectory);
        CloseImage(&State);
        return Result;
    }

    if (strcmp(Argv[First], "--rename") == 0 ||
        strcmp(Argv[First], "--link") == 0)
    {
        bool HardLink =
            strcmp(Argv[First], "--link") == 0;

        if (First + 4 != Argc || ShowMetadata)
        {
            PrintUsage(Argv[0]);
            return 2;
        }
        Status = OpenImageWritable(
            Argv[First + 1],
            &State);
        if (!NT_SUCCESS(Status))
        {
            fprintf(stderr,
                    "%s: ntfslib status 0x%08" PRIx32
                    " (%s)\n",
                    Argv[First + 1],
                    (uint32_t)Status,
                    strerror(NtStatusToErrno(Status)));
            return 1;
        }
        Result = RenameNode(
            &State,
            Argv[First + 2],
            Argv[First + 3],
            HardLink);
        CloseImage(&State);
        return Result;
    }

    if (strcmp(Argv[First], "--set-sparse") == 0)
    {
        bool SetSparse;

        if (First + 4 != Argc || ShowMetadata ||
            (strcmp(Argv[First + 3], "0") != 0 &&
             strcmp(Argv[First + 3], "1") != 0))
        {
            PrintUsage(Argv[0]);
            return 2;
        }
        SetSparse =
            strcmp(Argv[First + 3], "1") == 0;

        Status = OpenImageWritable(
            Argv[First + 1],
            &State);
        if (!NT_SUCCESS(Status))
        {
            fprintf(stderr,
                    "%s: ntfslib status 0x%08" PRIx32
                    " (%s)\n",
                    Argv[First + 1],
                    (uint32_t)Status,
                    strerror(NtStatusToErrno(Status)));
            return 1;
        }
        Result = SetSparseState(
            &State,
            Argv[First + 2],
            SetSparse);
        CloseImage(&State);
        return Result;
    }

    if (strcmp(Argv[First], "--zero-data") == 0 ||
        strcmp(Argv[First],
               "--allocated-ranges") == 0)
    {
        bool Query =
            strcmp(Argv[First],
                   "--allocated-ranges") == 0;
        ULONGLONG Start;
        ULONGLONG EndOrLength;

        if (First + 5 != Argc ||
            (!Query && ShowMetadata) ||
            !ParseUnsigned(Argv[First + 3],
                           INT64_MAX,
                           &Start) ||
            !ParseUnsigned(Argv[First + 4],
                           INT64_MAX,
                           &EndOrLength))
        {
            PrintUsage(Argv[0]);
            return 2;
        }

        Status = Query
            ? OpenImage(Argv[First + 1],
                        ShowMetadata,
                        &State)
            : OpenImageWritable(Argv[First + 1],
                                &State);
        if (!NT_SUCCESS(Status))
        {
            fprintf(stderr,
                    "%s: ntfslib status 0x%08" PRIx32
                    " (%s)\n",
                    Argv[First + 1],
                    (uint32_t)Status,
                    strerror(NtStatusToErrno(Status)));
            return 1;
        }
        Result = Query
            ? PrintAllocatedRanges(
                &State,
                Argv[First + 2],
                Start,
                EndOrLength)
            : SetZeroDataRange(
                &State,
                Argv[First + 2],
                Start,
                EndOrLength);
        CloseImage(&State);
        return Result;
    }

    if (strcmp(Argv[First],
               "--retrieval-pointers") == 0)
    {
        ULONGLONG StartingVcn;

        if (First + 4 != Argc ||
            !ParseUnsigned(Argv[First + 3],
                           INT64_MAX,
                           &StartingVcn))
        {
            PrintUsage(Argv[0]);
            return 2;
        }

        Status = OpenImage(Argv[First + 1],
                           ShowMetadata,
                           &State);
        if (!NT_SUCCESS(Status))
        {
            fprintf(stderr,
                    "%s: ntfslib status 0x%08" PRIx32
                    " (%s)\n",
                    Argv[First + 1],
                    (uint32_t)Status,
                    strerror(NtStatusToErrno(Status)));
            return 1;
        }
        Result = PrintRetrievalPointers(
            &State,
            Argv[First + 2],
            StartingVcn);
        CloseImage(&State);
        return Result;
    }

    if (strcmp(Argv[First],
               "--volume-bitmap") == 0)
    {
        ULONGLONG StartingLcn;
        ULONGLONG ByteCount;

        if (First + 4 != Argc ||
            !ParseUnsigned(Argv[First + 2],
                           INT64_MAX,
                           &StartingLcn) ||
            !ParseUnsigned(Argv[First + 3],
                           MAXULONG,
                           &ByteCount))
        {
            PrintUsage(Argv[0]);
            return 2;
        }

        Status = OpenImage(Argv[First + 1],
                           ShowMetadata,
                           &State);
        if (!NT_SUCCESS(Status))
        {
            fprintf(stderr,
                    "%s: ntfslib status 0x%08" PRIx32
                    " (%s)\n",
                    Argv[First + 1],
                    (uint32_t)Status,
                    strerror(NtStatusToErrno(Status)));
            return 1;
        }
        Result = PrintVolumeBitmap(
            &State,
            StartingLcn,
            (ULONG)ByteCount);
        CloseImage(&State);
        return Result;
    }

    if (strcmp(Argv[First],
               "--volume-data") == 0)
    {
        if (First + 2 != Argc)
        {
            PrintUsage(Argv[0]);
            return 2;
        }

        Status = OpenImage(Argv[First + 1],
                           ShowMetadata,
                           &State);
        if (!NT_SUCCESS(Status))
        {
            fprintf(stderr,
                    "%s: ntfslib status 0x%08" PRIx32
                    " (%s)\n",
                    Argv[First + 1],
                    (uint32_t)Status,
                    strerror(NtStatusToErrno(Status)));
            return 1;
        }
        Result = PrintVolumeData(&State);
        CloseImage(&State);
        return Result;
    }

    if (strcmp(Argv[First],
               "--mft-record") == 0)
    {
        ULONGLONG FileReference;

        if (First + 3 != Argc ||
            !ParseUnsigned(Argv[First + 2],
                           ~(ULONGLONG)0,
                           &FileReference))
        {
            PrintUsage(Argv[0]);
            return 2;
        }

        Status = OpenImage(Argv[First + 1],
                           ShowMetadata,
                           &State);
        if (!NT_SUCCESS(Status))
        {
            fprintf(stderr,
                    "%s: ntfslib status 0x%08" PRIx32
                    " (%s)\n",
                    Argv[First + 1],
                    (uint32_t)Status,
                    strerror(NtStatusToErrno(Status)));
            return 1;
        }
        Result = DumpMftRecord(&State,
                               FileReference);
        CloseImage(&State);
        return Result;
    }

    if (strcmp(Argv[First],
               "--streams") == 0)
    {
        if (First + 3 != Argc)
        {
            PrintUsage(Argv[0]);
            return 2;
        }

        Status = OpenImage(Argv[First + 1],
                           ShowMetadata,
                           &State);
        if (!NT_SUCCESS(Status))
        {
            fprintf(stderr,
                    "%s: ntfslib status 0x%08" PRIx32
                    " (%s)\n",
                    Argv[First + 1],
                    (uint32_t)Status,
                    strerror(NtStatusToErrno(Status)));
            return 1;
        }
        Result = PrintDataStreams(
            &State,
            Argv[First + 2]);
        CloseImage(&State);
        return Result;
    }

    if (strcmp(Argv[First], "--truncate") == 0 ||
        strcmp(Argv[First], "--allocate") == 0)
    {
        bool AllocationOnly =
            strcmp(Argv[First], "--allocate") == 0;
        ULONGLONG NewSize;
        char* End;

        if (First + 4 != Argc || ShowMetadata)
        {
            PrintUsage(Argv[0]);
            return 2;
        }
        errno = 0;
        NewSize = strtoull(Argv[First + 3],
                           &End,
                           0);
        if (errno != 0 ||
            *Argv[First + 3] == '\0' ||
            *End != '\0' ||
            NewSize > INT64_MAX)
        {
            fprintf(stderr, "invalid file size\n");
            return 2;
        }

        Status = OpenImageWritable(Argv[First + 1],
                                   &State);
        if (!NT_SUCCESS(Status))
        {
            fprintf(stderr,
                    "%s: ntfslib status 0x%08" PRIx32
                    " (%s)\n",
                    Argv[First + 1],
                    (uint32_t)Status,
                    strerror(NtStatusToErrno(Status)));
            return 1;
        }
        Result = SetFileSize(&State,
                             Argv[First + 2],
                             NewSize,
                             AllocationOnly);
        CloseImage(&State);
        return Result;
    }

    {
        bool WritableMount = false;

        if (First < Argc &&
            strcmp(Argv[First], "--writable") == 0)
        {
            WritableMount = true;
            First++;
        }
        if (First + 2 > Argc)
        {
            PrintUsage(Argv[0]);
            return 2;
        }

#if NTFSLIB_HAVE_FUSE
        Status = OpenImageWithMode(Argv[First],
                                   ShowMetadata,
                                   WritableMount,
                                   &State);
        if (!NT_SUCCESS(Status))
        {
            fprintf(stderr,
                    "%s: ntfslib status 0x%08" PRIx32 " (%s)\n",
                    Argv[First],
                    (uint32_t)Status,
                    strerror(NtStatusToErrno(Status)));
            return 1;
        }
        State.Writable = WritableMount;

        /*
         * Remove IMAGE from the FUSE argv, retain MOUNTPOINT/options, and
         * force single-threaded operation because the shared core has no
         * internal locking. Mounts stay read-only unless --writable was
         * given; writable mounts publish every mutation in a readable
         * order but are not crash-atomic until LFS transactions exist.
         */
        {
            std::vector<char*> FuseArguments;
            static char SingleThreaded[] = "-s";
            static char ReadOnly[] = "-oro";
            static char KernelCache[] = "-okernel_cache";
            static char AttributeTimeout[] = "-oattr_timeout=60";
            static char EntryTimeout[] = "-oentry_timeout=60";
            static char NegativeTimeout[] = "-onegative_timeout=5";

            FuseArguments.push_back(Argv[0]);
            for (int Index = First + 1; Index < Argc; Index++)
                FuseArguments.push_back(Argv[Index]);
            FuseArguments.push_back(SingleThreaded);
            if (!WritableMount)
                FuseArguments.push_back(ReadOnly);
            FuseArguments.push_back(KernelCache);
            FuseArguments.push_back(AttributeTimeout);
            FuseArguments.push_back(EntryTimeout);
            FuseArguments.push_back(NegativeTimeout);

            Result = MountImage((int)FuseArguments.size(),
                                FuseArguments.data(),
                                &State);
        }
        CloseImage(&State);
        return Result;
#else
        fprintf(stderr,
                "This build has no libfuse3 development headers. Install "
                "libfuse3-dev and rebuild; image inspection commands work "
                "now.\n");
        return 2;
#endif
    }
}
