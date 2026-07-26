#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <chrono>
#include <string>
#include <vector>

static double
ElapsedSeconds(std::chrono::steady_clock::time_point Start)
{
    return std::chrono::duration<double>(
        std::chrono::steady_clock::now() - Start).count();
}

static bool
ParseIterations(const char* Text, unsigned* Value)
{
    char* End = NULL;
    unsigned long Parsed;

    errno = 0;
    Parsed = strtoul(Text, &End, 10);
    if (errno != 0 || !End || *End != '\0' ||
        Parsed == 0 || Parsed > UINT32_MAX)
    {
        return false;
    }
    *Value = (unsigned)Parsed;
    return true;
}

static int
CountDirectory(const char* Path, uint64_t* EntryCount)
{
    uint64_t Entries = 0;
    DIR* Directory = opendir(Path);
    if (!Directory)
    {
        fprintf(stderr, "opendir(%s): %s\n", Path, strerror(errno));
        return 1;
    }

    errno = 0;
    while (struct dirent* Entry = readdir(Directory))
    {
        if (strcmp(Entry->d_name, ".") != 0 &&
            strcmp(Entry->d_name, "..") != 0)
        {
            Entries++;
        }
    }
    int ReadError = errno;
    if (closedir(Directory) != 0)
    {
        fprintf(stderr, "closedir(%s): %s\n", Path, strerror(errno));
        return 1;
    }
    if (ReadError != 0)
    {
        fprintf(stderr, "readdir(%s): %s\n", Path, strerror(ReadError));
        return 1;
    }

    *EntryCount = Entries;
    return 0;
}

static int
BenchmarkDirectory(const char* Path,
                   unsigned Iterations,
                   double* Seconds,
                   uint64_t* EntryCount)
{
    uint64_t ExpectedEntries;

    /* Populate each driver's directory cache before starting the clock. */
    if (CountDirectory(Path, &ExpectedEntries) != 0)
        return 1;

    auto Start = std::chrono::steady_clock::now();
    for (unsigned Iteration = 0; Iteration < Iterations; Iteration++)
    {
        uint64_t Entries;

        if (CountDirectory(Path, &Entries) != 0)
            return 1;
        if (Entries != ExpectedEntries)
        {
            fprintf(stderr,
                    "%s changed during benchmark (%llu -> %llu entries)\n",
                    Path,
                    (unsigned long long)ExpectedEntries,
                    (unsigned long long)Entries);
            return 1;
        }
    }

    *Seconds = ElapsedSeconds(Start);
    *EntryCount = ExpectedEntries;
    return 0;
}

static int
ReadFile(const char* Path,
         int ExtraOpenFlags,
         unsigned char* Buffer,
         size_t BufferSize,
         uint64_t* BytesRead)
{
    int File = open(Path, O_RDONLY | O_CLOEXEC | ExtraOpenFlags);
    if (File < 0)
    {
        fprintf(stderr, "open(%s): %s\n", Path, strerror(errno));
        return 1;
    }

    for (;;)
    {
        ssize_t Count = read(File, Buffer, BufferSize);
        if (Count > 0)
        {
            *BytesRead += (uint64_t)Count;
            continue;
        }
        if (Count == 0)
            break;
        if (errno == EINTR)
            continue;

        fprintf(stderr, "read(%s): %s\n", Path, strerror(errno));
        close(File);
        return 1;
    }

    if (close(File) != 0)
    {
        fprintf(stderr, "close(%s): %s\n", Path, strerror(errno));
        return 1;
    }
    return 0;
}

static int
BenchmarkRead(const char* Path,
              int ExtraOpenFlags,
              unsigned Iterations,
              double* Seconds,
              uint64_t* BytesRead)
{
    const size_t BufferSize = 1024 * 1024;
    void* Allocation = NULL;
    uint64_t WarmupBytes = 0;
    uint64_t TotalBytes = 0;

    if (posix_memalign(&Allocation, 4096, BufferSize) != 0)
    {
        fprintf(stderr, "failed to allocate aligned read buffer\n");
        return 1;
    }

    /*
     * The buffered row is intentionally a hot page-cache measurement. The
     * direct-I/O row bypasses that cache and exercises the FUSE read path.
     */
    if (ReadFile(Path,
                 ExtraOpenFlags,
                 (unsigned char*)Allocation,
                 BufferSize,
                 &WarmupBytes) != 0)
    {
        free(Allocation);
        return 1;
    }

    auto Start = std::chrono::steady_clock::now();
    for (unsigned Iteration = 0; Iteration < Iterations; Iteration++)
    {
        if (ReadFile(Path,
                     ExtraOpenFlags,
                     (unsigned char*)Allocation,
                     BufferSize,
                     &TotalBytes) != 0)
        {
            free(Allocation);
            return 1;
        }
    }
    *Seconds = ElapsedSeconds(Start);
    *BytesRead = TotalBytes;
    free(Allocation);
    return 0;
}

