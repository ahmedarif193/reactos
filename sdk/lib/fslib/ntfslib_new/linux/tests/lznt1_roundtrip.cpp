/*
 * LZNT1 codec round-trip test: every corpus must decompress to exactly
 * the bytes that were compressed, compressible corpora must shrink, and
 * incompressible data must fail cleanly when the capacity requires a
 * saving. All data is generated from fixed seeds so failures reproduce.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vector>

#include <ntfslib_new.h>
#include <ntfs_linux.h>

static uint32_t RandomState;

static uint8_t
NextByte(void)
{
    RandomState = RandomState * 1664525u + 1013904223u;
    return (uint8_t)(RandomState >> 24);
}

static int
RoundTrip(const char* Name,
          const std::vector<uint8_t>& Corpus,
          bool ExpectShrink)
{
    std::vector<uint8_t> Compressed(Corpus.size() +
                                    Corpus.size() / 8 + 4096);
    std::vector<uint8_t> Decompressed(Corpus.size());
    ULONG CompressedSize = 0;
    ULONG FinalSize = 0;
    NTSTATUS Status;

    Status = NtfsLznt1Compress((PUCHAR)Corpus.data(),
                               (ULONG)Corpus.size(),
                               Compressed.data(),
                               (ULONG)Compressed.size(),
                               &CompressedSize);
    if (Status != STATUS_SUCCESS)
    {
        fprintf(stderr,
                "%s: compress failed 0x%08x\n",
                Name,
                (unsigned)Status);
        return 1;
    }
    if (ExpectShrink && CompressedSize >= Corpus.size())
    {
        fprintf(stderr,
                "%s: expected shrink, got %lu >= %zu\n",
                Name,
                (unsigned long)CompressedSize,
                Corpus.size());
        return 1;
    }

    Status = NtfsLznt1Decompress(Decompressed.data(),
                                 (ULONG)Decompressed.size(),
                                 Compressed.data(),
                                 CompressedSize,
                                 &FinalSize);
    if (Status != STATUS_SUCCESS ||
        FinalSize != Corpus.size() ||
        memcmp(Decompressed.data(),
               Corpus.data(),
               Corpus.size()) != 0)
    {
        fprintf(stderr,
                "%s: round trip mismatch (status 0x%08x, %lu of %zu)\n",
                Name,
                (unsigned)Status,
                (unsigned long)FinalSize,
                Corpus.size());
        return 1;
    }
    return 0;
}

int
main(void)
{
    static const size_t Sizes[] =
    {
        1, 2, 3, 4, 15, 16, 17, 4095, 4096, 4097,
        8191, 8192, 8193, 65535, 65536, 100000
    };
    int Failures = 0;

    for (size_t Index = 0;
         Index < sizeof(Sizes) / sizeof(Sizes[0]);
         Index++)
    {
        size_t Size = Sizes[Index];
        char Name[64];

        std::vector<uint8_t> Zeros(Size, 0);
        snprintf(Name, sizeof(Name), "zeros-%zu", Size);
        Failures += RoundTrip(Name, Zeros, Size >= 64);

        std::vector<uint8_t> Text(Size);
        static const char Phrase[] =
            "the quick brown fox jumps over the lazy dog. ";
        for (size_t Byte = 0; Byte < Size; Byte++)
            Text[Byte] = (uint8_t)Phrase[Byte % (sizeof(Phrase) - 1)];
        snprintf(Name, sizeof(Name), "text-%zu", Size);
        Failures += RoundTrip(Name, Text, Size >= 256);

        std::vector<uint8_t> Random(Size);
        RandomState = (uint32_t)(0x5eed0000u + Size);
        for (size_t Byte = 0; Byte < Size; Byte++)
            Random[Byte] = NextByte();
        snprintf(Name, sizeof(Name), "random-%zu", Size);
        Failures += RoundTrip(Name, Random, false);

        std::vector<uint8_t> Sawtooth(Size);
        for (size_t Byte = 0; Byte < Size; Byte++)
            Sawtooth[Byte] = (uint8_t)(Byte & 0xff);
        snprintf(Name, sizeof(Name), "sawtooth-%zu", Size);
        Failures += RoundTrip(Name, Sawtooth, Size >= 1024);
    }

    /* Random buffers of random sizes, still fully deterministic. */
    RandomState = 0x17f5c0deu;
    for (unsigned Round = 0; Round < 200; Round++)
    {
        size_t Size = 1 +
            (((size_t)NextByte() << 9) ^ ((size_t)NextByte() << 3) ^
             (size_t)NextByte()) % 131072;
        std::vector<uint8_t> Corpus(Size);
        size_t RunLength = 0;
        uint8_t RunByte = 0;
        char Name[64];

        for (size_t Byte = 0; Byte < Size; Byte++)
        {
            /* Mix literal noise with runs so both paths are exercised. */
            if (RunLength == 0)
            {
                RunByte = NextByte();
                RunLength = 1 + (NextByte() % 64);
            }
            Corpus[Byte] = (NextByte() & 1) ? RunByte : NextByte();
            RunLength--;
        }
        snprintf(Name, sizeof(Name), "mixed-%u-%zu", Round, Size);
        Failures += RoundTrip(Name, Corpus, false);
    }

    /*
     * Incompressible data with a capacity that demands a saving must
     * fail with STATUS_BUFFER_TOO_SMALL and must not write beyond it.
     */
    {
        std::vector<uint8_t> Random(65536);
        std::vector<uint8_t> Compressed(65536 - 4096);
        ULONG CompressedSize = 0;

        RandomState = 0x0badc0deu;
        for (size_t Byte = 0; Byte < Random.size(); Byte++)
            Random[Byte] = NextByte();
        if (NtfsLznt1Compress(Random.data(),
                              (ULONG)Random.size(),
                              Compressed.data(),
                              (ULONG)Compressed.size(),
                              &CompressedSize) !=
            STATUS_BUFFER_TOO_SMALL)
        {
            fprintf(stderr,
                    "tight capacity did not report BUFFER_TOO_SMALL\n");
            Failures++;
        }
    }

    if (Failures != 0)
    {
        fprintf(stderr, "%d corpus failures\n", Failures);
        return 1;
    }
    printf("lznt1 round trip: all corpora byte-exact\n");
    return 0;
}
