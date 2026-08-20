#include "k32_vista.h"

#ifdef __REACTOS__
/* _WIN32_WINNT is Vista in this directory, so the SDK hides this prototype. */
BOOL WINAPI GetLogicalProcessorInformationEx( LOGICAL_PROCESSOR_RELATIONSHIP,
                                              SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *,
                                              DWORD * );
#endif

/* Taken from Wine kernel32/process.c */

static SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *get_logical_processor_info(void)
{
    DWORD size = 0;
    SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *info;

    GetLogicalProcessorInformationEx( RelationGroup, NULL, &size );
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) return NULL;
    if (!(info = HeapAlloc( GetProcessHeap(), 0, size ))) return NULL;
    if (!GetLogicalProcessorInformationEx( RelationGroup, info, &size ))
    {
        HeapFree( GetProcessHeap(), 0, info );
        return NULL;
    }
    return info;
}


/***********************************************************************
 *           GetActiveProcessorCount (KERNEL32.@)
 */
DWORD WINAPI GetActiveProcessorCount(WORD group)
{
    DWORD cpus = 0;
    SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *info;

    if (!(info = get_logical_processor_info())) return 0;

    if (group == ALL_PROCESSOR_GROUPS)
    {
        for (group = 0; group < info->Group.ActiveGroupCount; group++)
            cpus += info->Group.GroupInfo[group].ActiveProcessorCount;
    }
    else
    {
        if (group < info->Group.ActiveGroupCount)
            cpus = info->Group.GroupInfo[group].ActiveProcessorCount;
    }

    HeapFree(GetProcessHeap(), 0, info);
    return cpus;
}

WORD WINAPI GetActiveProcessorGroupCount(void)
{
    WORD groups;
    SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *info;

    if (!(info = get_logical_processor_info())) return 0;

    groups = info->Group.ActiveGroupCount;

    HeapFree(GetProcessHeap(), 0, info);
    return groups;
}

DWORD WINAPI GetMaximumProcessorCount(WORD group)
{
    DWORD cpus = 0;
    SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *info;

    if (!(info = get_logical_processor_info())) return 0;

    if (group == ALL_PROCESSOR_GROUPS)
    {
        for (group = 0; group < info->Group.MaximumGroupCount; group++)
            cpus += info->Group.GroupInfo[group].MaximumProcessorCount;
    }
    else
    {
        if (group < info->Group.MaximumGroupCount)
            cpus = info->Group.GroupInfo[group].MaximumProcessorCount;
    }

    HeapFree(GetProcessHeap(), 0, info);
    return cpus;
}

WORD WINAPI GetMaximumProcessorGroupCount(void)
{
    WORD groups;
    SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *info;

    if (!(info = get_logical_processor_info())) return 0;

    groups = info->Group.MaximumGroupCount;

    HeapFree(GetProcessHeap(), 0, info);
    return groups;
}
