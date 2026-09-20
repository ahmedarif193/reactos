/* SPDX-License-Identifier: GPL-3.0-or-later
 * Byte-addressed ID3D11DeviceContext::CopySubresourceRegion between buffers. */
#include <apitest.h>
#include <initguid.h>
#include <d3d11.h>

#define SOURCE_BYTES 256u
#define FILL_BYTE 0xeeu

static ID3D11Buffer *CreateTestBuffer(ID3D11Device *Device, UINT Bytes, D3D11_USAGE Usage, const BYTE *Initial)
{
    D3D11_BUFFER_DESC Desc = {};
    D3D11_SUBRESOURCE_DATA Data = {};
    ID3D11Buffer *Buffer = NULL;

    Desc.ByteWidth = Bytes;
    Desc.Usage = Usage;
    if (Usage == D3D11_USAGE_STAGING)
        Desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
    else
        Desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    Data.pSysMem = Initial;
    HRESULT Hr = Device->CreateBuffer(&Desc, Initial ? &Data : NULL, &Buffer);
    ok(SUCCEEDED(Hr), "CreateBuffer(%u bytes, usage %u): %#lx\n", Bytes, Usage, Hr);
    return Buffer;
}

/* Reads a buffer through a whole-resource copy, so a broken region copy
 * cannot also hide its own result. */
static BOOL ReadBuffer(ID3D11Device *Device, ID3D11DeviceContext *Context, ID3D11Buffer *Buffer, BYTE *Out, UINT Bytes)
{
    ID3D11Buffer *Staging = CreateTestBuffer(Device, Bytes, D3D11_USAGE_STAGING, NULL);
    D3D11_MAPPED_SUBRESOURCE Mapped = {};
    BOOL Read = FALSE;

    if (!Staging) return FALSE;
    Context->CopyResource(Staging, Buffer);
    HRESULT Hr = Context->Map(Staging, 0, D3D11_MAP_READ, 0, &Mapped);
    ok(SUCCEEDED(Hr) && Mapped.pData, "Map readback: %#lx\n", Hr);
    if (SUCCEEDED(Hr) && Mapped.pData)
    {
        memcpy(Out, Mapped.pData, Bytes);
        Context->Unmap(Staging, 0);
        Read = TRUE;
    }
    Staging->Release();
    return Read;
}

/* Expects Source[SourceOffset, SourceOffset + Count) at Offset and the
 * initial fill everywhere else. */
static void CheckBytes(const char *Case, const BYTE *Actual, UINT Bytes, UINT Offset, UINT Count, UINT SourceOffset)
{
    UINT Wrong = 0, First = 0;
    BYTE FirstActual = 0, FirstExpected = 0;

    for (UINT Index = 0; Index != Bytes; ++Index)
    {
        BYTE Expected = Index >= Offset && Index - Offset < Count
                ? static_cast<BYTE>(SourceOffset + Index - Offset) : static_cast<BYTE>(FILL_BYTE);
        if (Actual[Index] == Expected) continue;
        if (!Wrong++) { First = Index; FirstActual = Actual[Index]; FirstExpected = Expected; }
    }
    ok(!Wrong, "%s: %u wrong bytes, first at %u is %#x, expected %#x\n", Case, Wrong, First, FirstActual, FirstExpected);
}

