/*
 * PROJECT:     ReactOS Direct3D 11 Runtime
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Internal header for D3D11 implementation
 * COPYRIGHT:   Copyright 2026 ReactOS Project
 */

#ifndef _D3D11_PRIVATE_H_
#define _D3D11_PRIVATE_H_

#define WIN32_NO_STATUS
#define _INC_WINDOWS
#define COM_NO_WINDOWS_H
#define COBJMACROS
#define INITGUID

#include <windef.h>
#include <winbase.h>
#include <wingdi.h>
#include <winuser.h>
#include <objbase.h>

#include <d3d11.h>
#include <dxgi.h>

#include <wine/debug.h>

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif

/*
 * Maximum limits
 */
#define D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT  8
#define D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT_  16
#define D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT_ 128
#define D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT_ 14
#define D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT_ 32
#define D3D11_SO_BUFFER_SLOT_COUNT_ 4
#define D3D11_SO_STREAM_COUNT_ 4
#define D3D11_PS_CS_UAV_REGISTER_COUNT_ 8
#define D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE_ 16

/*
 * Forward declarations
 */
struct d3d11_device;
struct d3d11_immediate_context;

/*
 * ID3D11DeviceChild - base for all device children
 */
typedef struct d3d11_device_child_info
{
    struct d3d11_device *device;
} d3d11_device_child_info;

/*
 * ID3D11BlendState implementation
 */
typedef struct d3d11_blend_state
{
    ID3D11BlendState ID3D11BlendState_iface;
    LONG refcount;
    d3d11_device_child_info child;
    D3D11_BLEND_DESC desc;
} d3d11_blend_state;

static inline struct d3d11_blend_state *impl_from_ID3D11BlendState(ID3D11BlendState *iface)
{
    return CONTAINING_RECORD(iface, struct d3d11_blend_state, ID3D11BlendState_iface);
}

/*
 * ID3D11DepthStencilState implementation
 */
typedef struct d3d11_depth_stencil_state
{
    ID3D11DepthStencilState ID3D11DepthStencilState_iface;
    LONG refcount;
    d3d11_device_child_info child;
    D3D11_DEPTH_STENCIL_DESC desc;
} d3d11_depth_stencil_state;

static inline struct d3d11_depth_stencil_state *impl_from_ID3D11DepthStencilState(ID3D11DepthStencilState *iface)
{
    return CONTAINING_RECORD(iface, struct d3d11_depth_stencil_state, ID3D11DepthStencilState_iface);
}

/*
 * ID3D11RasterizerState implementation
 */
typedef struct d3d11_rasterizer_state
{
    ID3D11RasterizerState ID3D11RasterizerState_iface;
    LONG refcount;
    d3d11_device_child_info child;
    D3D11_RASTERIZER_DESC desc;
} d3d11_rasterizer_state;

static inline struct d3d11_rasterizer_state *impl_from_ID3D11RasterizerState(ID3D11RasterizerState *iface)
{
    return CONTAINING_RECORD(iface, struct d3d11_rasterizer_state, ID3D11RasterizerState_iface);
}

/*
 * ID3D11SamplerState implementation
 */
typedef struct d3d11_sampler_state
{
    ID3D11SamplerState ID3D11SamplerState_iface;
    LONG refcount;
    d3d11_device_child_info child;
    D3D11_SAMPLER_DESC desc;
} d3d11_sampler_state;

static inline struct d3d11_sampler_state *impl_from_ID3D11SamplerState(ID3D11SamplerState *iface)
{
    return CONTAINING_RECORD(iface, struct d3d11_sampler_state, ID3D11SamplerState_iface);
}

/*
 * ID3D11Buffer implementation
 */
typedef struct d3d11_buffer
{
    ID3D11Buffer ID3D11Buffer_iface;
    LONG refcount;
    d3d11_device_child_info child;
    D3D11_BUFFER_DESC desc;
    void *sysmem;
    SIZE_T sysmem_size;
    UINT eviction_priority;
} d3d11_buffer;

