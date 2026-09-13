//
// SPDX-FileCopyrightText: Copyright 2021-2025 Arm Limited and/or its affiliates <open-source-office@arm.com>
// SPDX-FileCopyrightText: Copyright (c) 2025 Tomeu Vizoso <tomeu@tomeuvizoso.net>
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

#ifndef ETHOSU_ENCODE_SUPPORT_H
#define ETHOSU_ENCODE_SUPPORT_H

#include <memory>
#include <stdexcept>
#include <vector>
#include <algorithm>
#include <cstdint>

struct WeightTransformParam
{
    int o, h, w, i, zeroCount;
    int *zeroPoints;
    bool is_signed;
};

typedef int (*WeightTransformFunc)(const WeightTransformParam *param, int weight);

enum class EthosUTraversal {
   DepthFirst,
   PartKernel,
   Depthwise
};

struct WeightsNotSparse : public std::runtime_error
{
    WeightsNotSparse() : std::runtime_error("weights not sparse") {}
};

struct SparsityTracker
{
    int _sparse_zeroes = 4;
    int _sparse_index = 0;
    uint32_t _sparse_pos = 0xFFFFFFFF;
    void Reset() { _sparse_pos = 0xFFFFFFFF; }

    void Check(uint32_t pos, int depth, int weight)
    {
        if ( _sparse_pos != pos )
        {
            _sparse_pos = pos;
            _sparse_zeroes = 0;
            _sparse_index = 0;
            if ( depth & 3 ) throw WeightsNotSparse();
        }

        if ( weight == 0 ) _sparse_zeroes++;
        else if ( weight > 127 || weight < -127 ) throw WeightsNotSparse();

        if ( (_sparse_index & 3) == 3 )
        {
            if ( _sparse_zeroes < 2 ) throw WeightsNotSparse();
            _sparse_zeroes = 0;
        }

        _sparse_index++;
    }
};

struct Point2i
{
    int x;
    int y;
    Point2i(int a, int b) : x(a), y(b) {}
};

static inline int RoundAway(int value, int align)
{
    assert(align > 0);
    int rem = value % align;
    if ( rem == 0 )
    {
        return value;
    }
    else if ( rem < 0 )
    {
        return value - (align + rem);
    }
    return value + (align - rem);
}

typedef int Shape[4];

class IWeightSource
{
public:
    virtual ~IWeightSource() = default;
    virtual int Elements() = 0;
    virtual int Get(int16_t *buffer, int count) = 0;
};

struct IVolumeWeightSource : public IWeightSource
{
    virtual ~IVolumeWeightSource() = default;
    virtual void SetSource(const void *buffer, int depthOffset, const Shape &ohwiShape, const Shape &ohwiStrides, int streamIndex) = 0;
};

class WeightSourceCommon : public IVolumeWeightSource
{

protected:
    const void *_source;
    int16_t _streams = 1;
    int16_t _streamIndex = 0;
    int _ofmDepth = 0;
    int _ifmDepth = 0;
    int _kernelH = 0;
    int _kernelW = 0;
    int _ohwiStrides[4];

protected:
    void SetSourceCommon(const void *buffer, int depthOffset, const Shape &ohwiShape, const Shape &ohwiStrides, int streamIndex, bool separated)
    {
        assert(streamIndex < _streams);
        _streamIndex = streamIndex;

        int streamOffset = depthOffset * ohwiStrides[0];
        _source = reinterpret_cast<const uint8_t *>(buffer) + streamOffset;
        _ifmDepth = ohwiShape[3];
        _ofmDepth = separated ? (ohwiShape[0] + _streams - 1 - streamIndex) / _streams : ohwiShape[0];
        _kernelH = ohwiShape[1];
        _kernelW = ohwiShape[2];

        // Bring in values for better cache locality
        _ohwiStrides[0] = ohwiStrides[0] * (separated ? _streams : 1);
        _ohwiStrides[1] = ohwiStrides[1];
        _ohwiStrides[2] = ohwiStrides[2];
        _ohwiStrides[3] = ohwiStrides[3];
    }

