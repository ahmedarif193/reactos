/*
 * PROJECT:     ReactOS Raspberry Pi 3 VC4 WDDM miniport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Linux VC4 validator compatibility contract for ReactOS
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#pragma once

#include "rpi3vc4.h"
#include "vc4_packet.h"
#include "vc4_qpu_defines.h"
#include <stdbool.h>
#include <stdint.h>

#define EINVAL  22
#define ENOMEM  12
#define GFP_KERNEL 0
#define UINT_MAX MAXUINT
#define BIT(_Bit) (1u << (_Bit))
#define ARRAY_SIZE(_Array) RTL_NUMBER_OF(_Array)
#define DIV_ROUND_UP(_Value, _Divisor) (((_Value) + (_Divisor) - 1) / (_Divisor))
#define MAX(_Left, _Right) max((_Left), (_Right))
#define align(_Value, _Alignment) ALIGN_UP_BY((_Value), (_Alignment))
#define roundup(_Value, _Alignment) align((_Value), (_Alignment))
#define round_up(_Value, _Alignment) align((_Value), (_Alignment))
#define BUG_ON(_Condition) ASSERT(!(_Condition))

#define DRM_INFO(...) DPRINT(__VA_ARGS__)
#define DRM_ERROR(...) DPRINT1("RPI3VC4: " __VA_ARGS__)
PVOID
Rpi3Vc4ValidatorAllocate(
    _In_ SIZE_T Size,
    _In_ BOOLEAN Zero);

PVOID
Rpi3Vc4ValidatorCallocate(
    _In_ SIZE_T Count,
    _In_ SIZE_T Size);

PVOID
Rpi3Vc4ValidatorReallocate(
    _In_opt_ PVOID Buffer,
    _In_ SIZE_T Size);

VOID
Rpi3Vc4ValidatorFree(
    _In_opt_ PVOID Buffer);

#define kmalloc(_Size, _Flags) Rpi3Vc4ValidatorAllocate((_Size), FALSE)
#define kcalloc(_Count, _Size, _Flags) \
    Rpi3Vc4ValidatorCallocate((_Count), (_Size))
#define krealloc(_Buffer, _Size, _Flags) \
    Rpi3Vc4ValidatorReallocate((_Buffer), (_Size))
#define kfree(_Buffer) Rpi3Vc4ValidatorFree((_Buffer))

#define BITMAP_WORDBITS (sizeof(unsigned long) * 8)
#define BITS_TO_LONGS(_Bits) \
    (roundup((_Bits), BITMAP_WORDBITS) / BITMAP_WORDBITS)

static __inline bool
test_bit(unsigned int Bit, const unsigned long *Address)
{
    return (Address[Bit / BITMAP_WORDBITS] &
            (1ul << (Bit % BITMAP_WORDBITS))) != 0;
}

static __inline bool
set_bit(unsigned int Bit, unsigned long *Address)
{
    unsigned long Mask = 1ul << (Bit % BITMAP_WORDBITS);
    unsigned long *Word = &Address[Bit / BITMAP_WORDBITS];
    bool WasSet = (*Word & Mask) != 0;

    *Word |= Mask;
    return WasSet;
}

typedef UCHAR u8;
typedef USHORT u16;
typedef ULONG u32;

struct drm_vc4_submit_rcl_surface
{
    uint32_t hindex;
    uint32_t offset;
    uint16_t bits;
    uint16_t flags;
};

#define VC4_SUBMIT_RCL_SURFACE_READ_IS_FULL_RES \
    RPI3VC4_SUBMIT_RCL_SURFACE_READ_IS_FULL_RES
#define VC4_SUBMIT_CL_USE_CLEAR_COLOR \
    RPI3VC4_SUBMIT_CL_USE_CLEAR_COLOR
#define VC4_SUBMIT_CL_FIXED_RCL_ORDER \
    RPI3VC4_SUBMIT_CL_FIXED_RCL_ORDER
#define VC4_SUBMIT_CL_RCL_ORDER_INCREASING_X \
    RPI3VC4_SUBMIT_CL_RCL_ORDER_INCREASING_X
#define VC4_SUBMIT_CL_RCL_ORDER_INCREASING_Y \
    RPI3VC4_SUBMIT_CL_RCL_ORDER_INCREASING_Y

struct drm_vc4_submit_cl
{
    uint64_t bin_cl;
    uint64_t shader_rec;
    uint64_t uniforms;
    uint64_t bo_handles;
    uint32_t bin_cl_size;
    uint32_t shader_rec_size;
    uint32_t shader_rec_count;
    uint32_t uniforms_size;
    uint32_t bo_handle_count;
    uint16_t width;
    uint16_t height;
    uint8_t min_x_tile;
    uint8_t min_y_tile;
    uint8_t max_x_tile;
    uint8_t max_y_tile;
    struct drm_vc4_submit_rcl_surface color_read;
    struct drm_vc4_submit_rcl_surface color_write;
    struct drm_vc4_submit_rcl_surface zs_read;
    struct drm_vc4_submit_rcl_surface zs_write;
    struct drm_vc4_submit_rcl_surface msaa_color_write;
    struct drm_vc4_submit_rcl_surface msaa_zs_write;
    uint32_t clear_color[2];
    uint32_t clear_z;
    uint8_t clear_s;
    uint32_t pad : 24;
    uint32_t flags;
    uint64_t seqno;
    uint32_t perfmonid;
    uint32_t in_sync;
    uint32_t out_sync;
    uint32_t pad2;
};

struct list_head
{
    struct list_head *Next;
    struct list_head *Previous;
};

static __inline VOID
INIT_LIST_HEAD(struct list_head *Head)
{
    Head->Next = Head;
    Head->Previous = Head;
}

static __inline VOID
list_addtail(struct list_head *Entry, struct list_head *Head)
{
    Entry->Previous = Head->Previous;
    Entry->Next = Head;
    Head->Previous->Next = Entry;
    Head->Previous = Entry;
}

struct vc4_validated_shader_info;
struct vc4_validation_context;

struct drm_device
{
    struct vc4_validation_context *Validation;
};

struct drm_gem_object
{
    size_t size;
    struct drm_device *dev;
};

struct drm_gem_cma_object
{
    struct drm_gem_object base;
    uint32_t paddr;
    void *vaddr;
};

struct drm_vc4_bo
{
    struct drm_gem_cma_object base;
    struct vc4_validated_shader_info *validated_shader;
    struct list_head unref_head;
};

static __inline struct drm_vc4_bo *
to_vc4_bo(struct drm_gem_object *Object)
{
    return (struct drm_vc4_bo *)Object;
}

struct vc4_exec_info
{
    uint64_t seqno;
    struct drm_vc4_submit_cl *args;
    struct drm_gem_cma_object **bo;
    uint32_t bo_count;
    struct list_head unref_list;
    uint32_t bo_index[2];
    struct drm_gem_cma_object *exec_bo;
    struct vc4_shader_state
    {
        uint32_t addr;
        uint32_t max_index;
    } *shader_state;
    uint32_t shader_state_size;
    uint32_t shader_state_count;
    bool found_tile_binning_mode_config_packet;
    bool found_start_tile_binning_packet;
    bool found_increment_semaphore_packet;
    bool found_flush;
    uint8_t bin_tiles_x;
    uint8_t bin_tiles_y;
    struct drm_gem_cma_object *tile_bo;
    uint32_t tile_alloc_offset;
    uint32_t tile_width;
    uint32_t tile_height;
    uint32_t ct0ca;
    uint32_t ct0ea;
    uint32_t ct1ca;
    uint32_t ct1ea;
    void *bin_u;
    void *shader_rec_u;
    void *shader_rec_v;
    uint32_t shader_rec_p;
    uint32_t shader_rec_size;
    void *uniforms_u;
    void *uniforms_v;
    uint32_t uniforms_p;
    uint32_t uniforms_size;
};

struct vc4_texture_sample_info
{
    bool is_direct;
    uint32_t p_offset[4];
};

struct vc4_validated_shader_info
{
    uint32_t uniforms_size;
    uint32_t uniforms_src_size;
    uint32_t num_texture_samples;
    struct vc4_texture_sample_info *texture_samples;
    uint32_t num_uniform_addr_offsets;
    uint32_t *uniform_addr_offsets;
    bool is_threaded;
};

struct drm_gem_cma_object *
drm_gem_cma_create(
    _In_ struct drm_device *Device,
    _In_ size_t Size);

int
vc4_validate_bin_cl(
    struct drm_device *Device,
    void *Validated,
    void *Unvalidated,
    struct vc4_exec_info *Exec);

int
vc4_validate_shader_recs(
    struct drm_device *Device,
    struct vc4_exec_info *Exec);

struct vc4_validated_shader_info *
vc4_validate_shader(struct drm_gem_cma_object *ShaderObject);

struct drm_gem_cma_object *
vc4_use_bo(struct vc4_exec_info *Exec, uint32_t HandleIndex);

int
vc4_get_rcl(struct drm_device *Device, struct vc4_exec_info *Exec);

bool
vc4_check_tex_size(
    struct vc4_exec_info *Exec,
    struct drm_gem_cma_object *FrameBuffer,
    uint32_t Offset,
    uint8_t TilingFormat,
    uint32_t Width,
    uint32_t Height,
    uint8_t BytesPerPixel);
