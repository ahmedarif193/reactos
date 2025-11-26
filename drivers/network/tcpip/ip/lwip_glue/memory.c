#include <lwip/mem.h>

#ifndef LWIP_TAG
#define LWIP_TAG 'PIwl'
#endif

typedef struct _LWIP_HEAP_HEADER
{
    SIZE_T Size;
#if DBG
    ULONGLONG Canary;
#endif
} LWIP_HEAP_HEADER, *PLWIP_HEAP_HEADER;

#if DBG
#define LWIP_HEAP_CANARY 0xC0DEC0DEC0DEC0DEULL
#endif

static __inline
PLWIP_HEAP_HEADER
LwipHeaderFromUserPointer(
    _In_ void *UserPtr)
{
    return (PLWIP_HEAP_HEADER)UserPtr - 1;
}

void *
malloc(mem_size_t size)
{
    PLWIP_HEAP_HEADER Header;
    SIZE_T TotalSize;

    if (size == 0)
        size = 1;

    TotalSize = sizeof(*Header) + (SIZE_T)size;

    Header = ExAllocatePoolWithTag(NonPagedPool,
                                   TotalSize,
                                   LWIP_TAG);
    if (!Header)
        return NULL;

    Header->Size = (SIZE_T)size;
#if DBG
    Header->Canary = LWIP_HEAP_CANARY;
#endif

    return (void *)(Header + 1);
}

void *
calloc(mem_size_t count, mem_size_t size)
{
    SIZE_T Total;
    void *mem;

    Total = (SIZE_T)count * (SIZE_T)size;
    mem = malloc((mem_size_t)Total);
    if (!mem)
        return NULL;

    RtlZeroMemory(mem, Total);

    return mem;
}

void
free(void *mem)
{
    PLWIP_HEAP_HEADER Header;

    if (!mem)
        return;

    Header = LwipHeaderFromUserPointer(mem);

#if DBG
    NT_ASSERT(Header->Canary == LWIP_HEAP_CANARY);
    Header->Canary = 0;
#endif

    ExFreePoolWithTag(Header, LWIP_TAG);
}

/* This is only used to trim in lwIP */
void *
realloc(void *mem, size_t size)
{
    PLWIP_HEAP_HEADER Header;
    void *new_mem;
    SIZE_T CopySize;

    /* realloc() with a NULL mem pointer acts like a call to malloc() */
    if (mem == NULL)
        return malloc((mem_size_t)size);

    /* realloc() with a size 0 acts like a call to free() */
    if (size == 0)
    {
        free(mem);
        return NULL;
    }

    Header = LwipHeaderFromUserPointer(mem);

    /* Allocate the new buffer first */
    new_mem = malloc((mem_size_t)size);
    if (new_mem == NULL)
    {
        /* The old buffer is still intact */
        return NULL;
    }

    /* Copy the data over (up to the old size) */
    CopySize = Header->Size < (SIZE_T)size ? Header->Size : (SIZE_T)size;
    RtlCopyMemory(new_mem, mem, CopySize);

    /* Deallocate the old buffer */
    free(mem);

    /* Return the newly allocated block */
    return new_mem;
}
