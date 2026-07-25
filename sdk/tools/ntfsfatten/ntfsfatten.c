/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS NTFS Image Creator
 * PURPOSE:         Format and populate an NTFS image using NTFS-3G
 */

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#ifndef S_ISDIR
#define S_ISDIR(mode) (((mode) & _S_IFMT) == _S_IFDIR)
#endif
#else
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#endif

#include "ntfs3g_ros.h"

#define COPY_BUFFER_SIZE 65536
#define LIST_LINE_SIZE 16384

int ntfs3g_mkntfs_main(int argc, char **argv);

static unsigned char CopyBuffer[COPY_BUFFER_SIZE];

static void
PrintHelp(const char *Program)
{
    fprintf(stderr,
            "Syntax: %s image_file -format <sectors> ntfs [label] "
            "[-addfiles <list file>]\n",
            Program);
}

static char *
DuplicateString(const char *String)
{
    size_t Length = strlen(String) + 1;
    char *Copy = malloc(Length);

    if (Copy)
        memcpy(Copy, String, Length);
    return Copy;
}

static char *
NormalizeImagePath(const char *Path)
{
    size_t Length;
    size_t Index;
    char *Normalized;

    while (*Path == '/' || *Path == '\\')
        Path++;
    Length = strlen(Path);
    Normalized = malloc(Length + 2);
    if (!Normalized)
        return NULL;

    Normalized[0] = '/';
    for (Index = 0; Index < Length; Index++)
        Normalized[Index + 1] = Path[Index] == '\\' ? '/' : Path[Index];
    Normalized[Length + 1] = '\0';

    while (Length > 0 && Normalized[Length] == '/')
        Normalized[Length--] = '\0';
    return Normalized;
}

static char *
TrimWhitespace(char *Text)
{
    char *End;

    while (*Text && isspace((unsigned char)*Text))
        Text++;
    End = Text + strlen(Text);
    while (End > Text && isspace((unsigned char)End[-1]))
        *--End = '\0';
    return Text;
}

static int
ResizeImage(const char *Path,
            uint64_t Size)
{
    int Descriptor;

#ifdef _WIN32
    Descriptor = _open(Path, _O_BINARY | _O_CREAT | _O_RDWR | _O_TRUNC,
                       _S_IREAD | _S_IWRITE);
    if (Descriptor < 0)
        return -1;
    if (_chsize_s(Descriptor, Size)) {
        int Error = errno;
        _close(Descriptor);
        errno = Error;
        return -1;
    }
    return _close(Descriptor);
#else
    Descriptor = open(Path, O_CREAT | O_RDWR | O_TRUNC, 0666);
    if (Descriptor < 0)
        return -1;
    if (ftruncate(Descriptor, (off_t)Size)) {
        int Error = errno;
        close(Descriptor);
        errno = Error;
        return -1;
    }
    return close(Descriptor);
#endif
}

static int
FormatImage(const char *Path,
            uint64_t Sectors,
            const char *Label)
{
    char SectorCount[32];
    char *Arguments[10];
    int ArgumentCount = 0;

    if (!Sectors || Sectors > UINT64_MAX / 512) {
        errno = EINVAL;
        return -1;
    }
    if (ResizeImage(Path, Sectors * 512))
        return -1;

    snprintf(SectorCount, sizeof(SectorCount), "%" PRIu64, Sectors);
    Arguments[ArgumentCount++] = "mkntfs";
    Arguments[ArgumentCount++] = "-F";
    Arguments[ArgumentCount++] = "-Q";
    Arguments[ArgumentCount++] = "-T";
    if (Label && *Label) {
        Arguments[ArgumentCount++] = "-L";
        Arguments[ArgumentCount++] = (char *)Label;
    }
    Arguments[ArgumentCount++] = (char *)Path;
    Arguments[ArgumentCount++] = SectorCount;
    Arguments[ArgumentCount] = NULL;

    return ntfs3g_mkntfs_main(ArgumentCount, Arguments) ? -1 : 0;
}

static int
CreateImageDirectoryIfMissing(NTFS3G_ROS_VOLUME *Volume,
                              const char *Path)
{
    NTFS3G_ROS_FILE *File;
    uint32_t Attributes;
    int Result;

    if (!strcmp(Path, "/"))
        return 0;

    Result = Ntfs3gRosOpenFile(Volume, Path, &File);
    if (!Result) {
        Attributes = Ntfs3gRosGetFileAttributes(File);
        Ntfs3gRosCloseFile(File);
        if (Attributes & NTFS3G_ROS_FILE_DIRECTORY)
            return 0;
        errno = ENOTDIR;
        return -1;
    }
    if (Result != -ENOENT) {
        errno = -Result;
        return -1;
    }

    Result = Ntfs3gRosCreateDirectory(Volume, Path);
    if (!Result || Result == -EEXIST)
        return 0;
    errno = -Result;
    return -1;
}