/*
 * Amortized per-operation comparison: one process opens, fully reads, and
 * closes every small file each pass, so process launch and mount setup do
 * not dominate the way they do in fresh-process rows.
 */
static int
BenchmarkSmallFiles(const char* DirectoryPath,
                    unsigned Iterations,
                    double* Seconds,
                    uint64_t* Operations)
{
    std::vector<std::string> Files;
    unsigned char Buffer[65536];
    uint64_t Bytes = 0;
    DIR* Directory = opendir(DirectoryPath);

    if (!Directory)
    {
        fprintf(stderr,
                "opendir(%s): %s\n",
                DirectoryPath,
                strerror(errno));
        return 1;
    }
    while (struct dirent* Entry = readdir(Directory))
    {
        if (strncmp(Entry->d_name, "small-", 6) == 0)
        {
            Files.push_back(std::string(DirectoryPath) + "/" +
                            Entry->d_name);
        }
    }
    closedir(Directory);
    if (Files.empty())
    {
        fprintf(stderr,
                "%s has no small-* files\n",
                DirectoryPath);
        return 1;
    }

    for (size_t Index = 0; Index < Files.size(); Index++)
    {
        if (ReadFile(Files[Index].c_str(),
                     0,
                     Buffer,
                     sizeof(Buffer),
                     &Bytes) != 0)
        {
            return 1;
        }
    }

    auto Start = std::chrono::steady_clock::now();
    for (unsigned Iteration = 0; Iteration < Iterations; Iteration++)
    {
        for (size_t Index = 0; Index < Files.size(); Index++)
        {
            if (ReadFile(Files[Index].c_str(),
                         0,
                         Buffer,
                         sizeof(Buffer),
                         &Bytes) != 0)
            {
                return 1;
            }
        }
    }
    *Seconds = ElapsedSeconds(Start);
    *Operations = (uint64_t)Iterations * Files.size();
    return 0;
}

int
main(int Argc, char** Argv)
{
    unsigned ListIterations;
    unsigned ReadIterations;
    unsigned SweepIterations = 10;
    double ListSeconds;
    double CachedReadSeconds;
    double DirectReadSeconds;
    double SweepSeconds;
    uint64_t Entries;
    uint64_t CachedBytes;
    uint64_t DirectBytes;
    uint64_t SweepOperations;

    if ((Argc != 6 && Argc != 7) ||
        !ParseIterations(Argv[4], &ListIterations) ||
        !ParseIterations(Argv[5], &ReadIterations) ||
        (Argc == 7 && !ParseIterations(Argv[6], &SweepIterations)))
    {
        fprintf(stderr,
                "Usage: %s LABEL DIRECTORY FILE "
                "LIST_ITERATIONS READ_ITERATIONS [SWEEP_ITERATIONS]\n",
                Argv[0]);
        return 2;
    }

    if (BenchmarkDirectory(Argv[2],
                           ListIterations,
                           &ListSeconds,
                           &Entries) != 0 ||
        BenchmarkRead(Argv[3],
                      0,
                      ReadIterations,
                      &CachedReadSeconds,
                      &CachedBytes) != 0 ||
        BenchmarkRead(Argv[3],
                      O_DIRECT,
                      ReadIterations,
                      &DirectReadSeconds,
                      &DirectBytes) != 0 ||
        BenchmarkSmallFiles(Argv[2],
                            SweepIterations,
                            &SweepSeconds,
                            &SweepOperations) != 0)
    {
        return 1;
    }

    printf("%s,list,%.6f,%.2f,%llu,ops/s\n",
           Argv[1],
           ListSeconds,
           ListIterations / ListSeconds,
           (unsigned long long)Entries);
    printf("%s,cached_read,%.6f,%.2f,%llu,MiB/s\n",
           Argv[1],
           CachedReadSeconds,
           (CachedBytes / (1024.0 * 1024.0)) / CachedReadSeconds,
           (unsigned long long)CachedBytes);
    printf("%s,direct_read,%.6f,%.2f,%llu,MiB/s\n",
           Argv[1],
           DirectReadSeconds,
           (DirectBytes / (1024.0 * 1024.0)) / DirectReadSeconds,
           (unsigned long long)DirectBytes);
    printf("%s,small_read,%.6f,%.2f,%llu,ops/s\n",
           Argv[1],
           SweepSeconds,
           SweepOperations / SweepSeconds,
           (unsigned long long)SweepOperations);
    return 0;
}