static inline struct d3d11_buffer *impl_from_ID3D11Buffer(ID3D11Buffer *iface)
{
    return CONTAINING_RECORD(iface, struct d3d11_buffer, ID3D11Buffer_iface);
}

/*
 * ID3D11Texture1D implementation
 */
typedef struct d3d11_texture1d
{
    ID3D11Texture1D ID3D11Texture1D_iface;
    LONG refcount;
    d3d11_device_child_info child;
    D3D11_TEXTURE1D_DESC desc;
    void *sysmem;
    SIZE_T sysmem_size;
    UINT row_pitch;
    UINT eviction_priority;
} d3d11_texture1d;

static inline struct d3d11_texture1d *impl_from_ID3D11Texture1D(ID3D11Texture1D *iface)
{
    return CONTAINING_RECORD(iface, struct d3d11_texture1d, ID3D11Texture1D_iface);
}

/*
 * ID3D11Texture2D implementation
 */
typedef struct d3d11_texture2d
{
    ID3D11Texture2D ID3D11Texture2D_iface;
    IDXGISurface IDXGISurface_iface;
    LONG refcount;
    d3d11_device_child_info child;
    D3D11_TEXTURE2D_DESC desc;
    void *sysmem;
    SIZE_T sysmem_size;
    UINT row_pitch;
    UINT eviction_priority;
} d3d11_texture2d;

static inline struct d3d11_texture2d *impl_from_ID3D11Texture2D(ID3D11Texture2D *iface)
{
    return CONTAINING_RECORD(iface, struct d3d11_texture2d, ID3D11Texture2D_iface);
}

static inline struct d3d11_texture2d *impl_from_IDXGISurface(IDXGISurface *iface)
{
    return CONTAINING_RECORD(iface, struct d3d11_texture2d, IDXGISurface_iface);
}

/*
 * ID3D11Texture3D implementation
 */
typedef struct d3d11_texture3d
{
    ID3D11Texture3D ID3D11Texture3D_iface;
    LONG refcount;
    d3d11_device_child_info child;
    D3D11_TEXTURE3D_DESC desc;
    void *sysmem;
    SIZE_T sysmem_size;
    UINT row_pitch;
    UINT slice_pitch;
    UINT eviction_priority;
} d3d11_texture3d;

static inline struct d3d11_texture3d *impl_from_ID3D11Texture3D(ID3D11Texture3D *iface)
{
    return CONTAINING_RECORD(iface, struct d3d11_texture3d, ID3D11Texture3D_iface);
}

/*
 * ID3D11ShaderResourceView implementation
 */
typedef struct d3d11_shader_resource_view
{
    ID3D11ShaderResourceView ID3D11ShaderResourceView_iface;
    LONG refcount;
    d3d11_device_child_info child;
    ID3D11Resource *resource;
    D3D11_SHADER_RESOURCE_VIEW_DESC desc;
} d3d11_shader_resource_view;

static inline struct d3d11_shader_resource_view *impl_from_ID3D11ShaderResourceView(ID3D11ShaderResourceView *iface)
{
    return CONTAINING_RECORD(iface, struct d3d11_shader_resource_view, ID3D11ShaderResourceView_iface);
}

/*
 * ID3D11RenderTargetView implementation
 */
typedef struct d3d11_render_target_view
{
    ID3D11RenderTargetView ID3D11RenderTargetView_iface;
    LONG refcount;
    d3d11_device_child_info child;
    ID3D11Resource *resource;
    D3D11_RENDER_TARGET_VIEW_DESC desc;
} d3d11_render_target_view;

static inline struct d3d11_render_target_view *impl_from_ID3D11RenderTargetView(ID3D11RenderTargetView *iface)
{
    return CONTAINING_RECORD(iface, struct d3d11_render_target_view, ID3D11RenderTargetView_iface);
}