    int Elements() override { return _ofmDepth * _ifmDepth * _kernelH * _kernelW; }

    inline int WeightIndex(int ofm_z, int wy, int wx, int ifm_z) const
    {
        return ofm_z * _ohwiStrides[0] + wy * _ohwiStrides[1] + wx * _ohwiStrides[2] + ifm_z * _ohwiStrides[3];
    }
};

template<typename TYPE>
class EthosU85WeightOrdering : public WeightSourceCommon
{
protected:
    static constexpr int InterleaveDepth = 4;
    // Transform
    WeightTransformParam *_param;
    WeightTransformFunc _transform;
    // Loop Limits
    Point2i _stride = Point2i(0, 0);
    int _ofmBlockDepth;
    int _ifmBlockDepth;
    short _ofmUBlockDepth;
    short _ifmUBlockDepth;
    short _decompX;
    short _decompY;
    short _subKernelRound;
    short _dwPaddingCount;
    // Saved state
    int _ofmBlockZ = 0;
    int _ifmBlockZ = 0;
    int _subKernelX = 0;
    int _subKernelY = 0;
    int _ifmUBlockOuter = 0;
    int _ifmUBlockInner = 0;
    int _ofmUBlockZ = 0;
    int _ifmUBlockZ = 0;
    int _subKernelElements = 0;
    int _strideX = 0;
    int _strideY = 0;
    int _kernelX = 0;
    int _kernelY = 0;
    int _ofmUBlockInner = 0;
    int _ofmUBlockOuter = 0;
    int _ifmLoopInc = 0;
    int _padding = 0;
    EthosUTraversal _traversal;
    bool _sparse;
    SparsityTracker _sparsity;

public:
    EthosU85WeightOrdering(int cores, int macs, Point2i stride, const Point2i &dilation, int ofmBlockDepth, int ifmBlockDepth, int ifmBitDepth,
        int ofmUBlockDepth, WeightTransformFunc func, WeightTransformParam *param, EthosUTraversal traversal, bool sparse)
    {
        const bool ifm16bit = (ifmBitDepth == 16);
        _streams = cores;
        _transform = func;
        _param = param;
        _traversal = traversal;
        _sparse = sparse;
        _stride = stride;

        _ofmBlockDepth = ofmBlockDepth;
        _ifmBlockDepth = ifmBlockDepth;
        _ofmUBlockDepth = short(ofmUBlockDepth);

        if ( traversal == EthosUTraversal::PartKernel )
        {
            _subKernelRound = (ifm16bit || sparse) ? 10 : 5;
            _ifmUBlockDepth = ifm16bit && !sparse ? 8 : _ifmBlockDepth;
        }
        else
        {
            if ( traversal == EthosUTraversal::DepthFirst )
            {
                _stride = Point2i(1, 1);
                _subKernelRound = 1;
                _ifmUBlockDepth = _ifmBlockDepth;
            }
            else if ( traversal == EthosUTraversal::Depthwise )
            {
                _subKernelRound = 10;
                _ifmUBlockDepth = 1;
            }
        }

        _decompX = short(8 / dilation.x);
        _decompY = short(8 / dilation.y);
        _dwPaddingCount = (!ifm16bit && macs <= 256) ? 0 : (macs <= 512) ? 2 : 6;

        _ifmLoopInc = -_ifmBlockDepth;
    }

