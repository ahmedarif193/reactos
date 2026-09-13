//
// SPDX-FileCopyrightText: Copyright 2022-2024 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0
//
// Licensed under the Apache License, Version 2.0 (the License); you may
// not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an AS IS BASIS, WITHOUT
// WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#if !defined ML_BIT_BUFFER_HPP
#define ML_BIT_BUFFER_HPP

#pragma once

#include "ml_raw_buffer.hpp"

#include <cstdint>

struct bitbuf_t
{
private:
    uint32_t *_buf;
    uint32_t  _next = 0;
    int  _pos;           // bit pos of next bit
    int  _limit;         // in bytes
    int  _substream_start = 0; // start position for substreams
    int  _substream_end = 0;   // end position for substreams
    bool _enabled = false;
    raw_buffer_t<uint8_t> *_buffer;
public:
    // Read constructor
    bitbuf_t(const void *buf, int used_bytes) : _buffer(nullptr)
    {
        _limit = used_bytes & ~3;
        _buf = reinterpret_cast<uint32_t *>(const_cast<void*>(buf));
        _pos = 0;
    }

    // Write constructor
    bitbuf_t(raw_buffer_t<uint8_t> &buffer, int reserve_bytes, bool disable_writes) : _buffer(&buffer)
    {
        _enabled = !disable_writes;
        if ( _enabled ) _buffer->reserve(reserve_bytes);
        _limit = _buffer->capacity() & ~3;
        prime( _buffer->used() * 8 ); // Start at the end of the buffer's used data
    }

    // Sub-stream writer
    bitbuf_t(bitbuf_t &dest, int bitpos, int bitlen=0) : _buffer(dest._buffer)
    {
        _limit = _buffer->capacity() & ~3;
        _substream_start = bitpos;
        bitlen = (bitlen <= 0) ? (_limit * 8) - bitpos : bitlen; // Default to rest of buffer
        _substream_end = bitpos + bitlen;
        int required = (_substream_end + 7 ) / 8;
        assert( required <= _limit );
        _enabled = dest._enabled;
         prime( bitpos );
    }

public:
    void put(int len, int32_t data)
    {
        assert( _buf == reinterpret_cast<uint32_t *>(_buffer->begin()) && "Buffer resized externally" );
        assert( (data & ((1 << len)-1)) == data && "Data must be pre-masked" );
        assert( ((_substream_end == 0)  || (_pos + len <= _substream_end)) && "Write past end of substream section" );
        if ( len > 0 && _enabled )
        {
            uint32_t next = _next;
            int bitpos = _pos & 0x1F;
            next |= uint32_t(data) << bitpos;

            if ( len >= (32 - bitpos) )
            {
                // Write won't fit, reserve more output space
                if ( (_pos / 8) >= _limit )
                {
                    extend();
                }

                _buf[_pos >> 5] = next;  // Requires little-endian
                next = uint32_t(data) >> (32 - bitpos);
            }

            _next = next;
        }

        _pos += len;
    }

    void put_masked(int len, int32_t data)
    {
        put(len, data & ((1 << len)-1));
    }

    void fill(int len, unsigned bit)
    {
        const uint32_t mask = 0xFFFFFFFF * bit;
        int remain = len;
        while ( remain >= 32 )
        {
            put(32, mask);
            remain -= 32;
        }
        if (remain > 0)
            put(remain, mask & ((1u << remain) - 1)  );
    }

    void align(int bits, int fill_byte)
    {
        // Alignments must be power of 2
        assert( (bits & (bits - 1)) == 0  && bits );
        const int mask = bits-1;

        int distance = (bits - (_pos & mask)) & mask;

        // Byte align first
        put_masked( distance & 7, fill_byte );
        distance &= ~7;
        while (distance != 0)
        {
            put(8, fill_byte);
            distance -= 8;
        }
    }

    void reposition(int bitpos)
    {
        int end = (_substream_end != 0) ? _substream_end : _limit * 8;
        assert( (bitpos >= 0 && bitpos <= end) && "Can't reposition out of stream" );
        assert( (_substream_end == 0 || bitpos >= _substream_start) && "Can't reposition before substream");
        if ((_pos != bitpos) && (bitpos > 0) && (bitpos <= end) )
        {
            // Reposition in bitstream. Caller must flush if writing.
            prime(bitpos);
        }
    }

    void flush(bool done=true)
    {
        if ( !_enabled ) return;
        // If buffering word is not empty, write it out as-is.
        if ( _pos & 0x1F )
        {
            // If writing a substream, blend any overlapping words with the parent stream
            if ( _substream_end )
            {
                int remain = _substream_end - (_pos & ~0x1F);  // Remaining word-bits in this substream
                if ( remain < 32 )                             // Will overlap happen?
                {
                    uint32_t mask = ~0u << (32 - remain);      // Mask the parent bits that we want to keep
                    _next = (_buf[_pos >> 5] & mask) | (_next & ~mask);
                }
            }
            // Otherwise limited by the buffer
            else
            {
                // Only extend by space required to flush remaining word.
                if ( (_pos / 8) >= _limit )
                {
                    extend(true);
                }
            }
            _buf[_pos >> 5] = _next;
        }
        if ( done && !_substream_end )
        {
            _buffer->set_used( _pos / 8 );
        }
    }

    void sync(bitbuf_t &substream)
    {
        flush(false);
        substream.flush(false);
        prime( std::max(_pos, substream._pos) );
        substream._buffer = nullptr;
    }

    int get(int len)
    {
        if ( len == 0 )
        {
            return 0;
        }

        const unsigned mask = (1u << len) - 1;
        assert( (_pos / 8) < _limit );
        uint32_t next = _buf[_pos >> 5];
        int bitpos = _pos & 0x1F;
        // Bits from this word
        unsigned value = next >> bitpos;
        _pos += len;

        // Some of the bits are in the next word
        if ( len > (32 - bitpos) )
        {
            assert( (_pos / 8) < _limit );
            next = _buf[_pos >> 5];
            value |= next << (32 - bitpos);
        }

        return int(value & mask);
    }

    void read_align(int bits)
    {
        // Alignments must be power of 2
        assert( (bits & (bits - 1)) == 0  && bits );
        const int mask = bits-1;
        _pos += (bits - (_pos & mask)) & mask;
    }

    bool read_eos() const { return _pos/8 >= _limit; }

    int read_avail() const { return (_limit - (_pos / 8)) * 8 - (_pos & 7); }
    int read_avail(int watermark) const { return (watermark - (_pos / 8)) * 8 - (_pos & 7); }

    int pos() const { return _pos; }
    int byte_pos() const { return _pos / 8; }
    int byte_length() const { return _limit; }

private:
    void prime(int bitpos)
    {
        assert( (bitpos >= 0) && (bitpos / 8) < _limit );
        // Prime (start up) the bitstream writer at the given bit position
        _pos = bitpos;
        _buf = reinterpret_cast<uint32_t *>(_buffer->begin());
        _next = _buf[bitpos >> 5];
        _next &= (1u << (bitpos & 0x1F)) - 1;
    }

    void extend(bool exact_resize=false)
    {
        assert(_enabled);
        _buffer->set_used( (_pos / 8) & ~3 );  // Only use whole words
        _buffer->reserve( sizeof(uint32_t), exact_resize );       // Buffer implementation must optimise small requests
        assert( (_buffer->capacity() & ~3) > _limit );
        assert( _substream_end == 0 ); // Can't extend a substream
        _limit = _buffer->capacity() & ~3;
        _buf = reinterpret_cast<uint32_t *>(_buffer->begin());
    }
};

#endif // ML_BIT_BUFFER_HPP
