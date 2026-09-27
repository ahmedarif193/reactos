
#include <asm.inc>
#include <ks386.inc>


// cpu 486
.code

EXTERN _check:PROC

PUBLIC _continuePoint
_continuePoint:
 push 0
 mov word ptr [esp], ss
 push 0
 pushfd
 push 0
 mov word ptr [esp], cs
 PUSH_OFFSET _continuePoint
 push ebp

 push eax
 push ecx
 push edx
 push ebx
 push esi
 push edi

 push 0
 mov word ptr [esp], ds
 push 0
 mov word ptr [esp], es
 push 0
 mov word ptr [esp], fs
 push 0
 mov word ptr [esp], gs

 // TODO: floating point state
 sub esp, 112

 // Debug registers
 sub esp, 24

 push HEX(00010007)

 // Fill the Esp field
 lea eax, [esp + HEX(0CC)]
 lea ecx, [esp + HEX(0C4)]
 mov [ecx], eax

 // Call the function that will compare the current context with the expected one
 cld
 push esp
 call _check

 // check() must not return
 int 3

// EOF
END
