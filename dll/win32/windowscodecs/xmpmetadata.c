/*
 * PROJECT:     LiberNT Windows Imaging Component
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     XMP structure and array metadata readers
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include <stdarg.h>
#include <limits.h>
#define COBJMACROS
#include "windef.h"
#include "winbase.h"
#include "objbase.h"
#include "oleauto.h"
#include "propvarutil.h"
#include "wincodecs_private.h"
#include "initguid.h"
#include "msxml6.h"

static const WCHAR rdf_namespace[] = L"http://www.w3.org/1999/02/22-rdf-syntax-ns#";
static const WCHAR xmlns_namespace[] = L"http://www.w3.org/2000/xmlns/";
static const MetadataHandlerVtbl xmp_struct_vtable, xmp_bag_vtable, xmp_seq_vtable, xmp_alt_vtable;

static HRESULT xmp_string(PROPVARIANT *value, const WCHAR *text)
{
    SIZE_T length = text ? wcslen(text) : 0;

    if (length > ~(SIZE_T)0 / sizeof(WCHAR) - 1) return E_OUTOFMEMORY;
    value->pwszVal = CoTaskMemAlloc((length + 1) * sizeof(WCHAR));
    if (!value->pwszVal) return E_OUTOFMEMORY;
    if (length) memcpy(value->pwszVal, text, length * sizeof(WCHAR));
    value->pwszVal[length] = 0;
    value->vt = VT_LPWSTR;
    return S_OK;
}

static HRESULT xmp_name(IXMLDOMNode *node, BSTR *schema, BSTR *name)
{
    HRESULT hr;

    *schema = *name = NULL;
    hr = IXMLDOMNode_get_namespaceURI(node, schema);
    if (SUCCEEDED(hr)) hr = IXMLDOMNode_get_baseName(node, name);
    if (SUCCEEDED(hr) && (!*schema || !**schema || !*name || !**name))
        hr = WINCODEC_ERR_BADMETADATAHEADER;
    return hr;
}

static BOOL xmp_name_is(const WCHAR *schema, const WCHAR *name, const WCHAR *expected)
{
    return schema && name && !wcscmp(schema, rdf_namespace) && !wcscmp(name, expected);
}

static HRESULT xmp_append(MetadataHandler *handler, MetadataItem *item)
{
    MetadataItem *items;
    SIZE_T count = handler->item_count;
    UINT i;

    for (i = 0; i < handler->item_count; ++i)
        if (!PropVariantCompareEx(&handler->items[i].schema, &item->schema, 0, 0) &&
            !PropVariantCompareEx(&handler->items[i].id, &item->id, 0, 0))
            return WINCODEC_ERR_DUPLICATEMETADATAPRESENT;
    if (count == UINT_MAX || count >= ~(SIZE_T)0 / sizeof(*items)) return E_OUTOFMEMORY;
    items = realloc(handler->items, (count + 1) * sizeof(*items));
    if (!items) return E_OUTOFMEMORY;
    handler->items = items;
    handler->items[handler->item_count++] = *item;
    memset(item, 0, sizeof(*item));
    return S_OK;
}

static HRESULT xmp_attributes(IXMLDOMNode *node, MetadataHandler *handler,
        BOOL structure, BSTR *parse_type, BSTR *resource)
{
    IXMLDOMNamedNodeMap *attributes = NULL;
    IXMLDOMNode *attribute = NULL;
    BSTR schema = NULL, name = NULL, text = NULL;
    MetadataItem item = {0};
    LONG count, i;
    BOOL extra = FALSE;
    HRESULT hr;

    *parse_type = *resource = NULL;
    hr = IXMLDOMNode_get_attributes(node, &attributes);
    if (SUCCEEDED(hr) && attributes) hr = IXMLDOMNamedNodeMap_get_length(attributes, &count);
    else if (SUCCEEDED(hr)) return S_OK;
    for (i = 0; SUCCEEDED(hr) && i < count; ++i)
    {
        hr = IXMLDOMNamedNodeMap_get_item(attributes, i, &attribute);
        if (SUCCEEDED(hr)) hr = IXMLDOMNode_get_namespaceURI(attribute, &schema);
        if (SUCCEEDED(hr)) hr = IXMLDOMNode_get_baseName(attribute, &name);
        if (SUCCEEDED(hr)) hr = IXMLDOMNode_get_text(attribute, &text);
        if (SUCCEEDED(hr))
        {
            if (schema && !wcscmp(schema, xmlns_namespace)) {}
            else if (xmp_name_is(schema, name, L"parseType"))
            {
                *parse_type = text;
                text = NULL;
            }
            else if (xmp_name_is(schema, name, L"resource"))
            {
                *resource = text;
                text = NULL;
            }
            else if (structure && xmp_name_is(schema, name, L"about")) {}
            else if (structure && schema && *schema && name && *name &&
                     wcscmp(schema, rdf_namespace) && wcscmp(schema, L"http://www.w3.org/XML/1998/namespace"))
            {
                hr = xmp_string(&item.schema, schema);
                if (SUCCEEDED(hr)) hr = xmp_string(&item.id, name);
                if (SUCCEEDED(hr)) hr = xmp_string(&item.value, text);
                if (SUCCEEDED(hr)) hr = xmp_append(handler, &item);
            }
            else if (!structure) extra = TRUE;
            else hr = WINCODEC_ERR_UNSUPPORTEDOPERATION;
        }
        clear_metadata_item(&item);
        SysFreeString(schema);
        SysFreeString(name);
        SysFreeString(text);
        schema = name = text = NULL;
        if (attribute) IXMLDOMNode_Release(attribute);
        attribute = NULL;
    }
    if (attributes) IXMLDOMNamedNodeMap_Release(attributes);
    if (SUCCEEDED(hr) && extra && (!*parse_type || wcscmp(*parse_type, L"Resource")))
        hr = WINCODEC_ERR_UNSUPPORTEDOPERATION;
    return hr;
}

static HRESULT xmp_read_children(MetadataHandler *handler, IXMLDOMNode *node, BOOL array);

static HRESULT xmp_nested(IXMLDOMNode *node, const MetadataHandlerVtbl *vtable, PROPVARIANT *value)
{
    IWICMetadataReader *reader;
    MetadataHandler *handler;
    BSTR parse_type = NULL, resource = NULL;
    BOOL array = vtable != &xmp_struct_vtable;
    HRESULT hr;

    hr = MetadataReader_Create(vtable, &IID_IWICMetadataReader, (void **)&reader);
    if (FAILED(hr)) return hr;
    handler = CONTAINING_RECORD((IWICMetadataWriter *)reader, MetadataHandler, IWICMetadataWriter_iface);
    hr = xmp_attributes(node, handler, !array, &parse_type, &resource);
    if (SUCCEEDED(hr) && (resource || (parse_type && wcscmp(parse_type, L"Resource"))))
        hr = WINCODEC_ERR_UNSUPPORTEDOPERATION;
    if (SUCCEEDED(hr)) hr = xmp_read_children(handler, node, array);
    SysFreeString(parse_type);
    SysFreeString(resource);
    if (SUCCEEDED(hr))
    {
        value->vt = VT_UNKNOWN;
        value->punkVal = (IUnknown *)reader;
    }
    else IWICMetadataReader_Release(reader);
    return hr;
}

static HRESULT xmp_value(IXMLDOMNode *node, PROPVARIANT *value)
{
    IXMLDOMNodeList *children = NULL;
    IXMLDOMNode *child = NULL, *element = NULL;
    BSTR schema = NULL, name = NULL, text = NULL, parse_type = NULL, resource = NULL;
    DOMNodeType type;
    LONG count, i;
    UINT elements = 0;
    BOOL nonspace = FALSE;
    HRESULT hr;

    hr = xmp_attributes(node, NULL, FALSE, &parse_type, &resource);
    if (SUCCEEDED(hr) && parse_type)
    {
        if (resource || wcscmp(parse_type, L"Resource")) hr = WINCODEC_ERR_UNSUPPORTEDOPERATION;
        else hr = xmp_nested(node, &xmp_struct_vtable, value);
        goto done;
    }
    if (SUCCEEDED(hr)) hr = IXMLDOMNode_get_childNodes(node, &children);
    if (SUCCEEDED(hr)) hr = IXMLDOMNodeList_get_length(children, &count);
    for (i = 0; SUCCEEDED(hr) && i < count; ++i)
    {
        hr = IXMLDOMNodeList_get_item(children, i, &child);
        if (SUCCEEDED(hr)) hr = IXMLDOMNode_get_nodeType(child, &type);
        if (SUCCEEDED(hr) && type == NODE_ELEMENT)
        {
            ++elements;
            if (!element)
            {
                element = child;
                IXMLDOMNode_AddRef(element);
            }
        }
        else if (SUCCEEDED(hr) && (type == NODE_TEXT || type == NODE_CDATA_SECTION))
        {
            hr = IXMLDOMNode_get_text(child, &text);
            if (SUCCEEDED(hr) && text && text[wcsspn(text, L" \t\r\n")]) nonspace = TRUE;
            SysFreeString(text);
            text = NULL;
        }
        if (child) IXMLDOMNode_Release(child);
        child = NULL;
    }
    if (FAILED(hr)) goto done;
    if (elements)
    {
        if (elements != 1 || nonspace || resource)
        {
            hr = WINCODEC_ERR_BADMETADATAHEADER;
            goto done;
        }
        hr = xmp_name(element, &schema, &name);
        if (SUCCEEDED(hr))
        {
            if (xmp_name_is(schema, name, L"Description"))
                hr = xmp_nested(element, &xmp_struct_vtable, value);
            else if (xmp_name_is(schema, name, L"Bag"))
                hr = xmp_nested(element, &xmp_bag_vtable, value);
            else if (xmp_name_is(schema, name, L"Seq"))
                hr = xmp_nested(element, &xmp_seq_vtable, value);
            else if (xmp_name_is(schema, name, L"Alt"))
                hr = xmp_nested(element, &xmp_alt_vtable, value);
            else hr = WINCODEC_ERR_UNSUPPORTEDOPERATION;
        }
    }
    else if (resource)
    {
        if (nonspace) hr = WINCODEC_ERR_BADMETADATAHEADER;
        else hr = xmp_string(value, resource);
    }
    else
    {
        hr = IXMLDOMNode_get_text(node, &text);
        if (SUCCEEDED(hr)) hr = xmp_string(value, text);
    }
done:
    SysFreeString(schema);
    SysFreeString(name);
    SysFreeString(text);
    SysFreeString(parse_type);
    SysFreeString(resource);
    if (element) IXMLDOMNode_Release(element);
    if (children) IXMLDOMNodeList_Release(children);
    return hr;
}

static HRESULT xmp_read_children(MetadataHandler *handler, IXMLDOMNode *node, BOOL array)
{
    IXMLDOMNodeList *children = NULL;
    IXMLDOMNode *child = NULL;
    BSTR schema = NULL, name = NULL, text = NULL;
    MetadataItem item = {0};
    DOMNodeType type;
    LONG count, i;
    HRESULT hr;

    hr = IXMLDOMNode_get_childNodes(node, &children);
    if (SUCCEEDED(hr)) hr = IXMLDOMNodeList_get_length(children, &count);
    for (i = 0; SUCCEEDED(hr) && i < count; ++i)
    {
        hr = IXMLDOMNodeList_get_item(children, i, &child);
        if (SUCCEEDED(hr)) hr = IXMLDOMNode_get_nodeType(child, &type);
        if (SUCCEEDED(hr) && type == NODE_ELEMENT)
        {
            hr = xmp_name(child, &schema, &name);
            if (SUCCEEDED(hr) && array)
            {
                if (!xmp_name_is(schema, name, L"li")) hr = WINCODEC_ERR_BADMETADATAHEADER;
                item.id.vt = VT_UI4;
                item.id.ulVal = handler->item_count;
            }
            else if (SUCCEEDED(hr))
            {
                if (!wcscmp(schema, rdf_namespace)) hr = WINCODEC_ERR_UNSUPPORTEDOPERATION;
                if (SUCCEEDED(hr)) hr = xmp_string(&item.schema, schema);
                if (SUCCEEDED(hr)) hr = xmp_string(&item.id, name);
            }
            if (SUCCEEDED(hr)) hr = xmp_value(child, &item.value);
            if (SUCCEEDED(hr)) hr = xmp_append(handler, &item);
        }
        else if (SUCCEEDED(hr) && (type == NODE_TEXT || type == NODE_CDATA_SECTION))
        {
            hr = IXMLDOMNode_get_text(child, &text);
            if (SUCCEEDED(hr) && text && text[wcsspn(text, L" \t\r\n")])
                hr = WINCODEC_ERR_BADMETADATAHEADER;
        }
        clear_metadata_item(&item);
        SysFreeString(schema);
        SysFreeString(name);
        SysFreeString(text);
        schema = name = text = NULL;
        if (child) IXMLDOMNode_Release(child);
        child = NULL;
    }
    if (children) IXMLDOMNodeList_Release(children);
    return hr;
}

static HRESULT xmp_load(MetadataHandler *handler, IStream *stream, const GUID *vendor, DWORD options)
{
    IXMLDOMDocument2 *document = NULL;
    IXMLDOMNodeList *children = NULL;
    IXMLDOMNode *root = NULL;
    BSTR property = NULL, schema = NULL, name = NULL, parse_type = NULL, resource = NULL;
    VARIANT input, value;
    VARIANT_BOOL loaded = VARIANT_FALSE;
    DOMNodeType type;
    LONG count;
    BOOL array = handler->vtable != &xmp_struct_vtable;
    HRESULT hr;

    hr = CoCreateInstance(&CLSID_DOMDocument60, NULL, CLSCTX_INPROC_SERVER,
            &IID_IXMLDOMDocument2, (void **)&document);
    if (FAILED(hr)) return hr;
    hr = IXMLDOMDocument2_put_async(document, VARIANT_FALSE);
    if (SUCCEEDED(hr)) hr = IXMLDOMDocument2_put_validateOnParse(document, VARIANT_FALSE);
    if (SUCCEEDED(hr)) hr = IXMLDOMDocument2_put_resolveExternals(document, VARIANT_FALSE);
    if (SUCCEEDED(hr))
    {
        property = SysAllocString(L"ProhibitDTD");
        if (!property) hr = E_OUTOFMEMORY;
        else
        {
            VariantInit(&value);
            V_VT(&value) = VT_BOOL;
            V_BOOL(&value) = VARIANT_TRUE;
            hr = IXMLDOMDocument2_setProperty(document, property, value);
        }
    }
    VariantInit(&input);
    V_VT(&input) = VT_UNKNOWN;
    V_UNKNOWN(&input) = (IUnknown *)stream;
    if (SUCCEEDED(hr))
    {
        hr = IXMLDOMDocument2_load(document, input, &loaded);
        if (SUCCEEDED(hr) && hr != S_OK) hr = E_FAIL;
        else if (SUCCEEDED(hr) && loaded != VARIANT_TRUE) hr = E_INVALIDARG;
    }
    if (SUCCEEDED(hr)) hr = IXMLDOMDocument2_get_childNodes(document, &children);
    if (SUCCEEDED(hr)) hr = IXMLDOMNodeList_get_length(children, &count);
    if (SUCCEEDED(hr) && count != 1) hr = WINCODEC_ERR_UNEXPECTEDSIZE;
    if (SUCCEEDED(hr)) hr = IXMLDOMNodeList_get_item(children, 0, &root);
    if (SUCCEEDED(hr)) hr = IXMLDOMNode_get_nodeType(root, &type);
    if (SUCCEEDED(hr) && type != NODE_ELEMENT) hr = WINCODEC_ERR_UNEXPECTEDSIZE;
    if (SUCCEEDED(hr) && array)
    {
        hr = xmp_name(root, &schema, &name);
        if (SUCCEEDED(hr) && !xmp_name_is(schema, name,
                handler->vtable == &xmp_bag_vtable ? L"Bag" :
                handler->vtable == &xmp_seq_vtable ? L"Seq" : L"Alt"))
            hr = WINCODEC_ERR_BADMETADATAHEADER;
    }
    if (SUCCEEDED(hr)) hr = xmp_attributes(root, handler, !array, &parse_type, &resource);
    if (SUCCEEDED(hr) && (resource || (parse_type && wcscmp(parse_type, L"Resource"))))
        hr = WINCODEC_ERR_UNSUPPORTEDOPERATION;
    if (SUCCEEDED(hr)) hr = xmp_read_children(handler, root, array);
    SysFreeString(property);
    SysFreeString(schema);
    SysFreeString(name);
    SysFreeString(parse_type);
    SysFreeString(resource);
    if (root) IXMLDOMNode_Release(root);
    if (children) IXMLDOMNodeList_Release(children);
    IXMLDOMDocument2_Release(document);
    return hr;
}

static const MetadataHandlerVtbl xmp_struct_vtable =
{
    METADATAHANDLER_CASE_SENSITIVE | METADATAHANDLER_DETACHED_LOAD,
    &CLSID_WICXMPStructMetadataReader, xmp_load
};
static const MetadataHandlerVtbl xmp_bag_vtable =
{
    METADATAHANDLER_CASE_SENSITIVE | METADATAHANDLER_DETACHED_LOAD,
    &CLSID_WICXMPBagMetadataReader, xmp_load
};
static const MetadataHandlerVtbl xmp_seq_vtable =
{
    METADATAHANDLER_CASE_SENSITIVE | METADATAHANDLER_DETACHED_LOAD,
    &CLSID_WICXMPSeqMetadataReader, xmp_load
};
static const MetadataHandlerVtbl xmp_alt_vtable =
{
    METADATAHANDLER_CASE_SENSITIVE | METADATAHANDLER_DETACHED_LOAD,
    &CLSID_WICXMPAltMetadataReader, xmp_load
};

HRESULT XMPStructReader_CreateInstance(REFIID iid, void **out)
{
    if (!out) return E_INVALIDARG;
    return MetadataReader_Create(&xmp_struct_vtable, iid, out);
}

HRESULT XMPBagReader_CreateInstance(REFIID iid, void **out)
{
    if (!out) return E_INVALIDARG;
    return MetadataReader_Create(&xmp_bag_vtable, iid, out);
}

HRESULT XMPSeqReader_CreateInstance(REFIID iid, void **out)
{
    if (!out) return E_INVALIDARG;
    return MetadataReader_Create(&xmp_seq_vtable, iid, out);
}

HRESULT XMPAltReader_CreateInstance(REFIID iid, void **out)
{
    if (!out) return E_INVALIDARG;
    return MetadataReader_Create(&xmp_alt_vtable, iid, out);
}