/*
 * ID3D11DepthStencilView implementation
 */
typedef struct d3d11_depth_stencil_view
{
    ID3D11DepthStencilView ID3D11DepthStencilView_iface;
    LONG refcount;
    d3d11_device_child_info child;
    ID3D11Resource *resource;
    D3D11_DEPTH_STENCIL_VIEW_DESC desc;
} d3d11_depth_stencil_view;

static inline struct d3d11_depth_stencil_view *impl_from_ID3D11DepthStencilView(ID3D11DepthStencilView *iface)
{
    return CONTAINING_RECORD(iface, struct d3d11_depth_stencil_view, ID3D11DepthStencilView_iface);
}

/*
 * ID3D11UnorderedAccessView implementation
 */
typedef struct d3d11_unordered_access_view
{
    ID3D11UnorderedAccessView ID3D11UnorderedAccessView_iface;
    LONG refcount;
    d3d11_device_child_info child;
    ID3D11Resource *resource;
    D3D11_UNORDERED_ACCESS_VIEW_DESC desc;
} d3d11_unordered_access_view;

static inline struct d3d11_unordered_access_view *impl_from_ID3D11UnorderedAccessView(ID3D11UnorderedAccessView *iface)
{
    return CONTAINING_RECORD(iface, struct d3d11_unordered_access_view, ID3D11UnorderedAccessView_iface);
}

/*
 * Shader objects
 */
typedef struct d3d11_vertex_shader
{
    ID3D11VertexShader ID3D11VertexShader_iface;
    LONG refcount;
    d3d11_device_child_info child;
    void *bytecode;
    SIZE_T bytecode_length;
} d3d11_vertex_shader;

static inline struct d3d11_vertex_shader *impl_from_ID3D11VertexShader(ID3D11VertexShader *iface)
{
    return CONTAINING_RECORD(iface, struct d3d11_vertex_shader, ID3D11VertexShader_iface);
}

typedef struct d3d11_pixel_shader
{
    ID3D11PixelShader ID3D11PixelShader_iface;
    LONG refcount;
    d3d11_device_child_info child;
    void *bytecode;
    SIZE_T bytecode_length;
} d3d11_pixel_shader;

static inline struct d3d11_pixel_shader *impl_from_ID3D11PixelShader(ID3D11PixelShader *iface)
{
    return CONTAINING_RECORD(iface, struct d3d11_pixel_shader, ID3D11PixelShader_iface);
}

typedef struct d3d11_geometry_shader
{
    ID3D11GeometryShader ID3D11GeometryShader_iface;
    LONG refcount;
    d3d11_device_child_info child;
    void *bytecode;
    SIZE_T bytecode_length;
} d3d11_geometry_shader;

static inline struct d3d11_geometry_shader *impl_from_ID3D11GeometryShader(ID3D11GeometryShader *iface)
{
    return CONTAINING_RECORD(iface, struct d3d11_geometry_shader, ID3D11GeometryShader_iface);
}

typedef struct d3d11_hull_shader
{
    ID3D11HullShader ID3D11HullShader_iface;
    LONG refcount;
    d3d11_device_child_info child;
    void *bytecode;
    SIZE_T bytecode_length;
} d3d11_hull_shader;

static inline struct d3d11_hull_shader *impl_from_ID3D11HullShader(ID3D11HullShader *iface)
{
    return CONTAINING_RECORD(iface, struct d3d11_hull_shader, ID3D11HullShader_iface);
}

typedef struct d3d11_domain_shader
{
    ID3D11DomainShader ID3D11DomainShader_iface;
    LONG refcount;
    d3d11_device_child_info child;
    void *bytecode;
    SIZE_T bytecode_length;
} d3d11_domain_shader;

static inline struct d3d11_domain_shader *impl_from_ID3D11DomainShader(ID3D11DomainShader *iface)
{
    return CONTAINING_RECORD(iface, struct d3d11_domain_shader, ID3D11DomainShader_iface);
}

