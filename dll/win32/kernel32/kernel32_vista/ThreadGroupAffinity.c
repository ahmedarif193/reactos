#include "k32_vista.h"

/* Taken from Wine kernelbase/thread.c */

/***********************************************************************
 *           GetThreadGroupAffinity   (kernelbase.@)
 */
BOOL WINAPI DECLSPEC_HOTPATCH GetThreadGroupAffinity( HANDLE thread, GROUP_AFFINITY *affinity )
{
#ifdef __REACTOS__
    NTSTATUS status;
#endif

    if (!affinity)
    {
        SetLastError( ERROR_INVALID_PARAMETER );
        return FALSE;
    }
#ifdef __REACTOS__
    status = NtQueryInformationThread( thread, ThreadGroupInformation,
                                       affinity, sizeof(*affinity), NULL );
    if (!NT_SUCCESS(status))
    {
        SetLastError( RtlNtStatusToDosError( status ));
        return FALSE;
    }
    return TRUE;
#else
    return set_ntstatus( NtQueryInformationThread( thread, ThreadGroupInformation,
                                                   affinity, sizeof(*affinity), NULL ));
#endif
}


/***********************************************************************
 *           SetThreadGroupAffinity   (kernelbase.@)
 */
BOOL WINAPI DECLSPEC_HOTPATCH SetThreadGroupAffinity( HANDLE thread, const GROUP_AFFINITY *new,
                                                      GROUP_AFFINITY *old )
{
#ifdef __REACTOS__
    NTSTATUS status;
#endif

    if (old && !GetThreadGroupAffinity( thread, old )) return FALSE;
#ifdef __REACTOS__
    status = NtSetInformationThread( thread, ThreadGroupInformation, (void *)new, sizeof(*new) );
    if (!NT_SUCCESS(status))
    {
        SetLastError( RtlNtStatusToDosError( status ));
        return FALSE;
    }
    return TRUE;
#else
    return set_ntstatus( NtSetInformationThread( thread, ThreadGroupInformation, new, sizeof(*new) ));
#endif
}
