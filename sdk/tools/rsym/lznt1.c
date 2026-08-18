/*
 * PROJECT:     ReactOS rsym
 * PURPOSE:     LZNT1 compressor for the .rossym section
 * LICENSE:     See COPYING in the top level directory
 */

#include <stdlib.h>
#include <string.h>
#include "rsym.h"



#define LZNT1_CHUNK_SIZE 4096

static unsigned
Lznt1CompressChunk(const unsigned char *In, unsigned InLength, unsigned char *Out, unsigned OutLimit)
{
    unsigned InPos = 0, OutPos = 0, FlagPos, Bit;
    unsigned char Flags;

    while (InPos < InLength)
    {
        if (OutPos >= OutLimit)
            return 0;
        FlagPos = OutPos;
        Flags = 0;
        OutPos++;

        for (Bit = 0; Bit < 8 && InPos < InLength; Bit++)
        {
            unsigned DisplacementBits, LengthBits, MaxLength, MaxDisplacement;
            unsigned BestLength = 0, BestDisplacement = 0, Start, Search, Length;

            for (DisplacementBits = 12; DisplacementBits > 4; DisplacementBits--)
                if ((1u << (DisplacementBits - 1)) < InPos) break;
            LengthBits = 16 - DisplacementBits;
            MaxLength = (1u << LengthBits) - 1 + 3;
            MaxDisplacement = 1u << DisplacementBits;

            Start = (InPos > MaxDisplacement) ? InPos - MaxDisplacement : 0;
            for (Search = Start; Search < InPos; Search++)
            {
                Length = 0;
                while (InPos + Length < InLength && Length < MaxLength && In[Search + Length] == In[InPos + Length])
                    Length++;
                if (Length > BestLength)
                {
                    BestLength = Length;
                    BestDisplacement = InPos - Search;
                }
            }

            if (BestLength >= 3)
            {
                unsigned Token = ((BestDisplacement - 1) << LengthBits) | (BestLength - 3);

                if (OutPos + 2 > OutLimit)
                    return 0;
                Out[OutPos++] = (unsigned char)(Token & 0xFF);
                Out[OutPos++] = (unsigned char)(Token >> 8);
                Flags |= (unsigned char)(1u << Bit);
                InPos += BestLength;
            }
            else
            {
                if (OutPos + 1 > OutLimit)
                    return 0;
                Out[OutPos++] = In[InPos++];
            }
        }

        Out[FlagPos] = Flags;
    }

    return OutPos;
}

unsigned
Lznt1Compress(const void *Uncompressed, unsigned UncompressedLength, void *Compressed, unsigned CompressedLimit)
{
    const unsigned char *In = (const unsigned char *)Uncompressed;
    unsigned char *Out = (unsigned char *)Compressed;
    unsigned InPos = 0, OutPos = 0;

    while (InPos < UncompressedLength)
    {
        unsigned ChunkLength = UncompressedLength - InPos;
        unsigned Produced, Header;

        if (ChunkLength > LZNT1_CHUNK_SIZE)
            ChunkLength = LZNT1_CHUNK_SIZE;

        if (OutPos + 2 > CompressedLimit)
            return 0;

        Produced = Lznt1CompressChunk(In + InPos, ChunkLength, Out + OutPos + 2, CompressedLimit - OutPos - 2);
        if (Produced == 0 || Produced >= ChunkLength)
        {
            if (OutPos + 2 + ChunkLength > CompressedLimit)
                return 0;
            memcpy(Out + OutPos + 2, In + InPos, ChunkLength);
            Header = 0x3000 | (ChunkLength - 1);
            Produced = ChunkLength;
        }
        else
        {
            Header = 0xB000 | (Produced - 1);
        }

        Out[OutPos] = (unsigned char)(Header & 0xFF);
        Out[OutPos + 1] = (unsigned char)(Header >> 8);
        OutPos += 2 + Produced;
        InPos += ChunkLength;
    }

    return OutPos;
}