typedef struct d3d11_compute_shader
{
    ID3D11ComputeShader ID3D11ComputeShader_iface;
    LONG refcount;
    d3d11_device_child_info child;
    void *bytecode;
    SIZE_T bytecode_length;
} d3d11_compute_shader;

static inline struct d3d11_compute_shader *impl_from_ID3D11ComputeShader(ID3D11ComputeShader *iface)
{
    return CONTAINING_RECORD(iface, struct d3d11_compute_shader, ID3D11ComputeShader_iface);
}

/*
 * ID3D11InputLayout implementation
 */
typedef struct d3d11_input_layout
{
    ID3D11InputLayout ID3D11InputLayout_iface;
    LONG refcount;
    d3d11_device_child_info child;
    D3D11_INPUT_ELEMENT_DESC *elements;
    UINT element_count;
} d3d11_input_layout;

static inline struct d3d11_input_layout *impl_from_ID3D11InputLayout(ID3D11InputLayout *iface)
{
    return CONTAINING_RECORD(iface, struct d3d11_input_layout, ID3D11InputLayout_iface);
}

/*
 * ID3D11Query implementation
 */
typedef struct d3d11_query
{
    ID3D11Query ID3D11Query_iface;
    LONG refcount;
    d3d11_device_child_info child;
    D3D11_QUERY_DESC desc;
    BOOL is_predicate;
} d3d11_query;

static inline struct d3d11_query *impl_from_ID3D11Query(ID3D11Query *iface)
{
    return CONTAINING_RECORD(iface, struct d3d11_query, ID3D11Query_iface);
}

/*
 * ID3D11DeviceContext (immediate) implementation
 */
typedef struct d3d11_immediate_context
{
    ID3D11DeviceContext ID3D11DeviceContext_iface;
    LONG refcount;
    struct d3d11_device *device;

    /* IA state */
    D3D11_PRIMITIVE_TOPOLOGY topology;
    ID3D11InputLayout *input_layout;
    ID3D11Buffer *vertex_buffers[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT_];
    UINT vertex_strides[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT_];
    UINT vertex_offsets[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT_];
    ID3D11Buffer *index_buffer;
    DXGI_FORMAT index_format;
    UINT index_offset;

    /* VS state */
    ID3D11VertexShader *vs;
    ID3D11Buffer *vs_cbs[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT_];
    ID3D11ShaderResourceView *vs_srvs[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT_];

    /* PS state */
    ID3D11PixelShader *ps;
    ID3D11Buffer *ps_cbs[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT_];
    ID3D11ShaderResourceView *ps_srvs[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT_];
    ID3D11SamplerState *ps_samplers[D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT_];

    /* GS state */
    ID3D11GeometryShader *gs;

    /* HS state */
    ID3D11HullShader *hs;

    /* DS state */
    ID3D11DomainShader *ds;

    /* CS state */
    ID3D11ComputeShader *cs;

    /* RS state */
    ID3D11RasterizerState *rasterizer_state;
    UINT viewport_count;
    D3D11_VIEWPORT viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE_];
    UINT scissor_count;
    D3D11_RECT scissor_rects[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE_];

    /* OM state */
    ID3D11RenderTargetView *render_targets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
    ID3D11DepthStencilView *depth_stencil;
    ID3D11BlendState *blend_state;
    FLOAT blend_factor[4];
    UINT sample_mask;
    ID3D11DepthStencilState *depth_stencil_state;
    UINT stencil_ref;
} d3d11_immediate_context;

static inline struct d3d11_immediate_context *impl_from_ID3D11DeviceContext(ID3D11DeviceContext *iface)
{
    return CONTAINING_RECORD(iface, struct d3d11_immediate_context, ID3D11DeviceContext_iface);
}

/*
 * ID3D11Device implementation
 */
