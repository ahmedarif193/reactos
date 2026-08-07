

#pragma once

void WINAPI RtlUpperString(STRING*, const STRING*);
LONG WINAPI RtlCompareString(const STRING*,const STRING*,BOOLEAN);
void WINAPI RtlMapGenericMask(ACCESS_MASK*,const GENERIC_MAPPING*);

typedef struct _PROCESS_ACCESS_TOKEN
{
    HANDLE Token;
    HANDLE Thread;
} PROCESS_ACCESS_TOKEN, *PPROCESS_ACCESS_TOKEN;