static int
EnsureImageDirectory(NTFS3G_ROS_VOLUME *Volume,
                     const char *Path)
{
    char *Normalized;
    char *Separator;
    char *Cursor;
    int Result = -1;

    Normalized = NormalizeImagePath(Path);
    if (!Normalized)
        return -1;
    if (!strcmp(Normalized, "/")) {
        free(Normalized);
        return 0;
    }

    Cursor = Normalized + 1;
    while ((Separator = strchr(Cursor, '/')) != NULL) {
        *Separator = '\0';
        if (CreateImageDirectoryIfMissing(Volume, Normalized))
            goto done;
        *Separator = '/';
        Cursor = Separator + 1;
    }
    Result = CreateImageDirectoryIfMissing(Volume, Normalized);

done:
    free(Normalized);
    return Result;
}

static int
EnsureImageParentDirectories(NTFS3G_ROS_VOLUME *Volume,
                             const char *Path)
{
    char *Normalized;
    char *Separator;
    int Result = 0;

    Normalized = NormalizeImagePath(Path);
    if (!Normalized)
        return -1;
    Separator = strrchr(Normalized, '/');
    if (Separator != Normalized) {
        *Separator = '\0';
        Result = EnsureImageDirectory(Volume, Normalized);
    }
    free(Normalized);
    return Result;
}

static int
HostPathIsDirectory(const char *Path)
{
    struct stat Status;

    return !stat(Path, &Status) && S_ISDIR(Status.st_mode);
}

static char *
JoinPath(const char *Left,
         const char *Right,
         char Separator)
{
    size_t LeftLength = strlen(Left);
    size_t RightLength = strlen(Right);
    int AddSeparator = LeftLength && Left[LeftLength - 1] != '/' &&
                       Left[LeftLength - 1] != '\\';
    char *Path = malloc(LeftLength + RightLength +
                        (AddSeparator ? 2 : 1));

    if (!Path)
        return NULL;
    memcpy(Path, Left, LeftLength);
    if (AddSeparator)
        Path[LeftLength++] = Separator;
    memcpy(Path + LeftLength, Right, RightLength + 1);
    return Path;
}

static int
CopyHostFile(NTFS3G_ROS_VOLUME *Volume,
             const char *HostPath,
             const char *ImagePath)
{
    NTFS3G_ROS_FILE *Destination = NULL;
    char *Normalized = NULL;
    FILE *Source = NULL;
    size_t ReadLength;
    int Result = -1;

    Source = fopen(HostPath, "rb");
    if (!Source)
        goto done;
    if (EnsureImageParentDirectories(Volume, ImagePath))
        goto done;

    Normalized = NormalizeImagePath(ImagePath);
    if (!Normalized)
        goto done;
    if (Ntfs3gRosCreateFile(Volume, Normalized, 1, &Destination))
        goto done;

    while ((ReadLength = fread(CopyBuffer, 1, sizeof(CopyBuffer), Source)) != 0) {
        size_t Offset = 0;

        while (Offset < ReadLength) {
            size_t Written;
            int Error = Ntfs3gRosWriteFile(Destination,
                                           CopyBuffer + Offset,
                                           ReadLength - Offset,
                                           &Written);
            if (Error || !Written) {
                if (Error)
                    errno = -Error;
                else
                    errno = EIO;
                goto done;
            }
            Offset += Written;
        }
    }
    if (ferror(Source))
        goto done;
    Result = 0;

done:
    if (Destination && Ntfs3gRosCloseFile(Destination) && !Result)
        Result = -1;
    if (Source)
        fclose(Source);
    free(Normalized);
    if (Result)
        fprintf(stderr, "Error: Could not copy '%s' to '%s' (%s).\n",
                HostPath, ImagePath, strerror(errno));
    return Result;
}

static int
AddHostPath(NTFS3G_ROS_VOLUME *Volume,
            const char *HostPath,
            const char *ImagePath)
{
    if (!HostPathIsDirectory(HostPath))
        return CopyHostFile(Volume, HostPath, ImagePath);

    if (EnsureImageDirectory(Volume, ImagePath)) {
        fprintf(stderr, "Error: Could not create directory '%s' (%s).\n",
                ImagePath, strerror(errno));
        return -1;
    }

#ifdef _WIN32
    {
        struct _finddata_t FindData;
        intptr_t Handle;
        char *Pattern;
        int Result = 0;

        Pattern = JoinPath(HostPath, "*", '\\');
        if (!Pattern)
            return -1;
        Handle = _findfirst(Pattern, &FindData);
        free(Pattern);
        if (Handle == -1)
            return errno == ENOENT ? 0 : -1;

        do {
            char *ChildHostPath;
            char *ChildImagePath;

            if (!strcmp(FindData.name, ".") ||
                !strcmp(FindData.name, ".."))
                continue;
            ChildHostPath = JoinPath(HostPath, FindData.name, '\\');
            ChildImagePath = JoinPath(ImagePath, FindData.name, '/');
            if (!ChildHostPath || !ChildImagePath ||
                AddHostPath(Volume, ChildHostPath, ChildImagePath)) {
                free(ChildHostPath);
                free(ChildImagePath);
                Result = -1;
                break;
            }
            free(ChildHostPath);
            free(ChildImagePath);
        } while (_findnext(Handle, &FindData) == 0);
        _findclose(Handle);
        return Result;
    }
#else
    {
        DIR *Directory;
        struct dirent *Entry;
        int Result = 0;

        Directory = opendir(HostPath);
        if (!Directory)
            return -1;
        while ((Entry = readdir(Directory)) != NULL) {
            char *ChildHostPath;
            char *ChildImagePath;

            if (!strcmp(Entry->d_name, ".") ||
                !strcmp(Entry->d_name, ".."))
                continue;
            ChildHostPath = JoinPath(HostPath, Entry->d_name, '/');
            ChildImagePath = JoinPath(ImagePath, Entry->d_name, '/');
            if (!ChildHostPath || !ChildImagePath ||
                AddHostPath(Volume, ChildHostPath, ChildImagePath)) {
                free(ChildHostPath);
                free(ChildImagePath);
                Result = -1;
                break;
            }
            free(ChildHostPath);
            free(ChildImagePath);
        }
        closedir(Directory);
        return Result;
    }
#endif
}