typedef struct d3d11_device
{
    ID3D11Device ID3D11Device_iface;
    IDXGIDevice IDXGIDevice_iface;
    LONG refcount;

    D3D_FEATURE_LEVEL feature_level;
    UINT creation_flags;
    HRESULT device_removed_reason;

    d3d11_immediate_context immediate_context;

    IDXGIAdapter *adapter;
    IDXGIFactory *factory;
} d3d11_device;

static inline struct d3d11_device *impl_from_ID3D11Device(ID3D11Device *iface)
{
    return CONTAINING_RECORD(iface, struct d3d11_device, ID3D11Device_iface);
}

static inline struct d3d11_device *impl_from_IDXGIDevice(IDXGIDevice *iface)
{
    return CONTAINING_RECORD(iface, struct d3d11_device, IDXGIDevice_iface);
}

static inline void d3d11_device_child_get_device(struct d3d11_device *device, ID3D11Device **out)
{
    if (!out)
        return;

    *out = NULL;
    if (!device)
        return;

    *out = &device->ID3D11Device_iface;
    ID3D11Device_AddRef(*out);
}

static inline HRESULT d3d11_device_child_init(d3d11_device_child_info *child,
        struct d3d11_device *device)
{
    if (!child || !device)
        return E_INVALIDARG;

    child->device = device;
    ID3D11Device_AddRef(&device->ID3D11Device_iface);
    return S_OK;
}

static inline void d3d11_device_child_cleanup(d3d11_device_child_info *child)
{
    struct d3d11_device *device;

    if (!child || !child->device)
        return;

    device = child->device;
    child->device = NULL;
    ID3D11Device_Release(&device->ID3D11Device_iface);
}

static inline BOOL d3d11_checked_mul_size(SIZE_T a, SIZE_T b, SIZE_T *out)
{
    if (a && b > ~(SIZE_T)0 / a)
        return FALSE;

    *out = a * b;
    return TRUE;
}

static inline void d3d11_set_unknown(IUnknown **slot, IUnknown *object)
{
    if (object)
        IUnknown_AddRef(object);
    if (*slot)
        IUnknown_Release(*slot);
    *slot = object;
}

static inline void d3d11_get_unknown(IUnknown *object, IUnknown **out)
{
    if (!out)
        return;

    *out = object;
    if (object)
        IUnknown_AddRef(object);
}

/*
 * Function prototypes - device.c
 */
HRESULT d3d11_device_create(IDXGIAdapter *adapter, D3D_DRIVER_TYPE driver_type,
        UINT flags, const D3D_FEATURE_LEVEL *feature_levels, UINT levels,
        struct d3d11_device **out);

/*
 * Function prototypes - context.c
 */
void d3d11_immediate_context_init(struct d3d11_immediate_context *context,
        struct d3d11_device *device);

/*
 * Function prototypes - state.c
 */
HRESULT d3d11_blend_state_create(struct d3d11_device *device,
        const D3D11_BLEND_DESC *desc, struct d3d11_blend_state **out);
HRESULT d3d11_depth_stencil_state_create(struct d3d11_device *device,
        const D3D11_DEPTH_STENCIL_DESC *desc, struct d3d11_depth_stencil_state **out);
HRESULT d3d11_rasterizer_state_create(struct d3d11_device *device,
        const D3D11_RASTERIZER_DESC *desc, struct d3d11_rasterizer_state **out);
HRESULT d3d11_sampler_state_create(struct d3d11_device *device,
        const D3D11_SAMPLER_DESC *desc, struct d3d11_sampler_state **out);

/*
 * Function prototypes - buffer.c
 */
HRESULT d3d11_buffer_create(struct d3d11_device *device,
        const D3D11_BUFFER_DESC *desc, const D3D11_SUBRESOURCE_DATA *data,
        struct d3d11_buffer **out);

/*
 * Function prototypes - texture.c
 */