    void SetSource(const void *buffer, int depthOffset, const Shape &ohwiShape, const Shape &ohwiStrides, int streamIndex) override
    {
        SetSourceCommon(buffer, depthOffset, ohwiShape, ohwiStrides, streamIndex, false);
        assert(_streamIndex == streamIndex);
        _ofmUBlockZ = _streamIndex * InterleaveDepth;
        _sparsity.Reset();
    }

public:
    int Get(int16_t *output, int count) override
    {
        if ( _traversal == EthosUTraversal::Depthwise )
        {
            assert(!_sparse);
            return GetNext<false, true>(output, count);
        }
        else if ( _sparse )
        {
            return GetNext<true, false>(output, count);
        }

        return GetNext<false, false>(output, count);
    }

    template<bool IS_SPARSE, bool IS_DEPTHWISE>
    int GetNext(int16_t *output, int count)
    {
        if ( _ofmBlockZ >= _ofmDepth )
        {
            return 0;
        }

        int ofmBlockZ, ifmBlockZ;
        int ifmUBlockOuter, ifmUBlockInner;
        int ifmUBlockZ, ofmUBlockZ, ofmUBlockInner, ofmUBlockOuter;
        int subKernelX, subKernelY;
        int strideX, strideY;
        int kernelX, kernelY;
        int padding;
        int16_t *write = output;

        const TYPE *buffer = reinterpret_cast<const TYPE *>(_source);

        for ( ofmBlockZ = _ofmBlockZ; ofmBlockZ < _ofmDepth; ofmBlockZ += _ofmBlockDepth )
        {
            _ifmLoopInc = -_ifmLoopInc;
            int clippedOfmBlockDepth = std::min(_ofmBlockDepth, _ofmDepth - ofmBlockZ);
            // IFM blocks required for the brick
            for ( ifmBlockZ = _ifmBlockZ; ifmBlockZ < (IS_DEPTHWISE ? 1 : _ifmDepth) && ifmBlockZ >= 0; ifmBlockZ += _ifmLoopInc )
            {
                _ifmBlockZ = ifmBlockZ;
                int clippedIfmBlockDepth = std::min(_ifmBlockDepth, _ifmDepth - ifmBlockZ);

                // Weight decomposition
                // Subkernel splitting (W)
                for ( subKernelX = _subKernelX; subKernelX < _kernelW; subKernelX += _decompX )
                {
                    int subWidth = std::min<int>(_kernelW - subKernelX, _decompX);
                    // Subkernel Splitting (H)
                    for ( subKernelY = _subKernelY; subKernelY < _kernelH; subKernelY += _decompY )
                    {
                        int subHeight = std::min<int>(_kernelH - subKernelY, _decompY);
                        int ifmBlockDepthOuter = IS_DEPTHWISE ? 1 : clippedIfmBlockDepth;
                        for ( ifmUBlockOuter = _ifmUBlockOuter; ifmUBlockOuter < ifmBlockDepthOuter; ifmUBlockOuter += _ifmUBlockDepth )
                        {
                            // OFM uBlocks in OFM-block over depth
                            for ( ofmUBlockOuter = _ofmUBlockOuter; ofmUBlockOuter < clippedOfmBlockDepth; ofmUBlockOuter += _ofmUBlockDepth )
                            {
                                // Part kernel first works across the kernel H/W and needs padding
                                if ( !_subKernelElements )
                                {
                                    int subKernelElements = subWidth * subHeight;
                                    _subKernelElements = RoundAway(subKernelElements, _subKernelRound);
                                }
                                for ( strideY = _strideY; strideY < _stride.y; ++strideY )
                                {
                                    int stridedKernelH = (subHeight + _stride.y - 1 - strideY) / _stride.y;
                                    for ( strideX = _strideX; strideX < _stride.x; ++strideX )
                                    {
                                        int stridedKernelW = (subWidth + _stride.x - 1 - strideX) / _stride.x;
                                        for ( kernelY = _kernelY; kernelY < stridedKernelH; ++kernelY )
                                        {
                                            int y = kernelY;
                                            for ( kernelX = _kernelX; kernelX < stridedKernelW; ++kernelX )
                                            {
                                                int x = kernelY % 2 == 0 ? kernelX : stridedKernelW - 1 - kernelX;
                                                _subKernelElements--;
                                                int ifmUBlockInnerStep = IS_DEPTHWISE ? 1 : (IS_SPARSE ? 16 : 8);
                                                for ( ifmUBlockInner = _ifmUBlockInner; ifmUBlockInner < _ifmUBlockDepth; ifmUBlockInner += ifmUBlockInnerStep )
                                                {
                                                    // Feed OFM uBlock elements
                                                    for ( ofmUBlockZ = _ofmUBlockZ; ofmUBlockZ < _ofmUBlockDepth; ofmUBlockZ += InterleaveDepth * _streams )
                                                    {
                                                        for ( ofmUBlockInner = _ofmUBlockInner; ofmUBlockInner < InterleaveDepth; ofmUBlockInner++ )
                                                        {
                                                            // Source IFM uBlock elements (only 1 element deep if
                                                            // depthwise)
                                                            for ( ifmUBlockZ = _ifmUBlockZ; ifmUBlockZ < ifmUBlockInnerStep; ifmUBlockZ++ )
                                                            {
                                                                // Source position within the current subkernel
                                                                int wx = subKernelX + strideX + x * _stride.x;
                                                                int wy = subKernelY + strideY + y * _stride.y;
                                                                // Source IFM/OFM slices
                                                                int ifm_z = ifmBlockZ + ifmUBlockOuter + ifmUBlockInner + ifmUBlockZ;
                                                                int ofm_z = ofmBlockZ + ofmUBlockOuter + ofmUBlockInner + ofmUBlockZ;
                                                                int weight = 0;
                                                                if ( ifm_z < _ifmDepth && ofm_z < _ofmDepth )
                                                                {
                                                                    _param->o = ofm_z;
                                                                    _param->h = wy;
                                                                    _param->w = wx;
                                                                    _param->i = ifm_z;
                                                                    weight = int(buffer[WeightIndex(ofm_z, wy, wx, ifm_z)]);
                                                                    weight = _transform(_param, weight);
                                                                }

                                                                if constexpr ( IS_SPARSE )
                                                                    _sparsity.Check((unsigned(wy) << 16) | wx, ifm_z, weight);

                                                                *write++ = int16_t(weight);

                                                                if ( --count == 0 )
                                                                {
                                                                    // Save state
                                                                    _subKernelElements++;
                                                                    _ifmUBlockZ = ifmUBlockZ + 1;
                                                                    _ofmUBlockInner = ofmUBlockInner;
                                                                    _ofmUBlockZ = ofmUBlockZ;
                                                                    _ifmUBlockInner = ifmUBlockInner;
                                                                    _kernelX = kernelX;
                                                                    _kernelY = kernelY;
                                                                    _strideX = strideX;
                                                                    _strideY = strideY;
                                                                    _ofmUBlockOuter = ofmUBlockOuter;
                                                                    _ifmUBlockOuter = ifmUBlockOuter;
                                                                    _subKernelY = subKernelY;
                                                                    _subKernelX = subKernelX;
                                                                    _ofmBlockZ = ofmBlockZ;
                                                                    _ifmLoopInc = -_ifmLoopInc;
                                                                    return int(intptr_t(write - output));
                                                                }
                                                            }
                                                            _ifmUBlockZ = 0;
                                                        }
                                                        _ofmUBlockInner = 0;
                                                    }
                                                    _ofmUBlockZ = _streamIndex * InterleaveDepth;
                                                }
                                                // Depthwise padding
                                                if ( IS_DEPTHWISE && _subKernelElements % _subKernelRound == 0 )
                                                {
                                                    int padCount = _dwPaddingCount * _ofmUBlockDepth / _streams;
                                                    for ( padding = _padding; padding < padCount; padding++ )
                                                    {
                                                        *write++ = 0;
                                                        if ( --count == 0 )
                                                        {
                                                            // Save state
                                                            _subKernelElements++;
                                                            _padding = padding + 1;
                                                            _ifmUBlockInner = ifmUBlockInner;  // Will skip loop above
                                                            _kernelX = kernelX;
                                                            _kernelY = kernelY;
                                                            _strideX = strideX;
                                                            _strideY = strideY;
                                                            _ofmUBlockOuter = ofmUBlockOuter;
                                                            _ifmUBlockOuter = ifmUBlockOuter;
                                                            _subKernelY = subKernelY;
                                                            _subKernelX = subKernelX;
                                                            _ofmBlockZ = ofmBlockZ;
                                                            _ifmLoopInc = -_ifmLoopInc;
                                                            return int(intptr_t(write - output));
                                                        }
                                                    }
                                                    _padding = 0;
                                                }
                                                _ifmUBlockInner = 0;
                                            }
                                            _kernelX = 0;
                                        }
                                        _kernelY = 0;
                                    }
                                    _strideX = 0;
                                }
                                // Padding
                                if ( _subKernelElements > 0 )
                                {
                                    int padCount = _subKernelElements + (IS_DEPTHWISE ? _dwPaddingCount : 0);
                                    padCount = padCount * _ifmUBlockDepth * _ofmUBlockDepth / _streams;
                                    for ( padding = _padding; padding < padCount; padding++ )
                                    {
                                        *write++ = 0;
                                        if ( --count == 0 )
                                        {
                                            // Save state
                                            _padding = padding + 1;
                                            _strideY = strideY;  // Will skip loop above
                                            _ofmUBlockOuter = ofmUBlockOuter;
                                            _ifmUBlockOuter = ifmUBlockOuter;
                                            _subKernelY = subKernelY;
                                            _subKernelX = subKernelX;
                                            _ofmBlockZ = ofmBlockZ;
                                            _ifmLoopInc = -_ifmLoopInc;
                                            return int(intptr_t(write - output));
                                        }
                                    }
                                    _padding = 0;
                                }
                                _subKernelElements = 0;
                                _strideY = 0;
                            }
                            _ofmUBlockOuter = 0;
                        }
                        _ifmUBlockOuter = 0;
                    }
                    _subKernelY = 0;
                }
                _subKernelX = 0;
            }
        }
        _ifmLoopInc = -_ifmBlockDepth;
        _ifmBlockZ = 0;
        _ofmBlockZ = 0;
        // Return weights generated (less than requested count == EOS)
        return int(intptr_t(write - output));
    }
};