static int
AddFilesFromList(NTFS3G_ROS_VOLUME *Volume,
                 const char *ListPath)
{
    FILE *List;
    char Line[LIST_LINE_SIZE];
    unsigned int LineNumber = 0;
    int Result = -1;

    List = fopen(ListPath, "rb");
    if (!List)
        return -1;

    while (fgets(Line, sizeof(Line), List)) {
        char *Entry;
        char *Separator;
        size_t Length = strlen(Line);

        LineNumber++;
        if (Length && Line[Length - 1] != '\n' && !feof(List)) {
            fprintf(stderr, "Error: List entry %u in '%s' is too long.\n",
                    LineNumber, ListPath);
            goto done;
        }
        Entry = TrimWhitespace(Line);
        if (!*Entry || *Entry == '#')
            continue;

        Separator = strchr(Entry, '=');
        if (!Separator) {
            if (EnsureImageDirectory(Volume, Entry)) {
                fprintf(stderr,
                        "Error: Could not create '%s' from list '%s' (%s).\n",
                        Entry, ListPath, strerror(errno));
                goto done;
            }
            continue;
        }

        *Separator = '\0';
        Entry = TrimWhitespace(Entry);
        Separator = TrimWhitespace(Separator + 1);
        if (!*Entry || !*Separator) {
            fprintf(stderr, "Error: Invalid list entry %u in '%s'.\n",
                    LineNumber, ListPath);
            goto done;
        }
        if (AddHostPath(Volume, Separator, Entry))
            goto done;
    }
    if (ferror(List))
        goto done;
    Result = 0;

done:
    fclose(List);
    return Result;
}

int
main(int argc,
     char **argv)
{
    NTFS3G_ROS_VOLUME *Volume = NULL;
    const char *ImagePath;
    const char *Label = NULL;
    const char *ListPath = NULL;
    uint64_t Sectors = 0;
    int Index;
    int Result = 1;

    if (argc < 2) {
        PrintHelp(argv[0]);
        return 1;
    }
    ImagePath = argv[1];

    for (Index = 2; Index < argc; Index++) {
        if (!strcmp(argv[Index], "-format")) {
            char *End;

            if (Index + 2 >= argc || strcmp(argv[Index + 2], "ntfs")) {
                PrintHelp(argv[0]);
                return 1;
            }
            errno = 0;
            Sectors = strtoull(argv[Index + 1], &End, 10);
            if (errno || End == argv[Index + 1] || *End || !Sectors) {
                fprintf(stderr, "Error: Invalid sector count '%s'.\n",
                        argv[Index + 1]);
                return 1;
            }
            Index += 2;
            if (Index + 1 < argc && argv[Index + 1][0] != '-')
                Label = argv[++Index];
        } else if (!strcmp(argv[Index], "-addfiles")) {
            if (++Index >= argc) {
                PrintHelp(argv[0]);
                return 1;
            }
            ListPath = argv[Index];
        } else {
            fprintf(stderr, "Error: Unknown command '%s'.\n", argv[Index]);
            PrintHelp(argv[0]);
            return 1;
        }
    }

    if (!Sectors) {
        fprintf(stderr, "Error: The -format command is required.\n");
        return 1;
    }
    if (FormatImage(ImagePath, Sectors, Label)) {
        fprintf(stderr, "Error: Could not format '%s' as NTFS (%s).\n",
                ImagePath, strerror(errno));
        return 1;
    }
    if (!ListPath)
        return 0;

    if (Ntfs3gRosMountPath(ImagePath, 0, &Volume)) {
        fprintf(stderr, "Error: Could not mount '%s' read-write (%s).\n",
                ImagePath, strerror(errno));
        return 1;
    }
    if (AddFilesFromList(Volume, ListPath))
        goto done;
    if (Ntfs3gRosFlushVolume(Volume)) {
        fprintf(stderr, "Error: Could not flush '%s' (%s).\n",
                ImagePath, strerror(errno));
        goto done;
    }
    Result = 0;

done:
    if (Ntfs3gRosUnmount(Volume) && !Result)
        Result = 1;
    return Result;
}
