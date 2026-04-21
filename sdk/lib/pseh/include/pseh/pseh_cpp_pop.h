/*
 * Deliberately without include guards: this header restores the raw C++ SEH
 * aliases saved by pseh_cpp_push.h.
 */

#ifdef __cplusplus
#pragma pop_macro("__leave")
#pragma pop_macro("__endtry")
#pragma pop_macro("__finally")
#pragma pop_macro("__except")
#pragma pop_macro("__try")
#endif
