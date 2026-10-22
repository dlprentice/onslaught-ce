// SPDX-License-Identifier: GPL-3.0-only
// ID3DXBuffer: a reference-counted block of bytes, as D3DXCreateBuffer and D3DXAssembleShader return.
#include <d3dx9.h>
#include <new>
#include <string.h>

namespace {

// IUnknown and ID3DXBuffer, compared bytewise so no GUID symbol has to be linked.
const GUID kIUnknown = {0x00000000, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
const GUID kID3DXBuffer = {0x932e6a7e, 0xc68e, 0x45dd, {0xa7, 0xbf, 0x53, 0xd1, 0x9c, 0x86, 0xdb, 0x1f}};

struct Buffer final : public ID3DXBuffer
{
    ULONG mRefs;
    DWORD mSize;
    unsigned char *mData;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv)
    {
        if (!ppv)
            return E_POINTER;
        if (memcmp(&riid, &kIUnknown, sizeof(GUID)) == 0 || memcmp(&riid, &kID3DXBuffer, sizeof(GUID)) == 0)
        {
            *ppv = this;
            AddRef();
            return S_OK;
        }
        *ppv = NULL;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() { return ++mRefs; }
    ULONG STDMETHODCALLTYPE Release()
    {
        ULONG refs = --mRefs;
        if (refs == 0)
        {
            delete[] mData;
            delete this;
        }
        return refs;
    }
    LPVOID STDMETHODCALLTYPE GetBufferPointer() { return mData; }
    DWORD STDMETHODCALLTYPE GetBufferSize() { return mSize; }
};

} // namespace

extern "C" HRESULT WINAPI D3DXCreateBuffer(DWORD NumBytes, LPD3DXBUFFER *ppBuffer)
{
    if (!ppBuffer)
        return D3DERR_INVALIDCALL;
    Buffer *b = new (std::nothrow) Buffer;
    if (!b)
        return E_OUTOFMEMORY;
    b->mRefs = 1;
    b->mSize = NumBytes;
    b->mData = new (std::nothrow) unsigned char[NumBytes ? NumBytes : 1];
    if (!b->mData)
    {
        delete b;
        return E_OUTOFMEMORY;
    }
    memset(b->mData, 0, NumBytes ? NumBytes : 1);
    *ppBuffer = b;
    return S_OK;
}
