/*
 * Deliberately without include guards: this header scopes raw C++ SEH aliases
 * to the current include region and can be used multiple times per TU.
 */

#ifdef __cplusplus
#pragma push_macro("__try")
#pragma push_macro("__except")
#pragma push_macro("__finally")
#pragma push_macro("__endtry")
#pragma push_macro("__leave")
#include <pseh/pseh_cpp_aliases.h>
#endif