template<typename TYPE>
class EthosUWeightOrdering : public WeightSourceCommon
{
protected:
    // Transform
    WeightTransformParam *_param;
    WeightTransformFunc _transform;
    // Loop Limits
    int _ofmBlockDepth;
    int _ifmBlockDepth;
    short _ofmUBlockDepth;
    short _ifmUBlockDepth;
    short _decompX;
    short _decompY;
    short _subKernelRound;
    // Saved state
    int _ofmBlockZ = 0;
    int _ifmBlockZ = 0;
    int _subKernelX = 0;
    int _subKernelY = 0;
    int _ifmUBlockOuter = 0;
    int _ifmUBlockInner = 0;
    int _ofmUBlockZ = 0;
    int _ifmUBlockZ = 0;
    int _kernelElement = 0;
    int _ofmUBlock = 0;
    EthosUTraversal _traversal;

public:
    EthosUWeightOrdering(int cores, const Point2i &dilation, int ofmBlockDepth, int ifmBitDepth, int ofmUBlockDepth,
        int ifmUBlockDepth, WeightTransformFunc func, WeightTransformParam *param, EthosUTraversal traversal)
    {
        _streams = cores;
        _ofmBlockDepth = ofmBlockDepth;
        _ifmBlockDepth = ((traversal == EthosUTraversal::PartKernel) || (ifmBitDepth == 16)) ? 16 : 32;
        _ofmUBlockDepth = short(ofmUBlockDepth);
        _ifmUBlockDepth = short(ifmUBlockDepth);
        _decompX = short(8 / dilation.x);
        _decompY = short(8 / dilation.y);
        if ( traversal == EthosUTraversal::Depthwise )
        {
            _subKernelRound = 4;
        }
        else if ( traversal == EthosUTraversal::PartKernel )
        {
            _subKernelRound = (ifmBitDepth == 16) ? 2 : 4;
        }
        else
        {
            _subKernelRound = 1;
        }
        _transform = func;
        _param = param;
        _traversal = traversal;
    }