START_TEST(buffer_copy)
{
    typedef HRESULT (WINAPI *CREATE_DEVICE)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE,
        UINT, const D3D_FEATURE_LEVEL *, UINT, UINT, ID3D11Device **,
        D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);
    HMODULE Module = LoadLibraryW(L"d3d11.dll");
    if (!Module) { skip("d3d11.dll is unavailable\n"); return; }
    CREATE_DEVICE Create = reinterpret_cast<CREATE_DEVICE>(GetProcAddress(Module, "D3D11CreateDevice"));
    if (!Create) { skip("D3D11CreateDevice is unavailable\n"); FreeLibrary(Module); return; }
    ID3D11Device *Device = NULL;
    ID3D11DeviceContext *Context = NULL;
    HRESULT Hr = Create(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0,
                        D3D11_SDK_VERSION, &Device, NULL, &Context);
    if (FAILED(Hr)) { skip("Hardware D3D11 unavailable: %#lx\n", Hr); FreeLibrary(Module); return; }

    BYTE Pattern[SOURCE_BYTES], Fill[SOURCE_BYTES * 2], Actual[SOURCE_BYTES * 2];
    for (UINT Index = 0; Index != SOURCE_BYTES; ++Index) Pattern[Index] = static_cast<BYTE>(Index);
    memset(Fill, FILL_BYTE, sizeof(Fill));
    ID3D11Buffer *Source = CreateTestBuffer(Device, SOURCE_BYTES, D3D11_USAGE_DEFAULT, Pattern);

    static const struct
    {
        const char *Name;
        UINT DestinationBytes, Offset;
        BOOL UseBox;
        D3D11_BOX Box;
        UINT ExpectedCount, ExpectedSource;
    } Cases[] =
    {
        {"interior range", SOURCE_BYTES, 100, TRUE, {16, 0, 0, 48, 1, 1}, 32, 16},
        {"first byte", SOURCE_BYTES, 0, TRUE, {255, 0, 0, 256, 1, 1}, 1, 255},
        {"last byte", SOURCE_BYTES, 255, TRUE, {0, 0, 0, 1, 1, 1}, 1, 0},
        {"whole source without a box", SOURCE_BYTES, 0, FALSE, {}, SOURCE_BYTES, 0},
        {"whole source with a box", SOURCE_BYTES, 0, TRUE, {0, 0, 0, SOURCE_BYTES, 1, 1}, SOURCE_BYTES, 0},
        {"growth into a larger buffer", SOURCE_BYTES * 2, 0, FALSE, {}, SOURCE_BYTES, 0},
        {"offset into a larger buffer", SOURCE_BYTES * 2, SOURCE_BYTES, FALSE, {}, SOURCE_BYTES, 0},
        {"unaligned range", SOURCE_BYTES, 3, TRUE, {5, 0, 0, 18, 1, 1}, 13, 5},
        {"empty box", SOURCE_BYTES, 64, TRUE, {32, 0, 0, 32, 1, 1}, 0, 0},
    };

    for (UINT Index = 0; Source && Index != ARRAYSIZE(Cases); ++Index)
    {
        ID3D11Buffer *Destination = CreateTestBuffer(Device, Cases[Index].DestinationBytes, D3D11_USAGE_DEFAULT, Fill);
        if (!Destination) continue;
        Context->CopySubresourceRegion(Destination, 0, Cases[Index].Offset, 0, 0, Source, 0,
                                       Cases[Index].UseBox ? &Cases[Index].Box : NULL);
        if (ReadBuffer(Device, Context, Destination, Actual, Cases[Index].DestinationBytes))
            CheckBytes(Cases[Index].Name, Actual, Cases[Index].DestinationBytes, Cases[Index].Offset,
                       Cases[Index].ExpectedCount, Cases[Index].ExpectedSource);
        Destination->Release();
    }

    /* A staging destination is read back without an intervening copy. */
    ID3D11Buffer *Staging = Source ? CreateTestBuffer(Device, SOURCE_BYTES, D3D11_USAGE_STAGING, Fill) : NULL;
    if (Staging)
    {
        D3D11_BOX Box = {40, 0, 0, 72, 1, 1};
        D3D11_MAPPED_SUBRESOURCE Mapped = {};
        Context->CopySubresourceRegion(Staging, 0, 8, 0, 0, Source, 0, &Box);
        Hr = Context->Map(Staging, 0, D3D11_MAP_READ, 0, &Mapped);
        ok(SUCCEEDED(Hr) && Mapped.pData, "Map staging destination: %#lx\n", Hr);
        if (SUCCEEDED(Hr) && Mapped.pData)
        {
            CheckBytes("staging destination", static_cast<const BYTE *>(Mapped.pData), SOURCE_BYTES, 8, 32, 40);
            Context->Unmap(Staging, 0);
        }
        Staging->Release();
    }

    /* Successive ranges accumulate; a later copy must not disturb an earlier one. */
    ID3D11Buffer *Destination = Source ? CreateTestBuffer(Device, SOURCE_BYTES, D3D11_USAGE_DEFAULT, Fill) : NULL;
    if (Destination)
    {
        D3D11_BOX Low = {0, 0, 0, 16, 1, 1}, High = {16, 0, 0, 32, 1, 1};
        Context->CopySubresourceRegion(Destination, 0, 0, 0, 0, Source, 0, &Low);
        Context->CopySubresourceRegion(Destination, 0, 16, 0, 0, Source, 0, &High);
        if (ReadBuffer(Device, Context, Destination, Actual, SOURCE_BYTES))
            CheckBytes("adjacent ranges", Actual, SOURCE_BYTES, 0, 32, 0);
        Destination->Release();
    }

    if (Source) Source->Release();
    Context->Release();
    Device->Release();
    FreeLibrary(Module);
}
