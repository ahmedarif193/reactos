/*
 * Deliberately without include guards: C++ SEH aliases may need to be
 * rebound multiple times in a single translation unit after libstdc++
 * headers define their own __try/__catch macros.
 */

#ifdef __cplusplus
#undef __try
#undef __except
#undef __finally
#undef __endtry
#undef __leave
#ifdef _SEH2_NATIVE_SEH
#define __endtry
#else
#define __try _SEH2_TRY
#define __except _SEH2_EXCEPT
#define __finally _SEH2_FINALLY
#define __endtry _SEH2_END
#define __leave _SEH2_LEAVE
#endif
#endif
