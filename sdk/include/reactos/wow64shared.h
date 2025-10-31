#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef enum _WOW64_SHARED_INFORMATION_INDEX
{
    Wow64SharedInformationInvalid = 0,
    Wow64SharedInformationUserApcDispatcher32,
    Wow64SharedInformationUserCallbackDispatcher32,
    Wow64SharedInformationUserExceptionDispatcher32,
    Wow64SharedInformationUserApcReturn32,
    Wow64SharedInformationMaximum
} WOW64_SHARED_INFORMATION_INDEX;

#define WOW64_SHARED_INFORMATION_COUNT Wow64SharedInformationMaximum

#ifdef __cplusplus
}
#endif

