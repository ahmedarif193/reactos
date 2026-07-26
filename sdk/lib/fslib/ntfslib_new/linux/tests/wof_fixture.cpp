/*
 * Build independent WOF test chunks with libwim's public compression API.
 * This program is test-only and is not linked into ntfslib.
 */

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

extern "C"
{
struct wimlib_compressor;

int
wimlib_create_compressor(int CompressionType,
                         size_t MaximumBlockSize,
                         unsigned int CompressionLevel,
                         struct wimlib_compressor** Compressor);

size_t
wimlib_compress(const void* UncompressedData,
                size_t UncompressedSize,
                void* CompressedData,
                size_t CompressedCapacity,
                struct wimlib_compressor* Compressor);

void
wimlib_free_compressor(struct wimlib_compressor* Compressor);
}

static bool
WriteFile(const char* Path, const std::vector<std::uint8_t>& Data)
{
    std::ofstream Output(Path, std::ios::binary | std::ios::trunc);
    if (!Output)
        return false;
    Output.write(reinterpret_cast<const char*>(Data.data()), Data.size());
    return Output.good();
}

static bool
ReadFile(const char* Path, std::vector<std::uint8_t>& Data)
{
    std::ifstream Input(Path, std::ios::binary);
    if (!Input)
        return false;
    Data.assign(std::istreambuf_iterator<char>(Input),
                std::istreambuf_iterator<char>());
    return Input.good() || Input.eof();
}

static void
AppendLe32(std::vector<std::uint8_t>& Output, std::uint32_t Value)
{
    Output.push_back(static_cast<std::uint8_t>(Value));
    Output.push_back(static_cast<std::uint8_t>(Value >> 8));
    Output.push_back(static_cast<std::uint8_t>(Value >> 16));
    Output.push_back(static_cast<std::uint8_t>(Value >> 24));
}

static int
Generate(const char* Path, std::size_t Size)
{
    std::vector<std::uint8_t> Data(Size);
    std::uint32_t State = 0x6d2b79f5;

    for (std::size_t Index = 0; Index < Size; Index++)
    {
        if (((Index / 32768) & 1) == 0)
        {
            Data[Index] = static_cast<std::uint8_t>(
                "WOF-core-pattern-"[Index % 17]);
        }
        else
        {
            State ^= State << 13;
            State ^= State >> 17;
            State ^= State << 5;
            Data[Index] = static_cast<std::uint8_t>(State);
        }
    }

    if (!WriteFile(Path, Data))
    {
        std::perror(Path);
        return 1;
    }
    return 0;
}

static int
Compress(unsigned Algorithm, const char* InputPath, const char* OutputPath)
{
    std::vector<std::uint8_t> Input;
    std::vector<std::uint8_t> Data;
    std::vector<std::uint32_t> Offsets;
    struct wimlib_compressor* Compressor = nullptr;
    std::size_t ChunkSize;
    int CompressionType;
    bool SawCompressed = false;
    bool SawRaw = false;

    switch (Algorithm)
    {
        case 0:
            ChunkSize = 4096;
            CompressionType = 1; /* WIMLIB_COMPRESSION_TYPE_XPRESS */
            break;
        case 1:
            ChunkSize = 32768;
            CompressionType = 2; /* WIMLIB_COMPRESSION_TYPE_LZX */
            break;
        case 2:
            ChunkSize = 8192;
            CompressionType = 1;
            break;
        case 3:
            ChunkSize = 16384;
            CompressionType = 1;
            break;
        default:
            std::fprintf(stderr, "unsupported WOF algorithm: %u\n", Algorithm);
            return 2;
    }

    if (!ReadFile(InputPath, Input) || Input.empty())
    {
        std::fprintf(stderr, "cannot read nonempty input: %s\n", InputPath);
        return 1;
    }
    if (wimlib_create_compressor(CompressionType,
                                 ChunkSize,
                                 50,
                                 &Compressor) != 0)
    {
        std::fprintf(stderr, "libwim compressor creation failed\n");
        return 1;
    }

    for (std::size_t Offset = 0; Offset < Input.size();)
    {
        std::size_t UncompressedSize =
            std::min(ChunkSize, Input.size() - Offset);
        std::vector<std::uint8_t> Compressed(UncompressedSize);
        std::size_t CompressedSize = wimlib_compress(
            Input.data() + Offset,
            UncompressedSize,
            Compressed.data(),
            Compressed.size(),
            Compressor);

        if (CompressedSize != 0 && CompressedSize < UncompressedSize)
        {
            Data.insert(Data.end(),
                        Compressed.begin(),
                        Compressed.begin() + CompressedSize);
            SawCompressed = true;
        }
        else
        {
            Data.insert(Data.end(),
                        Input.begin() + Offset,
                        Input.begin() + Offset + UncompressedSize);
            SawRaw = true;
        }

        Offset += UncompressedSize;
        if (Offset < Input.size())
        {
            if (Data.size() > UINT32_MAX)
            {
                std::fprintf(stderr, "test fixture exceeded 32-bit WOF table\n");
                wimlib_free_compressor(Compressor);
                return 1;
            }
            Offsets.push_back(static_cast<std::uint32_t>(Data.size()));
        }
    }
    wimlib_free_compressor(Compressor);

    if (!SawCompressed || !SawRaw)
    {
        std::fprintf(stderr,
                     "fixture did not exercise both compressed and raw chunks\n");
        return 1;
    }

    std::vector<std::uint8_t> Output;
    Output.reserve(Offsets.size() * sizeof(std::uint32_t) + Data.size());
    for (std::uint32_t Offset : Offsets)
        AppendLe32(Output, Offset);
    Output.insert(Output.end(), Data.begin(), Data.end());

    if (!WriteFile(OutputPath, Output))
    {
        std::perror(OutputPath);
        return 1;
    }
    return 0;
}

int
main(int Argc, char** Argv)
{
    if (Argc == 4 && std::string(Argv[1]) == "--generate")
    {
        char* End = nullptr;
        errno = 0;
        unsigned long long Size = std::strtoull(Argv[3], &End, 10);
        if (errno || !End || *End || Size == 0 || Size > SIZE_MAX)
            return 2;
        return Generate(Argv[2], static_cast<std::size_t>(Size));
    }
    if (Argc == 5 && std::string(Argv[1]) == "--compress")
    {
        char* End = nullptr;
        errno = 0;
        unsigned long Algorithm = std::strtoul(Argv[2], &End, 10);
        if (errno || !End || *End || Algorithm > 3)
            return 2;
        return Compress(static_cast<unsigned>(Algorithm), Argv[3], Argv[4]);
    }

    std::fprintf(stderr,
                 "usage: %s --generate OUTPUT SIZE\n"
                 "       %s --compress ALGORITHM INPUT OUTPUT\n",
                 Argv[0],
                 Argv[0]);
    return 2;
}
