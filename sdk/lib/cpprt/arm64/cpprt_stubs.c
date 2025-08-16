/*
 * ARM64 C++ Runtime Support Stubs
 * TODO: Implement proper C++ exception handling for ARM64
 */

/* C++ exception handling personality routine */
void* __gxx_personality_seh0 = (void*)0;

/* Unwind resume function */
void _Unwind_Resume(void* exception_object)
{
    /* TODO: Implement proper unwind resume for ARM64 */
    /* For now, just terminate */
    while(1);
}

/* Note: __cxa_pure_virtual is now in __cxa_pure_virtual.cpp */

void __cxa_begin_catch(void* exception_object)
{
    /* TODO: Implement */
}

void __cxa_end_catch(void)
{
    /* TODO: Implement */
}

void __cxa_rethrow(void)
{
    /* TODO: Implement */
}

void* __cxa_allocate_exception(unsigned long thrown_size)
{
    /* TODO: Implement */
    return (void*)0;
}

void __cxa_throw(void* thrown_exception, void* tinfo, void (*dest)(void*))
{
    /* TODO: Implement */
    while(1);
}

void __cxa_call_unexpected(void* exception_object)
{
    /* TODO: Implement proper unexpected handler */
    /* For now, just terminate */
    while(1);
}

/* Note: C++ operators new/delete are now in cppnew.cpp */

/* C++ guard functions for static initialization */
int __cxa_guard_acquire(long long* guard_object)
{
    /* Simple implementation - check if already initialized */
    if (*guard_object & 1) {
        return 0;  /* Already initialized */
    }
    /* TODO: Add proper locking for thread safety */
    return 1;  /* Proceed with initialization */
}

void __cxa_guard_release(long long* guard_object)
{
    /* Mark as initialized */
    *guard_object |= 1;
}

void __cxa_guard_abort(long long* guard_object)
{
    /* Initialization aborted, nothing to do in simple implementation */
    /* TODO: Add proper cleanup if needed */
}