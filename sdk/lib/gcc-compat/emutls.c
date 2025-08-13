/*
 * PROJECT:     ReactOS GCC compatibility library
 * LICENSE:     MIT
 * PURPOSE:     Thread-local storage emulation for GCC C++ runtime
 * COPYRIGHT:   Copyright 2025 ReactOS Team
 */

#include <windows.h>
#include <stdlib.h>

/* 
 * Simple TLS emulation implementation
 * This is a minimal implementation for C++ exception handling support
 * It provides basic thread-local storage without full POSIX TLS semantics
 */

typedef struct _emutls_object {
    size_t size;
    size_t align;
    void *default_value;
    void *(*destructor)(void*);
} emutls_object;

/* Simple global TLS slot for basic exception handling */
static DWORD tls_slot = TLS_OUT_OF_INDEXES;

void* __emutls_get_address(void* obj_ptr)
{
    emutls_object* obj = (emutls_object*)obj_ptr;
    void* data;
    
    /* Initialize TLS slot on first use */
    if (tls_slot == TLS_OUT_OF_INDEXES) {
        tls_slot = TlsAlloc();
        if (tls_slot == TLS_OUT_OF_INDEXES) {
            return NULL;
        }
    }
    
    /* Get existing data for this thread */
    data = TlsGetValue(tls_slot);
    if (!data && obj && obj->size > 0) {
        /* Allocate new data for this thread */
        data = calloc(1, obj->size);
        if (data) {
            /* Initialize with default value if provided */
            if (obj->default_value) {
                memcpy(data, obj->default_value, obj->size);
            }
            TlsSetValue(tls_slot, data);
        }
    }
    
    return data;
}

/* Clean up function for process/thread exit */
void __emutls_cleanup(void)
{
    if (tls_slot != TLS_OUT_OF_INDEXES) {
        void* data = TlsGetValue(tls_slot);
        if (data) {
            free(data);
            TlsSetValue(tls_slot, NULL);
        }
    }
}