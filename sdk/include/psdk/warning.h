
/* FIXME - For WDK compatibility.
   Here #pragma warning are placed and are disabled or enabled.
   GCC and MSVC use different pragmas so a compatible list for
   these remains to be found. */

#if !defined(__cplusplus) && !defined(DONT_REDEFINE_TRYEXCEPT_IN_WARNING_H)
#undef try
#undef except
#undef finally
#undef leave
#define try __try
#define except __except
#define finally __finally
#define leave __leave
#endif
