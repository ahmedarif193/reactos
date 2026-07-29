#include "k32_vista.h"

#define NDEBUG
#include <debug.h>

#undef FIXME
#define FIXME DPRINT1

/* Taken from Wine kernelbase/process.c */

/***********************************************************************
 *           GetProcessGroupAffinity   (kernelbase.@)
 */
BOOL WINAPI DECLSPEC_HOTPATCH GetProcessGroupAffinity( HANDLE process, USHORT *count, USHORT *array )
{
    FIXME( "(%p,%p,%p): stub\n", process, count, array );
    SetLastError( ERROR_CALL_NOT_IMPLEMENTED );
    return FALSE;
}