    void SetSource(const void *buffer, int depthOffset, const Shape &ohwiShape, const Shape &ohwiStrides, int streamIndex) override
    {
        SetSourceCommon(buffer, depthOffset + streamIndex, ohwiShape, ohwiStrides, streamIndex, true);
    }

public:
    int Get(int16_t *output, int count) override
    {
        if ( _traversal == EthosUTraversal::Depthwise ) return GetNext<false, true>(output, count);
        else if ( _traversal == EthosUTraversal::PartKernel ) return GetNext<true, false>(output, count);
        return GetNext<false, false>(output, count);
    }

    template<bool IS_PARTKERNEL, bool IS_DEPTHWISE>
    int GetNext(int16_t *output, int count)
    {
        if ( _ofmBlockZ >= _ofmDepth )
        {
            return 0;
        }

        int ofmBlockZ, ifmBlockZ;
        int ifmUBlockOuter, ifmUBlockInner;
        int ifmUBlockZ, ofmUBlockZ, ofmUBlock;
        int subKernelX, subKernelY;
        int kernelElement;
        int16_t *write = output;

        const TYPE *buffer = reinterpret_cast<const TYPE *>(_source);
        int streamBlockDepth = (_ofmBlockDepth + _streams - 1 - _streamIndex) / _streams;

        for ( ofmBlockZ = _ofmBlockZ; ofmBlockZ < _ofmDepth; ofmBlockZ += streamBlockDepth )
        {
            int clippedOfmBlockDepth = std::min(streamBlockDepth, _ofmDepth - ofmBlockZ);
            // IFM blocks required for the brick
            for ( ifmBlockZ = _ifmBlockZ; ifmBlockZ < (IS_DEPTHWISE ? 1 : _ifmDepth); ifmBlockZ += _ifmBlockDepth )
            {
                int clippedIfmBlockDepth;
                if ( IS_DEPTHWISE )
                {
                    clippedIfmBlockDepth = _ifmUBlockDepth;
                }
                else
                {
                    clippedIfmBlockDepth = IS_PARTKERNEL ? std::min(_ifmBlockDepth, _ifmDepth - ifmBlockZ) : _ifmBlockDepth;
                }

                // Weight decomposition
                // Subkernel Splitting (H)
                for ( subKernelY = _subKernelY; subKernelY < _kernelH; subKernelY += _decompY )
                {
                    int subHeight = std::min<int>(_kernelH - subKernelY, _decompY);
                    // Subkernel splitting (W)
                    for ( subKernelX = _subKernelX; subKernelX < _kernelW; subKernelX += _decompX )
                    {
                        int subWidth = std::min<int>(_kernelW - subKernelX, _decompX);
                        int subKernelElements = subWidth * subHeight;

                        // Part-kernel first works across the kernel H/W and needs padding
                        subKernelElements = RoundAway(subKernelElements, _subKernelRound);

                        int ifmBlockDepthOuter = IS_PARTKERNEL ? clippedIfmBlockDepth : 1;
                        int ifmBlockDepthInner = IS_PARTKERNEL ? 1 : clippedIfmBlockDepth;

                        for ( ifmUBlockOuter = _ifmUBlockOuter; ifmUBlockOuter < ifmBlockDepthOuter; ifmUBlockOuter += _ifmUBlockDepth )
                        {
                            // OFM uBlocks in OFM-block over depth
                            for ( ofmUBlock = _ofmUBlock; ofmUBlock < clippedOfmBlockDepth; ofmUBlock += _ofmUBlockDepth )
                            {
                                // HW Kernel element traversal - cannot be a H/W loop due to element
                                // padding requirement on depthwise/part-kernel configurations
                                for ( kernelElement = _kernelElement; kernelElement < subKernelElements; kernelElement++ )
                                {
                                    int kx = kernelElement % subWidth;
                                    int ky = kernelElement / subWidth;
                                    // IFM uBlocks in IFM-block over depth (only 1 uBlock if depthwise)
                                    // In case of part-kernel-first IFM uBlock traversal have already been handled
                                    // and this loop is ignored.
                                    for ( ifmUBlockInner = _ifmUBlockInner; ifmUBlockInner < ifmBlockDepthInner; ifmUBlockInner += _ifmUBlockDepth )
                                    {
                                        int ifmUBlock = ifmUBlockInner + ifmUBlockOuter;
                                        // Feed OFM uBlock elements
                                        for ( ofmUBlockZ = _ofmUBlockZ; ofmUBlockZ < _ofmUBlockDepth; ofmUBlockZ++ )
                                        {
                                            // Source IFM uBlock elements (only 1 element deep if depthwise)
                                            for ( ifmUBlockZ = _ifmUBlockZ; ifmUBlockZ < (IS_DEPTHWISE ? 1 : _ifmUBlockDepth); ifmUBlockZ++ )
                                            {
                                                // Source position within the current subkernel
                                                int wx = subKernelX + kx;
                                                int wy = subKernelY + ky;
                                                // Source IFM/OFM slices
                                                int ifm_z = ifmBlockZ + ifmUBlock + ifmUBlockZ;
                                                int ofm_z = ofmBlockZ + ofmUBlock + ofmUBlockZ;
                                                if ( (ifm_z < _ifmDepth) && (ofm_z < _ofmDepth) && (ky < subHeight) )
                                                {
                                                    _param->o = ofm_z;
                                                    _param->h = wy;
                                                    _param->w = wx;
                                                    _param->i = ifm_z;
                                                    int weight = int(buffer[WeightIndex(ofm_z, wy, wx, ifm_z)]);
                                                    *write = int16_t(_transform(_param, weight));
                                                }
                                                else
                                                {
                                                    *write = 0;
                                                }
                                                write++;
                                                if ( --count == 0 )
                                                {
                                                    // Save state
                                                    _ifmUBlockZ = ifmUBlockZ + 1;
                                                    _ofmUBlockZ = ofmUBlockZ;
                                                    _ifmUBlockInner = ifmUBlockInner;
                                                    _kernelElement = kernelElement;
                                                    _ofmUBlock = ofmUBlock;
                                                    _ifmUBlockOuter = ifmUBlockOuter;
                                                    _subKernelX = subKernelX;
                                                    _subKernelY = subKernelY;
                                                    _ifmBlockZ = ifmBlockZ;
                                                    _ofmBlockZ = ofmBlockZ;
                                                    // Return weights generated (less than requested count == EOS)
                                                    return int(intptr_t(write - output));
                                                }
                                            }
                                            _ifmUBlockZ = 0;
                                        }
                                        _ofmUBlockZ = 0;
                                    }
                                    _ifmUBlockInner = 0;
                                }
                                _kernelElement = 0;
                            }
                            _ofmUBlock = 0;
                        }
                        _ifmUBlockOuter = 0;
                    }
                    _subKernelX = 0;
                }
                _subKernelY = 0;
            }
            _ifmBlockZ = 0;
        }
        _ofmBlockZ = 0;
        return int(intptr_t(write - output));
    }
};

#endif /* ETHOSU_ENCODE_SUPPORT_H */