HRESULT d3d11_texture1d_create(struct d3d11_device *device,
        const D3D11_TEXTURE1D_DESC *desc, const D3D11_SUBRESOURCE_DATA *data,
        struct d3d11_texture1d **out);
HRESULT d3d11_texture2d_create(struct d3d11_device *device,
        const D3D11_TEXTURE2D_DESC *desc, const D3D11_SUBRESOURCE_DATA *data,
        struct d3d11_texture2d **out);
HRESULT d3d11_texture3d_create(struct d3d11_device *device,
        const D3D11_TEXTURE3D_DESC *desc, const D3D11_SUBRESOURCE_DATA *data,
        struct d3d11_texture3d **out);
HRESULT d3d11_texture_resource_map(ID3D11Resource *resource, UINT subresource,
        D3D11_MAP type, UINT flags, D3D11_MAPPED_SUBRESOURCE *mapped);
void d3d11_texture_resource_unmap(ID3D11Resource *resource, UINT subresource);
HRESULT d3d11_texture_resource_update(ID3D11Resource *resource, UINT subresource,
        const D3D11_BOX *box, const void *data, UINT row_pitch, UINT depth_pitch);

/*
 * Function prototypes - view.c
 */
HRESULT d3d11_shader_resource_view_create(struct d3d11_device *device,
        ID3D11Resource *resource, const D3D11_SHADER_RESOURCE_VIEW_DESC *desc,
        struct d3d11_shader_resource_view **out);
HRESULT d3d11_render_target_view_create(struct d3d11_device *device,
        ID3D11Resource *resource, const D3D11_RENDER_TARGET_VIEW_DESC *desc,
        struct d3d11_render_target_view **out);
HRESULT d3d11_depth_stencil_view_create(struct d3d11_device *device,
        ID3D11Resource *resource, const D3D11_DEPTH_STENCIL_VIEW_DESC *desc,
        struct d3d11_depth_stencil_view **out);
HRESULT d3d11_unordered_access_view_create(struct d3d11_device *device,
        ID3D11Resource *resource, const D3D11_UNORDERED_ACCESS_VIEW_DESC *desc,
        struct d3d11_unordered_access_view **out);

/*
 * Function prototypes - shader.c
 */
HRESULT d3d11_vertex_shader_create(struct d3d11_device *device,
        const void *bytecode, SIZE_T length, struct d3d11_vertex_shader **out);
HRESULT d3d11_pixel_shader_create(struct d3d11_device *device,
        const void *bytecode, SIZE_T length, struct d3d11_pixel_shader **out);
HRESULT d3d11_geometry_shader_create(struct d3d11_device *device,
        const void *bytecode, SIZE_T length, struct d3d11_geometry_shader **out);
HRESULT d3d11_hull_shader_create(struct d3d11_device *device,
        const void *bytecode, SIZE_T length, struct d3d11_hull_shader **out);
HRESULT d3d11_domain_shader_create(struct d3d11_device *device,
        const void *bytecode, SIZE_T length, struct d3d11_domain_shader **out);
HRESULT d3d11_compute_shader_create(struct d3d11_device *device,
        const void *bytecode, SIZE_T length, struct d3d11_compute_shader **out);

/*
 * Function prototypes - inputlayout.c
 */
HRESULT d3d11_input_layout_create(struct d3d11_device *device,
        const D3D11_INPUT_ELEMENT_DESC *descs, UINT count,
        const void *bytecode, SIZE_T bytecode_length,
        struct d3d11_input_layout **out);

/*
 * Function prototypes - query.c
 */
HRESULT d3d11_query_create(struct d3d11_device *device,
        const D3D11_QUERY_DESC *desc, BOOL is_predicate,
        struct d3d11_query **out);

/*
 * Utility functions - utils.c
 */
UINT d3d11_dxgi_format_bpp(DXGI_FORMAT format);

#endif /* _D3D11_PRIVATE_H_ */
