/* These definitions below are from Wine's headers, but for one reason or another
 * cannot be currently imported. The ultimate goal should be to phase this header
 * out entirely by importing these Wine headers unchanged into sdk/include/wine.
 *
 * Note: the header filenames correspond to Wine headers, they are often incompatible
 * with our headers or Microsoft's corresponding header.
 */

/* NTDEF.H */
#ifndef RTL_CONSTANT_STRING
#define RTL_CONSTANT_STRING(s) { sizeof(s) - sizeof(s[0]), sizeof(s), (void*)s }
#endif
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)

/* WINBASE.H */
typedef void *HPCON;

/* WINCON.H */
WINBASEAPI BOOL   WINAPI CloseConsoleHandle(HANDLE);
WINBASEAPI HANDLE WINAPI DuplicateConsoleHandle(HANDLE,DWORD,BOOL,DWORD);
WINBASEAPI HANDLE WINAPI GetConsoleInputWaitHandle(void);
