#include <ntfslib_new.h>
#include <ntfslib_new_internal.h>

#ifdef NTFSLIB_PORTABLE
#define NTFSLIB_DELETE_NOEXCEPT noexcept

void* __cdecl operator new(size_t Size)
{
    void* pObject = NtfsAllocatePoolWithTag(NonPagedPool,
                                             Size ? Size : 1,
                                             TAG_NTFS);
    if (!pObject)
        throw std::bad_alloc();
    return pObject;
}

void* __cdecl operator new[](size_t Size)
{
    return ::operator new(Size);
}
#else
#define NTFSLIB_DELETE_NOEXCEPT
#endif

void* __cdecl operator new(size_t Size, POOL_TYPE PoolType)
{

    Size = (Size != 0) ? Size : 1;
    void* pObject = NtfsAllocatePoolWithTag(PoolType, Size, TAG_NTFS);

#if DBG
    if (pObject != NULL)
        RtlFillMemory(pObject, Size, 0xCD);
#endif // DBG

    return pObject;
}

void* __cdecl operator new[](size_t Size, POOL_TYPE PoolType)
{

    Size = (Size != 0) ? Size : 1;

    void* pObject = NtfsAllocatePoolWithTag(PoolType, Size, TAG_NTFS);

#if DBG
    if (pObject != NULL)
        RtlFillMemory(pObject, Size, 0xCD);
#endif // DBG

    return pObject;
}

void* __cdecl operator new(size_t Size, POOL_TYPE PoolType, ULONG Tag)
{

    Size = (Size != 0) ? Size : 1;
    void* pObject = NtfsAllocatePoolWithTag(PoolType, Size, Tag);

#if DBG
    if (pObject != NULL)
        RtlFillMemory(pObject, Size, 0xCD);
#endif // DBG

    return pObject;
}

void* __cdecl operator new[](size_t Size, POOL_TYPE PoolType, ULONG Tag)
{

    Size = (Size != 0) ? Size : 1;

    void* pObject = NtfsAllocatePoolWithTag(PoolType, Size, Tag);

#if DBG
    if (pObject != NULL)
        RtlFillMemory(pObject, Size, 0xCD);
#endif // DBG

    return pObject;
}

void __cdecl operator delete(void* pObject) NTFSLIB_DELETE_NOEXCEPT
{
    if (pObject != NULL)
        NtfsFreePool(pObject);
}

void __cdecl operator delete(void* pObject, size_t s) NTFSLIB_DELETE_NOEXCEPT
{
    UNREFERENCED_PARAMETER( s );
    ::operator delete( pObject );
}

void __cdecl operator delete[](void* pObject) NTFSLIB_DELETE_NOEXCEPT
{
    if (pObject != NULL)
        NtfsFreePool(pObject);
}

#ifdef NTFSLIB_PORTABLE
void __cdecl operator delete[](void* pObject,
                               size_t s) NTFSLIB_DELETE_NOEXCEPT
{
    UNREFERENCED_PARAMETER(s);
    ::operator delete[](pObject);
}
#endif
