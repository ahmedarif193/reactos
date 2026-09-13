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

#if !defined ML_RAW_BUFFER_HPP
#define ML_RAW_BUFFER_HPP

#pragma once

#include <memory>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <type_traits>
#include <algorithm>

typedef void* (*realloc_t)(void *ptr, size_t size, int);

template <typename TYPE>
struct raw_buffer_t
{
    static_assert(std::is_trivially_copyable<TYPE>::value, "expected simple storage type");
    constexpr static int CAPACITY_ALIGN = 16;
    TYPE *_data;
    int  _used;
    int  _capacity;
    int  _reallocArg = 0;
    realloc_t _realloc=&realloc_proxy;

public:
    raw_buffer_t(int capacity, int arg=0, realloc_t rfunc=nullptr)
    {
        assert(capacity > 0);
        _realloc = (rfunc != nullptr) ? rfunc : &realloc_proxy;
        _capacity = (capacity + CAPACITY_ALIGN - 1) & ~(CAPACITY_ALIGN - 1);
        _reallocArg = arg;
        _data = reinterpret_cast<TYPE*>(_realloc(nullptr, _capacity * sizeof(TYPE), _reallocArg));
        _used = 0;
    }

    raw_buffer_t(raw_buffer_t &&other)
    {
        _capacity = other._capacity;
        other._capacity = 0;
        _data = other._data;
        other._data = nullptr;
        _used = other._used;
        other._used = 0;
        _reallocArg = other._reallocArg;
        _realloc = other._realloc;
    }

    raw_buffer_t(TYPE *data, int used, int capacity)
    {
        _data = data;
        _used = used;
        _capacity = capacity;
    }

    ~raw_buffer_t()
    {
        if (_data)
        {
            _realloc(_data, 0, _reallocArg);
        }
    }

    TYPE *begin() { return _data; }
    TYPE *end() { return _data + _used; }
    int   used() const { return _used; }
    int   capacity() const { return _capacity; }
    void  clear() { _used = 0; }

    const TYPE &operator[](int index) const { assert(index < _used); return _data[index]; }

    void set_used(int used)
    {
        assert(used >= _used);
        assert(used <= _capacity);
        _used = used;
    }

    TYPE *reserve(int count, bool exact_resize=false)
    {
        int req_capacity = _used + count;
        if ( req_capacity > _capacity )
        {
            if ( !exact_resize )
            {
                req_capacity = std::max(req_capacity, _capacity * 2);
            }

            auto *p = reinterpret_cast<TYPE*>( _realloc(_data, req_capacity * sizeof(TYPE), _reallocArg) );
            if ( !p )
            {
                return nullptr;
            }
            _data = p;
            _capacity = req_capacity;
        }
        int at = _used;
        return _data + at;
    }

    TYPE *use(int count)
    {
        int at = _used;
        _used += count;
        return _data + at;
    }

    TYPE *detach()
    {
        auto tmp = _data;
        _data = nullptr;
        return tmp;
    }

    void align(int align_bytes, TYPE fill)
    {
        int count = (((_used + align_bytes - 1) / align_bytes) * align_bytes) - _used;
        TYPE *p = reserve(count);
        assert(p);
        use(count);
        while (count--)
        {
            *p++ = fill;
        }
    }

    void remove_left(int count)
    {
        int to_move = _used - count;
        if (to_move >= 0)
        {
            memmove(_data, _data + count, to_move * sizeof(TYPE));
        }
        _used = to_move;
    }

private:
    static void *realloc_proxy(void *ptr, size_t size, int)
    {
        return realloc(ptr, size);
    }
};

#endif // ML_RAW_BUFFER_HPP
