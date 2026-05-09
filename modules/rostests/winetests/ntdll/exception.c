/*
 * Unit test suite for ntdll exceptions
 *
 * Copyright 2005 Alexandre Julliard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <stdarg.h>
#include <stdio.h>
#include <setjmp.h>

#include "ntstatus.h"
#define WIN32_NO_STATUS
#if defined(__i386__) || defined(__x86_64__)
#define NONAMELESSUNION
#endif
#include "windef.h"
#include "winbase.h"
#include "winnt.h"
#include "winreg.h"
#include "winternl.h"
#ifdef __REACTOS__
#include <wine/exception.h>
#else
#include "excpt.h"
#endif
#include "wine/test.h"

static void *code_mem;

static NTSTATUS  (WINAPI *pNtGetContextThread)(HANDLE,CONTEXT*);
static NTSTATUS  (WINAPI *pNtSetContextThread)(HANDLE,CONTEXT*);
static NTSTATUS  (WINAPI *pRtlRaiseException)(EXCEPTION_RECORD *rec);
static PVOID     (WINAPI *pRtlUnwind)(PVOID, PVOID, PEXCEPTION_RECORD, PVOID);
static VOID      (WINAPI *pRtlCaptureContext)(CONTEXT*);
static PVOID     (WINAPI *pRtlAddVectoredExceptionHandler)(ULONG first, PVECTORED_EXCEPTION_HANDLER func);
static ULONG     (WINAPI *pRtlRemoveVectoredExceptionHandler)(PVOID handler);
static PVOID     (WINAPI *pRtlAddVectoredContinueHandler)(ULONG first, PVECTORED_EXCEPTION_HANDLER func);
static ULONG     (WINAPI *pRtlRemoveVectoredContinueHandler)(PVOID handler);
static NTSTATUS  (WINAPI *pNtReadVirtualMemory)(HANDLE, const void*, void*, SIZE_T, SIZE_T*);
static NTSTATUS  (WINAPI *pNtTerminateProcess)(HANDLE handle, LONG exit_code);
static NTSTATUS  (WINAPI *pNtQueryInformationProcess)(HANDLE, PROCESSINFOCLASS, PVOID, ULONG, PULONG);
static NTSTATUS  (WINAPI *pNtSetInformationProcess)(HANDLE, PROCESSINFOCLASS, PVOID, ULONG);
static BOOL      (WINAPI *pIsWow64Process)(HANDLE, PBOOL);
static NTSTATUS  (WINAPI *pNtClose)(HANDLE);

#if defined(__x86_64__)
#ifndef __REACTOS__
typedef struct
{
    ULONG Count;
    struct
    {
        ULONG BeginAddress;
        ULONG EndAddress;
        ULONG HandlerAddress;
        ULONG JumpTarget;
    } ScopeRecord[1];
} SCOPE_TABLE;

typedef struct
{
    ULONG64               ControlPc;
    ULONG64               ImageBase;
    PRUNTIME_FUNCTION     FunctionEntry;
    ULONG64               EstablisherFrame;
    ULONG64               TargetIp;
    PCONTEXT              ContextRecord;
    void* /*PEXCEPTION_ROUTINE*/ LanguageHandler;
    PVOID                 HandlerData;
    PUNWIND_HISTORY_TABLE HistoryTable;
    ULONG                 ScopeIndex;
} DISPATCHER_CONTEXT;

typedef struct _SETJMP_FLOAT128
{
    unsigned __int64 DECLSPEC_ALIGN(16) Part[2];
} SETJMP_FLOAT128;

typedef struct _JUMP_BUFFER
{
    unsigned __int64 Frame;
    unsigned __int64 Rbx;
    unsigned __int64 Rsp;
    unsigned __int64 Rbp;
    unsigned __int64 Rsi;
    unsigned __int64 Rdi;
    unsigned __int64 R12;
    unsigned __int64 R13;
    unsigned __int64 R14;
    unsigned __int64 R15;
    unsigned __int64 Rip;
    unsigned __int64 Spare;
    SETJMP_FLOAT128  Xmm6;
    SETJMP_FLOAT128  Xmm7;
    SETJMP_FLOAT128  Xmm8;
    SETJMP_FLOAT128  Xmm9;
    SETJMP_FLOAT128  Xmm10;
    SETJMP_FLOAT128  Xmm11;
    SETJMP_FLOAT128  Xmm12;
    SETJMP_FLOAT128  Xmm13;
    SETJMP_FLOAT128  Xmm14;
    SETJMP_FLOAT128  Xmm15;
} _JUMP_BUFFER;
#endif // __REACTOS__

static BOOLEAN   (CDECL *pRtlAddFunctionTable)(RUNTIME_FUNCTION*, DWORD, DWORD64);
static BOOLEAN   (CDECL *pRtlDeleteFunctionTable)(RUNTIME_FUNCTION*);
static BOOLEAN   (CDECL *pRtlInstallFunctionTableCallback)(DWORD64, DWORD64, DWORD, PGET_RUNTIME_FUNCTION_CALLBACK, PVOID, PCWSTR);
static PRUNTIME_FUNCTION (WINAPI *pRtlLookupFunctionEntry)(ULONG64, ULONG64*, UNWIND_HISTORY_TABLE*);
static EXCEPTION_DISPOSITION (WINAPI *p__C_specific_handler)(EXCEPTION_RECORD*, ULONG64, CONTEXT*, DISPATCHER_CONTEXT*);
static VOID      (WINAPI *pRtlCaptureContext)(CONTEXT*);
static VOID      (CDECL *pRtlRestoreContext)(CONTEXT*, EXCEPTION_RECORD*);
static VOID      (CDECL *pRtlUnwindEx)(VOID*, VOID*, EXCEPTION_RECORD*, VOID*, CONTEXT*, UNWIND_HISTORY_TABLE*);
static int       (CDECL *p_setjmp)(_JUMP_BUFFER*);
#endif

#if defined(__aarch64__)

#ifndef UNW_FLAG_NHANDLER
#define UNW_FLAG_NHANDLER  0
#define UNW_FLAG_EHANDLER  1
#define UNW_FLAG_UHANDLER  2
#define UNW_FLAG_CHAININFO 4
#endif

#ifdef __REACTOS__
typedef union _IMAGE_ARM64_RUNTIME_FUNCTION_ENTRY_XDATA {
    DWORD HeaderData;
    struct {
        DWORD FunctionLength : 18;
        DWORD Version : 2;
        DWORD ExceptionDataPresent : 1;
        DWORD EpilogInHeader : 1;
        DWORD EpilogCount : 5;
        DWORD CodeWords : 5;
    };
} IMAGE_ARM64_RUNTIME_FUNCTION_ENTRY_XDATA;
#endif

static BOOLEAN   (CDECL *pRtlAddFunctionTable)(RUNTIME_FUNCTION*, DWORD, DWORD64);
static BOOLEAN   (CDECL *pRtlDeleteFunctionTable)(RUNTIME_FUNCTION*);
static VOID      (CDECL *pRtlRestoreContext)(CONTEXT*, EXCEPTION_RECORD*);
static VOID      (CDECL *pRtlUnwindEx)(VOID*, VOID*, EXCEPTION_RECORD*, VOID*, CONTEXT*, struct _UNWIND_HISTORY_TABLE*);
#endif

#ifdef __i386__

#ifndef __WINE_WINTRNL_H
#define ProcessExecuteFlags 0x22
#define MEM_EXECUTE_OPTION_DISABLE   0x01
#define MEM_EXECUTE_OPTION_ENABLE    0x02
#define MEM_EXECUTE_OPTION_PERMANENT 0x08
#endif

static int      my_argc;
static char**   my_argv;
static int      test_stage;

static BOOL is_wow64;

/* Test various instruction combinations that cause a protection fault on the i386,
 * and check what the resulting exception looks like.
 */

static const struct exception
{
    BYTE     code[18];      /* asm code */
    BYTE     offset;        /* offset of faulting instruction */
    BYTE     length;        /* length of faulting instruction */
    BOOL     wow64_broken;  /* broken on Wow64, should be skipped */
    NTSTATUS status;        /* expected status code */
    DWORD    nb_params;     /* expected number of parameters */
    DWORD    params[4];     /* expected parameters */
    NTSTATUS alt_status;    /* alternative status code */
    DWORD    alt_nb_params; /* alternative number of parameters */
    DWORD    alt_params[4]; /* alternative parameters */
} exceptions[] =
{
/* 0 */
    /* test some privileged instructions */
    { { 0xfb, 0xc3 },  /* 0: sti; ret */
      0, 1, FALSE, STATUS_PRIVILEGED_INSTRUCTION, 0 },
    { { 0x6c, 0xc3 },  /* 1: insb (%dx); ret */
      0, 1, FALSE, STATUS_PRIVILEGED_INSTRUCTION, 0 },
    { { 0x6d, 0xc3 },  /* 2: insl (%dx); ret */
      0, 1, FALSE, STATUS_PRIVILEGED_INSTRUCTION, 0 },
    { { 0x6e, 0xc3 },  /* 3: outsb (%dx); ret */
      0, 1, FALSE, STATUS_PRIVILEGED_INSTRUCTION, 0 },
    { { 0x6f, 0xc3 },  /* 4: outsl (%dx); ret */
      0, 1, FALSE, STATUS_PRIVILEGED_INSTRUCTION, 0 },
/* 5 */
    { { 0xe4, 0x11, 0xc3 },  /* 5: inb $0x11,%al; ret */
      0, 2, FALSE, STATUS_PRIVILEGED_INSTRUCTION, 0 },
    { { 0xe5, 0x11, 0xc3 },  /* 6: inl $0x11,%eax; ret */
      0, 2, FALSE, STATUS_PRIVILEGED_INSTRUCTION, 0 },
    { { 0xe6, 0x11, 0xc3 },  /* 7: outb %al,$0x11; ret */
      0, 2, FALSE, STATUS_PRIVILEGED_INSTRUCTION, 0 },
    { { 0xe7, 0x11, 0xc3 },  /* 8: outl %eax,$0x11; ret */
      0, 2, FALSE, STATUS_PRIVILEGED_INSTRUCTION, 0 },
    { { 0xed, 0xc3 },  /* 9: inl (%dx),%eax; ret */
      0, 1, FALSE, STATUS_PRIVILEGED_INSTRUCTION, 0 },
/* 10 */
    { { 0xee, 0xc3 },  /* 10: outb %al,(%dx); ret */
      0, 1, FALSE, STATUS_PRIVILEGED_INSTRUCTION, 0 },
    { { 0xef, 0xc3 },  /* 11: outl %eax,(%dx); ret */
      0, 1, FALSE, STATUS_PRIVILEGED_INSTRUCTION, 0 },
    { { 0xf4, 0xc3 },  /* 12: hlt; ret */
      0, 1, FALSE, STATUS_PRIVILEGED_INSTRUCTION, 0 },
    { { 0xfa, 0xc3 },  /* 13: cli; ret */
      0, 1, FALSE, STATUS_PRIVILEGED_INSTRUCTION, 0 },

    /* test long jump to invalid selector */
    { { 0xea, 0, 0, 0, 0, 0, 0, 0xc3 },  /* 14: ljmp $0,$0; ret */
      0, 7, FALSE, STATUS_ACCESS_VIOLATION, 2, { 0, 0xffffffff } },

/* 15 */
    /* test iret to invalid selector */
    { { 0x6a, 0x00, 0x6a, 0x00, 0x6a, 0x00, 0xcf, 0x83, 0xc4, 0x0c, 0xc3 },
      /* 15: pushl $0; pushl $0; pushl $0; iret; addl $12,%esp; ret */
      6, 1, FALSE, STATUS_ACCESS_VIOLATION, 2, { 0, 0xffffffff } },

    /* test loading an invalid selector */
    { { 0xb8, 0xef, 0xbe, 0x00, 0x00, 0x8e, 0xe8, 0xc3 },  /* 16: mov $beef,%ax; mov %ax,%gs; ret */
      5, 2, FALSE, STATUS_ACCESS_VIOLATION, 2, { 0, 0xbee8 } }, /* 0xbee8 or 0xffffffff */

    /* test accessing a zero selector (%es broken on Wow64) */
    { { 0x06, 0x31, 0xc0, 0x8e, 0xc0, 0x26, 0xa1, 0, 0, 0, 0, 0x07, 0xc3 },
       /* push %es; xor %eax,%eax; mov %ax,%es; mov %es:(0),%ax; pop %es; ret */
      5, 6, TRUE, STATUS_ACCESS_VIOLATION, 2, { 0, 0xffffffff } },
    { { 0x0f, 0xa8, 0x31, 0xc0, 0x8e, 0xe8, 0x65, 0xa1, 0, 0, 0, 0, 0x0f, 0xa9, 0xc3 },
      /* push %gs; xor %eax,%eax; mov %ax,%gs; mov %gs:(0),%ax; pop %gs; ret */
      6, 6, FALSE, STATUS_ACCESS_VIOLATION, 2, { 0, 0xffffffff } },

    /* test moving %cs -> %ss */
    { { 0x0e, 0x17, 0x58, 0xc3 },  /* pushl %cs; popl %ss; popl %eax; ret */
      1, 1, FALSE, STATUS_ACCESS_VIOLATION, 2, { 0, 0xffffffff } },

/* 20 */
    /* test overlong instruction (limit is 15 bytes, 5 on Win7) */
    { { 0x64,0x64,0x64,0x64,0x64,0x64,0x64,0x64,0x64,0x64,0x64,0x64,0x64,0x64,0x64,0xfa,0xc3 },
      0, 16, TRUE, STATUS_ILLEGAL_INSTRUCTION, 0, { 0 },
      STATUS_ACCESS_VIOLATION, 2, { 0, 0xffffffff } },
    { { 0x64,0x64,0x64,0x64,0xfa,0xc3 },
      0, 5, TRUE, STATUS_PRIVILEGED_INSTRUCTION, 0 },

    /* test invalid interrupt */
    { { 0xcd, 0xff, 0xc3 },   /* int $0xff; ret */
      0, 2, FALSE, STATUS_ACCESS_VIOLATION, 2, { 0, 0xffffffff } },

    /* test moves to/from Crx */
    { { 0x0f, 0x20, 0xc0, 0xc3 },  /* movl %cr0,%eax; ret */
      0, 3, FALSE, STATUS_PRIVILEGED_INSTRUCTION, 0 },
    { { 0x0f, 0x20, 0xe0, 0xc3 },  /* movl %cr4,%eax; ret */
      0, 3, FALSE, STATUS_PRIVILEGED_INSTRUCTION, 0 },
/* 25 */
    { { 0x0f, 0x22, 0xc0, 0xc3 },  /* movl %eax,%cr0; ret */
      0, 3, FALSE, STATUS_PRIVILEGED_INSTRUCTION, 0 },
    { { 0x0f, 0x22, 0xe0, 0xc3 },  /* movl %eax,%cr4; ret */
      0, 3, FALSE, STATUS_PRIVILEGED_INSTRUCTION, 0 },

    /* test moves to/from Drx */
    { { 0x0f, 0x21, 0xc0, 0xc3 },  /* movl %dr0,%eax; ret */
      0, 3, FALSE, STATUS_PRIVILEGED_INSTRUCTION, 0 },
    { { 0x0f, 0x21, 0xc8, 0xc3 },  /* movl %dr1,%eax; ret */
      0, 3, FALSE, STATUS_PRIVILEGED_INSTRUCTION, 0 },
    { { 0x0f, 0x21, 0xf8, 0xc3 },  /* movl %dr7,%eax; ret */
      0, 3, FALSE, STATUS_PRIVILEGED_INSTRUCTION, 0 },
/* 30 */
    { { 0x0f, 0x23, 0xc0, 0xc3 },  /* movl %eax,%dr0; ret */
      0, 3, FALSE, STATUS_PRIVILEGED_INSTRUCTION, 0 },
    { { 0x0f, 0x23, 0xc8, 0xc3 },  /* movl %eax,%dr1; ret */
      0, 3, FALSE, STATUS_PRIVILEGED_INSTRUCTION, 0 },
    { { 0x0f, 0x23, 0xf8, 0xc3 },  /* movl %eax,%dr7; ret */
      0, 3, FALSE, STATUS_PRIVILEGED_INSTRUCTION, 0 },

    /* test memory reads */
    { { 0xa1, 0xfc, 0xff, 0xff, 0xff, 0xc3 },  /* movl 0xfffffffc,%eax; ret */
      0, 5, FALSE, STATUS_ACCESS_VIOLATION, 2, { 0, 0xfffffffc } },
    { { 0xa1, 0xfd, 0xff, 0xff, 0xff, 0xc3 },  /* movl 0xfffffffd,%eax; ret */
      0, 5, FALSE, STATUS_ACCESS_VIOLATION, 2, { 0, 0xfffffffd } },
/* 35 */
    { { 0xa1, 0xfe, 0xff, 0xff, 0xff, 0xc3 },  /* movl 0xfffffffe,%eax; ret */
      0, 5, FALSE, STATUS_ACCESS_VIOLATION, 2, { 0, 0xfffffffe } },
    { { 0xa1, 0xff, 0xff, 0xff, 0xff, 0xc3 },  /* movl 0xffffffff,%eax; ret */
      0, 5, FALSE, STATUS_ACCESS_VIOLATION, 2, { 0, 0xffffffff } },

    /* test memory writes */
    { { 0xa3, 0xfc, 0xff, 0xff, 0xff, 0xc3 },  /* movl %eax,0xfffffffc; ret */
      0, 5, FALSE, STATUS_ACCESS_VIOLATION, 2, { 1, 0xfffffffc } },
    { { 0xa3, 0xfd, 0xff, 0xff, 0xff, 0xc3 },  /* movl %eax,0xfffffffd; ret */
      0, 5, FALSE, STATUS_ACCESS_VIOLATION, 2, { 1, 0xfffffffd } },
    { { 0xa3, 0xfe, 0xff, 0xff, 0xff, 0xc3 },  /* movl %eax,0xfffffffe; ret */
      0, 5, FALSE, STATUS_ACCESS_VIOLATION, 2, { 1, 0xfffffffe } },
/* 40 */
    { { 0xa3, 0xff, 0xff, 0xff, 0xff, 0xc3 },  /* movl %eax,0xffffffff; ret */
      0, 5, FALSE, STATUS_ACCESS_VIOLATION, 2, { 1, 0xffffffff } },

    /* test exception with cleared %ds and %es (broken on Wow64) */
    { { 0x1e, 0x06, 0x31, 0xc0, 0x8e, 0xd8, 0x8e, 0xc0, 0xfa, 0x07, 0x1f, 0xc3 },
          /* push %ds; push %es; xorl %eax,%eax; mov %ax,%ds; mov %ax,%es; cli; pop %es; pop %ds; ret */
      8, 1, TRUE, STATUS_PRIVILEGED_INSTRUCTION, 0 },

    { { 0xf1, 0x90, 0xc3 },  /* icebp; nop; ret */
      1, 1, FALSE, STATUS_SINGLE_STEP, 0 },
    { { 0xb8, 0xb8, 0xb8, 0xb8, 0xb8,          /* mov $0xb8b8b8b8, %eax */
        0xb9, 0xb9, 0xb9, 0xb9, 0xb9,          /* mov $0xb9b9b9b9, %ecx */
        0xba, 0xba, 0xba, 0xba, 0xba,          /* mov $0xbabababa, %edx */
        0xcd, 0x2d, 0xc3 },                    /* int $0x2d; ret */
      17, 0, FALSE, STATUS_BREAKPOINT, 3, { 0xb8b8b8b8, 0xb9b9b9b9, 0xbabababa } },
};

static int got_exception;
static BOOL have_vectored_api;

static void run_exception_test(void *handler, const void* context,
                               const void *code, unsigned int code_size,
                               DWORD access)
{
    struct {
        EXCEPTION_REGISTRATION_RECORD frame;
        const void *context;
    } exc_frame;
    void (*func)(void) = code_mem;
    DWORD oldaccess, oldaccess2;

    exc_frame.frame.Handler = handler;
    exc_frame.frame.Prev = NtCurrentTeb()->Tib.ExceptionList;
    exc_frame.context = context;

    memcpy(code_mem, code, code_size);
    if(access)
        VirtualProtect(code_mem, code_size, access, &oldaccess);

    NtCurrentTeb()->Tib.ExceptionList = &exc_frame.frame;
    func();
    NtCurrentTeb()->Tib.ExceptionList = exc_frame.frame.Prev;

    if(access)
        VirtualProtect(code_mem, code_size, oldaccess, &oldaccess2);
}

static LONG CALLBACK rtlraiseexception_vectored_handler(EXCEPTION_POINTERS *ExceptionInfo)
{
    PCONTEXT context = ExceptionInfo->ContextRecord;
    PEXCEPTION_RECORD rec = ExceptionInfo->ExceptionRecord;
    trace("vect. handler %08x addr:%p context.Eip:%x\n", rec->ExceptionCode,
          rec->ExceptionAddress, context->Eip);

    ok(rec->ExceptionAddress == (char *)code_mem + 0xb, "ExceptionAddress at %p instead of %p\n",
       rec->ExceptionAddress, (char *)code_mem + 0xb);

    if (NtCurrentTeb()->Peb->BeingDebugged)
        ok((void *)context->Eax == pRtlRaiseException ||
           broken( is_wow64 && context->Eax == 0xf00f00f1 ), /* broken on vista */
           "debugger managed to modify Eax to %x should be %p\n",
           context->Eax, pRtlRaiseException);

    /* check that context.Eip is fixed up only for EXCEPTION_BREAKPOINT
     * even if raised by RtlRaiseException
     */
    if(rec->ExceptionCode == EXCEPTION_BREAKPOINT)
    {
        ok(context->Eip == (DWORD)code_mem + 0xa ||
           broken(context->Eip == (DWORD)code_mem + 0xb), /* win2k3 */
           "Eip at %x instead of %x or %x\n", context->Eip,
           (DWORD)code_mem + 0xa, (DWORD)code_mem + 0xb);
    }
    else
    {
        ok(context->Eip == (DWORD)code_mem + 0xb, "Eip at %x instead of %x\n",
           context->Eip, (DWORD)code_mem + 0xb);
    }

    /* test if context change is preserved from vectored handler to stack handlers */
    context->Eax = 0xf00f00f0;
    return EXCEPTION_CONTINUE_SEARCH;
}

static DWORD rtlraiseexception_handler( EXCEPTION_RECORD *rec, EXCEPTION_REGISTRATION_RECORD *frame,
                      CONTEXT *context, EXCEPTION_REGISTRATION_RECORD **dispatcher )
{
    trace( "exception: %08x flags:%x addr:%p context: Eip:%x\n",
           rec->ExceptionCode, rec->ExceptionFlags, rec->ExceptionAddress, context->Eip );

    ok(rec->ExceptionAddress == (char *)code_mem + 0xb, "ExceptionAddress at %p instead of %p\n",
       rec->ExceptionAddress, (char *)code_mem + 0xb);

    /* check that context.Eip is fixed up only for EXCEPTION_BREAKPOINT
     * even if raised by RtlRaiseException
     */
    if(rec->ExceptionCode == EXCEPTION_BREAKPOINT)
    {
        ok(context->Eip == (DWORD)code_mem + 0xa ||
           broken(context->Eip == (DWORD)code_mem + 0xb), /* win2k3 */
           "Eip at %x instead of %x or %x\n", context->Eip,
           (DWORD)code_mem + 0xa, (DWORD)code_mem + 0xb);
    }
    else
    {
        ok(context->Eip == (DWORD)code_mem + 0xb, "Eip at %x instead of %x\n",
           context->Eip, (DWORD)code_mem + 0xb);
    }

    if(have_vectored_api)
        ok(context->Eax == 0xf00f00f0, "Eax is %x, should have been set to 0xf00f00f0 in vectored handler\n",
           context->Eax);

    /* give the debugger a chance to examine the state a second time */
    /* without the exception handler changing Eip */
    if (test_stage == 2)
        return ExceptionContinueSearch;

    /* Eip in context is decreased by 1
     * Increase it again, else execution will continue in the middle of an instruction */
    if(rec->ExceptionCode == EXCEPTION_BREAKPOINT && (context->Eip == (DWORD)code_mem + 0xa))
        context->Eip += 1;
    return ExceptionContinueExecution;
}


static const BYTE call_one_arg_code[] = {
        0x8b, 0x44, 0x24, 0x08, /* mov 0x8(%esp),%eax */
        0x50,                   /* push %eax */
        0x8b, 0x44, 0x24, 0x08, /* mov 0x8(%esp),%eax */
        0xff, 0xd0,             /* call *%eax */
        0x90,                   /* nop */
        0x90,                   /* nop */
        0x90,                   /* nop */
        0x90,                   /* nop */
        0xc3,                   /* ret */
};


static void run_rtlraiseexception_test(DWORD exceptioncode)
{
    EXCEPTION_REGISTRATION_RECORD frame;
    EXCEPTION_RECORD record;
    PVOID vectored_handler = NULL;

    void (*func)(void* function, EXCEPTION_RECORD* record) = code_mem;

    record.ExceptionCode = exceptioncode;
    record.ExceptionFlags = 0;
    record.ExceptionRecord = NULL;
    record.ExceptionAddress = NULL; /* does not matter, copied return address */
    record.NumberParameters = 0;

    frame.Handler = rtlraiseexception_handler;
    frame.Prev = NtCurrentTeb()->Tib.ExceptionList;

    memcpy(code_mem, call_one_arg_code, sizeof(call_one_arg_code));

    NtCurrentTeb()->Tib.ExceptionList = &frame;
    if (have_vectored_api)
    {
        vectored_handler = pRtlAddVectoredExceptionHandler(TRUE, &rtlraiseexception_vectored_handler);
        ok(vectored_handler != 0, "RtlAddVectoredExceptionHandler failed\n");
    }

    func(pRtlRaiseException, &record);
    ok( record.ExceptionAddress == (char *)code_mem + 0x0b,
        "address set to %p instead of %p\n", record.ExceptionAddress, (char *)code_mem + 0x0b );

    if (have_vectored_api)
        pRtlRemoveVectoredExceptionHandler(vectored_handler);
    NtCurrentTeb()->Tib.ExceptionList = frame.Prev;
}

static void test_rtlraiseexception(void)
{
    if (!pRtlRaiseException)
    {
        skip("RtlRaiseException not found\n");
        return;
    }

    /* test without debugger */
    run_rtlraiseexception_test(0x12345);
    run_rtlraiseexception_test(EXCEPTION_BREAKPOINT);
    run_rtlraiseexception_test(EXCEPTION_INVALID_HANDLE);
}

static DWORD unwind_expected_eax;

static DWORD unwind_handler( EXCEPTION_RECORD *rec, EXCEPTION_REGISTRATION_RECORD *frame,
                             CONTEXT *context, EXCEPTION_REGISTRATION_RECORD **dispatcher )
{
    trace("exception: %08x flags:%x addr:%p context: Eip:%x\n",
          rec->ExceptionCode, rec->ExceptionFlags, rec->ExceptionAddress, context->Eip);

    ok(rec->ExceptionCode == STATUS_UNWIND, "ExceptionCode is %08x instead of %08x\n",
       rec->ExceptionCode, STATUS_UNWIND);
    ok(rec->ExceptionAddress == (char *)code_mem + 0x22, "ExceptionAddress at %p instead of %p\n",
       rec->ExceptionAddress, (char *)code_mem + 0x22);
    ok(context->Eax == unwind_expected_eax, "context->Eax is %08x instead of %08x\n",
       context->Eax, unwind_expected_eax);

    context->Eax += 1;
    return ExceptionContinueSearch;
}

static const BYTE call_unwind_code[] = {
    0x55,                           /* push %ebp */
    0x53,                           /* push %ebx */
    0x56,                           /* push %esi */
    0x57,                           /* push %edi */
    0xe8, 0x00, 0x00, 0x00, 0x00,   /* call 0 */
    0x58,                           /* 0: pop %eax */
    0x05, 0x1e, 0x00, 0x00, 0x00,   /* add $0x1e,%eax */
    0xff, 0x74, 0x24, 0x20,         /* push 0x20(%esp) */
    0xff, 0x74, 0x24, 0x20,         /* push 0x20(%esp) */
    0x50,                           /* push %eax */
    0xff, 0x74, 0x24, 0x24,         /* push 0x24(%esp) */
    0x8B, 0x44, 0x24, 0x24,         /* mov 0x24(%esp),%eax */
    0xff, 0xd0,                     /* call *%eax */
    0x5f,                           /* pop %edi */
    0x5e,                           /* pop %esi */
    0x5b,                           /* pop %ebx */
    0x5d,                           /* pop %ebp */
    0xc3,                           /* ret */
    0xcc,                           /* int $3 */
};

static void test_unwind(void)
{
    EXCEPTION_REGISTRATION_RECORD frames[2], *frame2 = &frames[0], *frame1 = &frames[1];
    DWORD (*func)(void* function, EXCEPTION_REGISTRATION_RECORD *pEndFrame, EXCEPTION_RECORD* record, DWORD retval) = code_mem;
    DWORD retval;

    memcpy(code_mem, call_unwind_code, sizeof(call_unwind_code));

    /* add first unwind handler */
    frame1->Handler = unwind_handler;
    frame1->Prev = NtCurrentTeb()->Tib.ExceptionList;
    NtCurrentTeb()->Tib.ExceptionList = frame1;

    /* add second unwind handler */
    frame2->Handler = unwind_handler;
    frame2->Prev = NtCurrentTeb()->Tib.ExceptionList;
    NtCurrentTeb()->Tib.ExceptionList = frame2;

    /* test unwind to current frame */
    unwind_expected_eax = 0xDEAD0000;
    retval = func(pRtlUnwind, frame2, NULL, 0xDEAD0000);
    ok(retval == 0xDEAD0000, "RtlUnwind returned eax %08x instead of %08x\n", retval, 0xDEAD0000);
    ok(NtCurrentTeb()->Tib.ExceptionList == frame2, "Exception record points to %p instead of %p\n",
       NtCurrentTeb()->Tib.ExceptionList, frame2);

    /* unwind to frame1 */
    unwind_expected_eax = 0xDEAD0000;
    retval = func(pRtlUnwind, frame1, NULL, 0xDEAD0000);
    ok(retval == 0xDEAD0001, "RtlUnwind returned eax %08x instead of %08x\n", retval, 0xDEAD0001);
    ok(NtCurrentTeb()->Tib.ExceptionList == frame1, "Exception record points to %p instead of %p\n",
       NtCurrentTeb()->Tib.ExceptionList, frame1);

    /* restore original handler */
    NtCurrentTeb()->Tib.ExceptionList = frame1->Prev;
}

static DWORD handler( EXCEPTION_RECORD *rec, EXCEPTION_REGISTRATION_RECORD *frame,
                      CONTEXT *context, EXCEPTION_REGISTRATION_RECORD **dispatcher )
{
    const struct exception *except = *(const struct exception **)(frame + 1);
    unsigned int i, parameter_count, entry = except - exceptions;

    got_exception++;
    trace( "exception %u: %x flags:%x addr:%p\n",
           entry, rec->ExceptionCode, rec->ExceptionFlags, rec->ExceptionAddress );

    ok( rec->ExceptionCode == except->status ||
        (except->alt_status != 0 && rec->ExceptionCode == except->alt_status),
        "%u: Wrong exception code %x/%x\n", entry, rec->ExceptionCode, except->status );
    ok( context->Eip == (DWORD_PTR)code_mem + except->offset,
        "%u: Unexpected eip %#x/%#lx\n", entry,
        context->Eip, (DWORD_PTR)code_mem + except->offset );
    ok( rec->ExceptionAddress == (char*)context->Eip ||
        (rec->ExceptionCode == STATUS_BREAKPOINT && rec->ExceptionAddress == (char*)context->Eip + 1),
        "%u: Unexpected exception address %p/%p\n", entry,
        rec->ExceptionAddress, (char*)context->Eip );

    if (except->status == STATUS_BREAKPOINT && is_wow64)
        parameter_count = 1;
    else if (except->alt_status == 0 || rec->ExceptionCode != except->alt_status)
        parameter_count = except->nb_params;
    else
        parameter_count = except->alt_nb_params;

    ok( rec->NumberParameters == parameter_count,
        "%u: Unexpected parameter count %u/%u\n", entry, rec->NumberParameters, parameter_count );

    /* Most CPUs (except Intel Core apparently) report a segment limit violation */
    /* instead of page faults for accesses beyond 0xffffffff */
    if (except->nb_params == 2 && except->params[1] >= 0xfffffffd)
    {
        if (rec->ExceptionInformation[0] == 0 && rec->ExceptionInformation[1] == 0xffffffff)
            goto skip_params;
    }

    /* Seems that both 0xbee8 and 0xfffffffff can be returned in windows */
    if (except->nb_params == 2 && rec->NumberParameters == 2
        && except->params[1] == 0xbee8 && rec->ExceptionInformation[1] == 0xffffffff
        && except->params[0] == rec->ExceptionInformation[0])
    {
        goto skip_params;
    }

    if (except->alt_status == 0 || rec->ExceptionCode != except->alt_status)
    {
        for (i = 0; i < rec->NumberParameters; i++)
            ok( rec->ExceptionInformation[i] == except->params[i],
                "%u: Wrong parameter %d: %lx/%x\n",
                entry, i, rec->ExceptionInformation[i], except->params[i] );
    }
    else
    {
        for (i = 0; i < rec->NumberParameters; i++)
            ok( rec->ExceptionInformation[i] == except->alt_params[i],
                "%u: Wrong parameter %d: %lx/%x\n",
                entry, i, rec->ExceptionInformation[i], except->alt_params[i] );
    }

skip_params:
    /* don't handle exception if it's not the address we expected */
    if (context->Eip != (DWORD_PTR)code_mem + except->offset) return ExceptionContinueSearch;

    context->Eip += except->length;
    return ExceptionContinueExecution;
}

static void test_prot_fault(void)
{
    unsigned int i;

    for (i = 0; i < sizeof(exceptions)/sizeof(exceptions[0]); i++)
    {
        if (is_wow64 && exceptions[i].wow64_broken && !strcmp( winetest_platform, "windows" ))
        {
            skip( "Exception %u broken on Wow64\n", i );
            continue;
        }
        got_exception = 0;
        run_exception_test(handler, &exceptions[i], &exceptions[i].code,
                           sizeof(exceptions[i].code), 0);
        if (!i && !got_exception)
        {
            trace( "No exception, assuming win9x, no point in testing further\n" );
            break;
        }
        ok( got_exception == (exceptions[i].status != 0),
            "%u: bad exception count %d\n", i, got_exception );
    }
}

struct dbgreg_test {
    DWORD dr0, dr1, dr2, dr3, dr6, dr7;
};

/* test handling of debug registers */
static DWORD dreg_handler( EXCEPTION_RECORD *rec, EXCEPTION_REGISTRATION_RECORD *frame,
                      CONTEXT *context, EXCEPTION_REGISTRATION_RECORD **dispatcher )
{
    const struct dbgreg_test *test = *(const struct dbgreg_test **)(frame + 1);

    context->Eip += 2;	/* Skips the popl (%eax) */
    context->Dr0 = test->dr0;
    context->Dr1 = test->dr1;
    context->Dr2 = test->dr2;
    context->Dr3 = test->dr3;
    context->Dr6 = test->dr6;
    context->Dr7 = test->dr7;
    return ExceptionContinueExecution;
}

#define CHECK_DEBUG_REG(n, m) \
    ok((ctx.Dr##n & m) == test->dr##n, "(%d) failed to set debug register " #n " to %x, got %x\n", \
       test_num, test->dr##n, ctx.Dr##n)

static void check_debug_registers(int test_num, const struct dbgreg_test *test)
{
    CONTEXT ctx;
    NTSTATUS status;

    ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    status = pNtGetContextThread(GetCurrentThread(), &ctx);
    ok(status == STATUS_SUCCESS, "NtGetContextThread failed with %x\n", status);

    CHECK_DEBUG_REG(0, ~0);
    CHECK_DEBUG_REG(1, ~0);
    CHECK_DEBUG_REG(2, ~0);
    CHECK_DEBUG_REG(3, ~0);
    CHECK_DEBUG_REG(6, 0x0f);
    CHECK_DEBUG_REG(7, ~0xdc00);
}

static const BYTE segfault_code[5] = {
	0x31, 0xc0, /* xor    %eax,%eax */
	0x8f, 0x00, /* popl   (%eax) - cause exception */
        0xc3        /* ret */
};

/* test the single step exception behaviour */
static DWORD single_step_handler( EXCEPTION_RECORD *rec, EXCEPTION_REGISTRATION_RECORD *frame,
                                  CONTEXT *context, EXCEPTION_REGISTRATION_RECORD **dispatcher )
{
    got_exception++;
    ok (!(context->EFlags & 0x100), "eflags has single stepping bit set\n");

    if( got_exception < 3)
        context->EFlags |= 0x100;  /* single step until popf instruction */
    else {
        /* show that the last single step exception on the popf instruction
         * (which removed the TF bit), still is a EXCEPTION_SINGLE_STEP exception */
        ok( rec->ExceptionCode == EXCEPTION_SINGLE_STEP,
            "exception is not EXCEPTION_SINGLE_STEP: %x\n", rec->ExceptionCode);
    }

    return ExceptionContinueExecution;
}

static const BYTE single_stepcode[] = {
    0x9c,		/* pushf */
    0x58,		/* pop   %eax */
    0x0d,0,1,0,0,	/* or    $0x100,%eax */
    0x50,		/* push   %eax */
    0x9d,		/* popf    */
    0x35,0,1,0,0,	/* xor    $0x100,%eax */
    0x50,		/* push   %eax */
    0x9d,		/* popf    */
    0xc3
};

/* Test the alignment check (AC) flag handling. */
static const BYTE align_check_code[] = {
    0x55,                  	/* push   %ebp */
    0x89,0xe5,             	/* mov    %esp,%ebp */
    0x9c,                  	/* pushf   */
    0x58,                  	/* pop    %eax */
    0x0d,0,0,4,0,       	/* or     $0x40000,%eax */
    0x50,                  	/* push   %eax */
    0x9d,                  	/* popf    */
    0x89,0xe0,                  /* mov %esp, %eax */
    0x8b,0x40,0x1,              /* mov 0x1(%eax), %eax - cause exception */
    0x9c,                  	/* pushf   */
    0x58,                  	/* pop    %eax */
    0x35,0,0,4,0,       	/* xor    $0x40000,%eax */
    0x50,                  	/* push   %eax */
    0x9d,                  	/* popf    */
    0x5d,                  	/* pop    %ebp */
    0xc3,                  	/* ret     */
};

static DWORD align_check_handler( EXCEPTION_RECORD *rec, EXCEPTION_REGISTRATION_RECORD *frame,
                                  CONTEXT *context, EXCEPTION_REGISTRATION_RECORD **dispatcher )
{
    ok (!(context->EFlags & 0x40000), "eflags has AC bit set\n");
    got_exception++;
    return ExceptionContinueExecution;
}

/* Test the direction flag handling. */
static const BYTE direction_flag_code[] = {
    0x55,                  	/* push   %ebp */
    0x89,0xe5,             	/* mov    %esp,%ebp */
    0xfd,                  	/* std */
    0xfa,                  	/* cli - cause exception */
    0x5d,                  	/* pop    %ebp */
    0xc3,                  	/* ret     */
};

static DWORD direction_flag_handler( EXCEPTION_RECORD *rec, EXCEPTION_REGISTRATION_RECORD *frame,
                                     CONTEXT *context, EXCEPTION_REGISTRATION_RECORD **dispatcher )
{
#ifdef __GNUC__
    unsigned int flags;
    __asm__("pushfl; popl %0; cld" : "=r" (flags) );
    /* older windows versions don't clear DF properly so don't test */
    if (flags & 0x400) trace( "eflags has DF bit set\n" );
#endif
    ok( context->EFlags & 0x400, "context eflags has DF bit cleared\n" );
    got_exception++;
    context->Eip++;  /* skip cli */
    context->EFlags &= ~0x400;  /* make sure it is cleared on return */
    return ExceptionContinueExecution;
}

/* test single stepping over hardware breakpoint */
static DWORD bpx_handler( EXCEPTION_RECORD *rec, EXCEPTION_REGISTRATION_RECORD *frame,
                          CONTEXT *context, EXCEPTION_REGISTRATION_RECORD **dispatcher )
{
    got_exception++;
    ok( rec->ExceptionCode == EXCEPTION_SINGLE_STEP,
        "wrong exception code: %x\n", rec->ExceptionCode);

    if(got_exception == 1) {
        /* hw bp exception on first nop */
        ok( context->Eip == (DWORD)code_mem, "eip is wrong:  %x instead of %x\n",
                                             context->Eip, (DWORD)code_mem);
        ok( (context->Dr6 & 0xf) == 1, "B0 flag is not set in Dr6\n");
        ros_skip_flaky
        ok( !(context->Dr6 & 0x4000), "BS flag is set in Dr6\n");
        context->Dr0 = context->Dr0 + 1;  /* set hw bp again on next instruction */
        context->EFlags |= 0x100;       /* enable single stepping */
    } else if(  got_exception == 2) {
        /* single step exception on second nop */
        ok( context->Eip == (DWORD)code_mem + 1, "eip is wrong: %x instead of %x\n",
                                                 context->Eip, (DWORD)code_mem + 1);
        ok( (context->Dr6 & 0x4000), "BS flag is not set in Dr6\n");
       /* depending on the win version the B0 bit is already set here as well
        ok( (context->Dr6 & 0xf) == 0, "B0...3 flags in Dr6 shouldn't be set\n"); */
        context->EFlags |= 0x100;
    } else if( got_exception == 3) {
        /* hw bp exception on second nop */
        ok( context->Eip == (DWORD)code_mem + 1, "eip is wrong: %x instead of %x\n",
                                                 context->Eip, (DWORD)code_mem + 1);
        ok( (context->Dr6 & 0xf) == 1, "B0 flag is not set in Dr6\n");
        ok( !(context->Dr6 & 0x4000), "BS flag is set in Dr6\n");
        context->Dr0 = 0;       /* clear breakpoint */
        context->EFlags |= 0x100;
    } else {
        /* single step exception on ret */
        ok( context->Eip == (DWORD)code_mem + 2, "eip is wrong: %x instead of %x\n",
                                                 context->Eip, (DWORD)code_mem + 2);
        ok( (context->Dr6 & 0xf) == 0, "B0...3 flags in Dr6 shouldn't be set\n");
        ok( (context->Dr6 & 0x4000), "BS flag is not set in Dr6\n");
    }

    context->Dr6 = 0;  /* clear status register */
    return ExceptionContinueExecution;
}

static const BYTE dummy_code[] = { 0x90, 0x90, 0xc3 };  /* nop, nop, ret */

/* test int3 handling */
static DWORD int3_handler( EXCEPTION_RECORD *rec, EXCEPTION_REGISTRATION_RECORD *frame,
                           CONTEXT *context, EXCEPTION_REGISTRATION_RECORD **dispatcher )
{
    ok( rec->ExceptionAddress == code_mem, "exception address not at: %p, but at %p\n",
                                           code_mem,  rec->ExceptionAddress);
    ok( context->Eip == (DWORD)code_mem, "eip not at: %p, but at %#x\n", code_mem, context->Eip);
    if(context->Eip == (DWORD)code_mem) context->Eip++; /* skip breakpoint */

    return ExceptionContinueExecution;
}

static const BYTE int3_code[] = { 0xCC, 0xc3 };  /* int 3, ret */

static DWORD WINAPI hw_reg_exception_thread( void *arg )
{
    int expect = (ULONG_PTR)arg;
    got_exception = 0;
    run_exception_test( bpx_handler, NULL, dummy_code, sizeof(dummy_code), 0 );
    ok( got_exception == expect, "expected %u exceptions, got %d\n", expect, got_exception );
    return 0;
}

static void test_exceptions(void)
{
    CONTEXT ctx;
    NTSTATUS res;
    struct dbgreg_test dreg_test;
    HANDLE h;

    if (!pNtGetContextThread || !pNtSetContextThread)
    {
        skip( "NtGetContextThread/NtSetContextThread not found\n" );
        return;
    }

    /* test handling of debug registers */
    memset(&dreg_test, 0, sizeof(dreg_test));

    dreg_test.dr0 = 0x42424240;
    dreg_test.dr2 = 0x126bb070;
    dreg_test.dr3 = 0x0badbad0;
    dreg_test.dr7 = 0xffff0115;
    run_exception_test(dreg_handler, &dreg_test, &segfault_code, sizeof(segfault_code), 0);
    check_debug_registers(1, &dreg_test);

    dreg_test.dr0 = 0x42424242;
    dreg_test.dr2 = 0x100f0fe7;
    dreg_test.dr3 = 0x0abebabe;
    dreg_test.dr7 = 0x115;
    run_exception_test(dreg_handler, &dreg_test, &segfault_code, sizeof(segfault_code), 0);
    check_debug_registers(2, &dreg_test);

    /* test single stepping behavior */
    got_exception = 0;
    run_exception_test(single_step_handler, NULL, &single_stepcode, sizeof(single_stepcode), 0);
    ok(got_exception == 3, "expected 3 single step exceptions, got %d\n", got_exception);

    /* test alignment exceptions */
    got_exception = 0;
    run_exception_test(align_check_handler, NULL, align_check_code, sizeof(align_check_code), 0);
    ok(got_exception == 0, "got %d alignment faults, expected 0\n", got_exception);

    /* test direction flag */
    got_exception = 0;
    run_exception_test(direction_flag_handler, NULL, direction_flag_code, sizeof(direction_flag_code), 0);
    ok(got_exception == 1, "got %d exceptions, expected 1\n", got_exception);

    /* test single stepping over hardware breakpoint */
    memset(&ctx, 0, sizeof(ctx));
    ctx.Dr0 = (DWORD) code_mem;  /* set hw bp on first nop */
    ctx.Dr7 = 3;
    ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    res = pNtSetContextThread( GetCurrentThread(), &ctx);
    ok( res == STATUS_SUCCESS, "NtSetContextThread failed with %x\n", res);

    got_exception = 0;
    run_exception_test(bpx_handler, NULL, dummy_code, sizeof(dummy_code), 0);
    ok( got_exception == 4,"expected 4 exceptions, got %d\n", got_exception);

    /* test int3 handling */
    run_exception_test(int3_handler, NULL, int3_code, sizeof(int3_code), 0);

    /* test that hardware breakpoints are not inherited by created threads */
    res = pNtSetContextThread( GetCurrentThread(), &ctx );
    ok( res == STATUS_SUCCESS, "NtSetContextThread failed with %x\n", res );

    h = CreateThread( NULL, 0, hw_reg_exception_thread, 0, 0, NULL );
    WaitForSingleObject( h, 10000 );
    CloseHandle( h );

    h = CreateThread( NULL, 0, hw_reg_exception_thread, (void *)4, CREATE_SUSPENDED, NULL );
    ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    res = pNtGetContextThread( h, &ctx );
    ok( res == STATUS_SUCCESS, "NtGetContextThread failed with %x\n", res );
    ok( ctx.Dr0 == 0, "dr0 %x\n", ctx.Dr0 );
    ok( ctx.Dr7 == 0, "dr7 %x\n", ctx.Dr7 );
    ctx.Dr0 = (DWORD)code_mem;
    ctx.Dr7 = 3;
    res = pNtSetContextThread( h, &ctx );
    ok( res == STATUS_SUCCESS, "NtSetContextThread failed with %x\n", res );
    ResumeThread( h );
    WaitForSingleObject( h, 10000 );
    CloseHandle( h );

    ctx.Dr0 = 0;
    ctx.Dr7 = 0;
    res = pNtSetContextThread( GetCurrentThread(), &ctx );
    ok( res == STATUS_SUCCESS, "NtSetContextThread failed with %x\n", res );
}

static void test_debugger(void)
{
    char cmdline[MAX_PATH];
    PROCESS_INFORMATION pi;
    STARTUPINFOA si = { 0 };
    DEBUG_EVENT de;
    DWORD continuestatus;
    PVOID code_mem_address = NULL;
    NTSTATUS status;
    SIZE_T size_read;
    BOOL ret;
    int counter = 0;
    si.cb = sizeof(si);

    if(!pNtGetContextThread || !pNtSetContextThread || !pNtReadVirtualMemory || !pNtTerminateProcess)
    {
        skip("NtGetContextThread, NtSetContextThread, NtReadVirtualMemory or NtTerminateProcess not found\n");
        return;
    }

    sprintf(cmdline, "%s %s %s %p", my_argv[0], my_argv[1], "debuggee", &test_stage);
    ret = CreateProcessA(NULL, cmdline, NULL, NULL, FALSE, DEBUG_PROCESS, NULL, NULL, &si, &pi);
    ok(ret, "could not create child process error: %u\n", GetLastError());
    if (!ret)
        return;

    do
    {
        continuestatus = DBG_CONTINUE;
        ok(WaitForDebugEvent(&de, INFINITE), "reading debug event\n");

        if (de.dwThreadId != pi.dwThreadId)
        {
            trace("event %d not coming from main thread, ignoring\n", de.dwDebugEventCode);
            ContinueDebugEvent(de.dwProcessId, de.dwThreadId, DBG_CONTINUE);
            continue;
        }

        if (de.dwDebugEventCode == CREATE_PROCESS_DEBUG_EVENT)
        {
            if(de.u.CreateProcessInfo.lpBaseOfImage != NtCurrentTeb()->Peb->ImageBaseAddress)
            {
                skip("child process loaded at different address, terminating it\n");
                pNtTerminateProcess(pi.hProcess, 0);
            }
        }
        else if (de.dwDebugEventCode == EXCEPTION_DEBUG_EVENT)
        {
            CONTEXT ctx;
            int stage;

            counter++;
            status = pNtReadVirtualMemory(pi.hProcess, &code_mem, &code_mem_address,
                                          sizeof(code_mem_address), &size_read);
            ok(!status,"NtReadVirtualMemory failed with 0x%x\n", status);
            status = pNtReadVirtualMemory(pi.hProcess, &test_stage, &stage,
                                          sizeof(stage), &size_read);
            ok(!status,"NtReadVirtualMemory failed with 0x%x\n", status);

            ctx.ContextFlags = CONTEXT_FULL;
            status = pNtGetContextThread(pi.hThread, &ctx);
            ok(!status, "NtGetContextThread failed with 0x%x\n", status);

            trace("exception 0x%x at %p firstchance=%d Eip=0x%x, Eax=0x%x\n",
                  de.u.Exception.ExceptionRecord.ExceptionCode,
                  de.u.Exception.ExceptionRecord.ExceptionAddress, de.u.Exception.dwFirstChance, ctx.Eip, ctx.Eax);

            if (counter > 100)
            {
                ok(FALSE, "got way too many exceptions, probably caught in an infinite loop, terminating child\n");
                pNtTerminateProcess(pi.hProcess, 1);
            }
            else if (counter >= 2) /* skip startup breakpoint */
            {
                if (stage == 1)
                {
                    ok((char *)ctx.Eip == (char *)code_mem_address + 0xb, "Eip at %x instead of %p\n",
                       ctx.Eip, (char *)code_mem_address + 0xb);
                    /* setting the context from debugger does not affect the context, the exception handlers gets */
                    /* uncomment once wine is fixed */
                    /* ctx.Eip = 0x12345; */
                    ctx.Eax = 0xf00f00f1;

                    /* let the debuggee handle the exception */
                    continuestatus = DBG_EXCEPTION_NOT_HANDLED;
                }
                else if (stage == 2)
                {
                    if (de.u.Exception.dwFirstChance)
                    {
                        /* debugger gets first chance exception with unmodified ctx.Eip */
                        ok((char *)ctx.Eip == (char *)code_mem_address + 0xb, "Eip at 0x%x instead of %p\n",
                            ctx.Eip, (char *)code_mem_address + 0xb);

                        /* setting the context from debugger does not affect the context, the exception handlers gets */
                        /* uncomment once wine is fixed */
                        /* ctx.Eip = 0x12345; */
                        ctx.Eax = 0xf00f00f1;

                        /* pass exception to debuggee
                         * exception will not be handled and
                         * a second chance exception will be raised */
                        continuestatus = DBG_EXCEPTION_NOT_HANDLED;
                    }
                    else
                    {
                        /* debugger gets context after exception handler has played with it */
                        /* ctx.Eip is the same value the exception handler got */
                        if (de.u.Exception.ExceptionRecord.ExceptionCode == EXCEPTION_BREAKPOINT)
                        {
                            ok((char *)ctx.Eip == (char *)code_mem_address + 0xa ||
                               broken(is_wow64 && (char *)ctx.Eip == (char *)code_mem_address + 0xb),
                               "Eip at 0x%x instead of %p\n",
                                ctx.Eip, (char *)code_mem_address + 0xa);
                            /* need to fixup Eip for debuggee */
                            if ((char *)ctx.Eip == (char *)code_mem_address + 0xa)
                                ctx.Eip += 1;
                        }
                        else
                            ok((char *)ctx.Eip == (char *)code_mem_address + 0xb, "Eip at 0x%x instead of %p\n",
                                ctx.Eip, (char *)code_mem_address + 0xb);
                        /* here we handle exception */
                    }
                }
                else if (stage == 7 || stage == 8)
                {
                    ok(de.u.Exception.ExceptionRecord.ExceptionCode == EXCEPTION_BREAKPOINT,
                       "expected EXCEPTION_BREAKPOINT, got %08x\n", de.u.Exception.ExceptionRecord.ExceptionCode);
                    ok((char *)ctx.Eip == (char *)code_mem_address + 0x1d,
                       "expected Eip = %p, got 0x%x\n", (char *)code_mem_address + 0x1d, ctx.Eip);

                    if (stage == 8) continuestatus = DBG_EXCEPTION_NOT_HANDLED;
                }
                else if (stage == 9 || stage == 10)
                {
                    ok(de.u.Exception.ExceptionRecord.ExceptionCode == EXCEPTION_BREAKPOINT,
                       "expected EXCEPTION_BREAKPOINT, got %08x\n", de.u.Exception.ExceptionRecord.ExceptionCode);
                    ok((char *)ctx.Eip == (char *)code_mem_address + 2,
                       "expected Eip = %p, got 0x%x\n", (char *)code_mem_address + 2, ctx.Eip);

                    if (stage == 10) continuestatus = DBG_EXCEPTION_NOT_HANDLED;
                }
                else if (stage == 11 || stage == 12)
                {
                    ok(de.u.Exception.ExceptionRecord.ExceptionCode == EXCEPTION_INVALID_HANDLE,
                       "unexpected exception code %08x, expected %08x\n", de.u.Exception.ExceptionRecord.ExceptionCode,
                       EXCEPTION_INVALID_HANDLE);
                    ok(de.u.Exception.ExceptionRecord.NumberParameters == 0,
                       "unexpected number of parameters %d, expected 0\n", de.u.Exception.ExceptionRecord.NumberParameters);

                    if (stage == 12) continuestatus = DBG_EXCEPTION_NOT_HANDLED;
                }
                else
                    ok(FALSE, "unexpected stage %x\n", stage);

                status = pNtSetContextThread(pi.hThread, &ctx);
                ok(!status, "NtSetContextThread failed with 0x%x\n", status);
            }
        }
        else if (de.dwDebugEventCode == OUTPUT_DEBUG_STRING_EVENT)
        {
            int stage;
#ifdef __REACTOS__
            /* This will catch our DPRINTs, such as
             * "WARNING:  RtlpDphTargetDllsLogicInitialize at ..\..\lib\rtl\heappage.c:1283 is UNIMPLEMENTED!"
             * so we need a full-size buffer to avoid a stack overflow
             */
            char buffer[513];
#else
            char buffer[64];
#endif

            status = pNtReadVirtualMemory(pi.hProcess, &test_stage, &stage,
                                          sizeof(stage), &size_read);
            ok(!status,"NtReadVirtualMemory failed with 0x%x\n", status);

            ok(!de.u.DebugString.fUnicode, "unexpected unicode debug string event\n");
            ok(de.u.DebugString.nDebugStringLength < sizeof(buffer) - 1, "buffer not large enough to hold %d bytes\n",
               de.u.DebugString.nDebugStringLength);

            memset(buffer, 0, sizeof(buffer));
            status = pNtReadVirtualMemory(pi.hProcess, de.u.DebugString.lpDebugStringData, buffer,
                                          de.u.DebugString.nDebugStringLength, &size_read);
            ok(!status,"NtReadVirtualMemory failed with 0x%x\n", status);

            if (stage == 3 || stage == 4)
                ok(!strcmp(buffer, "Hello World"), "got unexpected debug string '%s'\n", buffer);
            else /* ignore unrelated debug strings like 'SHIMVIEW: ShimInfo(Complete)' */
                ok(strstr(buffer, "SHIMVIEW") != NULL, "unexpected stage %x, got debug string event '%s'\n", stage, buffer);

            if (stage == 4) continuestatus = DBG_EXCEPTION_NOT_HANDLED;
        }
        else if (de.dwDebugEventCode == RIP_EVENT)
        {
            int stage;

            status = pNtReadVirtualMemory(pi.hProcess, &test_stage, &stage,
                                          sizeof(stage), &size_read);
            ok(!status,"NtReadVirtualMemory failed with 0x%x\n", status);

            if (stage == 5 || stage == 6)
            {
                ok(de.u.RipInfo.dwError == 0x11223344, "got unexpected rip error code %08x, expected %08x\n",
                   de.u.RipInfo.dwError, 0x11223344);
                ok(de.u.RipInfo.dwType  == 0x55667788, "got unexpected rip type %08x, expected %08x\n",
                   de.u.RipInfo.dwType, 0x55667788);
            }
            else
                ok(FALSE, "unexpected stage %x\n", stage);

            if (stage == 6) continuestatus = DBG_EXCEPTION_NOT_HANDLED;
        }

        ContinueDebugEvent(de.dwProcessId, de.dwThreadId, continuestatus);

    } while (de.dwDebugEventCode != EXIT_PROCESS_DEBUG_EVENT);

    winetest_wait_child_process( pi.hProcess );
    ret = CloseHandle(pi.hThread);
    ok(ret, "error %u\n", GetLastError());
    ret = CloseHandle(pi.hProcess);
    ok(ret, "error %u\n", GetLastError());

    return;
}

static DWORD simd_fault_handler( EXCEPTION_RECORD *rec, EXCEPTION_REGISTRATION_RECORD *frame,
                                 CONTEXT *context, EXCEPTION_REGISTRATION_RECORD **dispatcher )
{
    int *stage = *(int **)(frame + 1);

    got_exception++;

    if( *stage == 1) {
        /* fault while executing sse instruction */
        context->Eip += 3; /* skip addps */
        return ExceptionContinueExecution;
    }
    else if ( *stage == 2 || *stage == 3 ) {
        /* stage 2 - divide by zero fault */
        /* stage 3 - invalid operation fault */
        if( rec->ExceptionCode == EXCEPTION_ILLEGAL_INSTRUCTION)
            skip("system doesn't support SIMD exceptions\n");
        else {
            ok( rec->ExceptionCode ==  STATUS_FLOAT_MULTIPLE_TRAPS,
                "exception code: %#x, should be %#x\n",
                rec->ExceptionCode,  STATUS_FLOAT_MULTIPLE_TRAPS);
            ok( rec->NumberParameters == 1 || broken(is_wow64 && rec->NumberParameters == 2),
                "# of params: %i, should be 1\n",
                rec->NumberParameters);
            if( rec->NumberParameters == 1 )
                ok( rec->ExceptionInformation[0] == 0, "param #1: %lx, should be 0\n", rec->ExceptionInformation[0]);
        }
        context->Eip += 3; /* skip divps */
    }
    else
        ok(FALSE, "unexpected stage %x\n", *stage);

    return ExceptionContinueExecution;
}

static const BYTE simd_exception_test[] = {
    0x83, 0xec, 0x4,                     /* sub $0x4, %esp       */
    0x0f, 0xae, 0x1c, 0x24,              /* stmxcsr (%esp)       */
    0x8b, 0x04, 0x24,                    /* mov    (%esp),%eax   * store mxcsr */
    0x66, 0x81, 0x24, 0x24, 0xff, 0xfd,  /* andw $0xfdff,(%esp)  * enable divide by */
    0x0f, 0xae, 0x14, 0x24,              /* ldmxcsr (%esp)       * zero exceptions  */
    0x6a, 0x01,                          /* push   $0x1          */
    0x6a, 0x01,                          /* push   $0x1          */
    0x6a, 0x01,                          /* push   $0x1          */
    0x6a, 0x01,                          /* push   $0x1          */
    0x0f, 0x10, 0x0c, 0x24,              /* movups (%esp),%xmm1  * fill dividend  */
    0x0f, 0x57, 0xc0,                    /* xorps  %xmm0,%xmm0   * clear divisor  */
    0x0f, 0x5e, 0xc8,                    /* divps  %xmm0,%xmm1   * generate fault */
    0x83, 0xc4, 0x10,                    /* add    $0x10,%esp    */
    0x89, 0x04, 0x24,                    /* mov    %eax,(%esp)   * restore to old mxcsr */
    0x0f, 0xae, 0x14, 0x24,              /* ldmxcsr (%esp)       */
    0x83, 0xc4, 0x04,                    /* add    $0x4,%esp     */
    0xc3,                                /* ret */
};

static const BYTE simd_exception_test2[] = {
    0x83, 0xec, 0x4,                     /* sub $0x4, %esp       */
    0x0f, 0xae, 0x1c, 0x24,              /* stmxcsr (%esp)       */
    0x8b, 0x04, 0x24,                    /* mov    (%esp),%eax   * store mxcsr */
    0x66, 0x81, 0x24, 0x24, 0x7f, 0xff,  /* andw $0xff7f,(%esp)  * enable invalid       */
    0x0f, 0xae, 0x14, 0x24,              /* ldmxcsr (%esp)       * operation exceptions */
    0x0f, 0x57, 0xc9,                    /* xorps  %xmm1,%xmm1   * clear dividend */
    0x0f, 0x57, 0xc0,                    /* xorps  %xmm0,%xmm0   * clear divisor  */
    0x0f, 0x5e, 0xc8,                    /* divps  %xmm0,%xmm1   * generate fault */
    0x89, 0x04, 0x24,                    /* mov    %eax,(%esp)   * restore to old mxcsr */
    0x0f, 0xae, 0x14, 0x24,              /* ldmxcsr (%esp)       */
    0x83, 0xc4, 0x04,                    /* add    $0x4,%esp     */
    0xc3,                                /* ret */
};

static const BYTE sse_check[] = {
    0x0f, 0x58, 0xc8,                    /* addps  %xmm0,%xmm1 */
    0xc3,                                /* ret */
};

static void test_simd_exceptions(void)
{
    int stage;

    /* test if CPU & OS can do sse */
    stage = 1;
    got_exception = 0;
    run_exception_test(simd_fault_handler, &stage, sse_check, sizeof(sse_check), 0);
    if(got_exception) {
        skip("system doesn't support SSE\n");
        return;
    }

    /* generate a SIMD exception */
    stage = 2;
    got_exception = 0;
    run_exception_test(simd_fault_handler, &stage, simd_exception_test,
                       sizeof(simd_exception_test), 0);
    ok(got_exception == 1, "got exception: %i, should be 1\n", got_exception);

    /* generate a SIMD exception, test FPE_FLTINV */
    stage = 3;
    got_exception = 0;
    run_exception_test(simd_fault_handler, &stage, simd_exception_test2,
                       sizeof(simd_exception_test2), 0);
    ok(got_exception == 1, "got exception: %i, should be 1\n", got_exception);
}

struct fpu_exception_info
{
    DWORD exception_code;
    DWORD exception_offset;
    DWORD eip_offset;
};

static DWORD fpu_exception_handler(EXCEPTION_RECORD *rec, EXCEPTION_REGISTRATION_RECORD *frame,
        CONTEXT *context, EXCEPTION_REGISTRATION_RECORD **dispatcher)
{
    struct fpu_exception_info *info = *(struct fpu_exception_info **)(frame + 1);

    info->exception_code = rec->ExceptionCode;
    info->exception_offset = (BYTE *)rec->ExceptionAddress - (BYTE *)code_mem;
    info->eip_offset = context->Eip - (DWORD)code_mem;

    ++context->Eip;
    return ExceptionContinueExecution;
}

static void test_fpu_exceptions(void)
{
    static const BYTE fpu_exception_test_ie[] =
    {
        0x83, 0xec, 0x04,                   /* sub $0x4,%esp        */
        0x66, 0xc7, 0x04, 0x24, 0xfe, 0x03, /* movw $0x3fe,(%esp)   */
        0x9b, 0xd9, 0x7c, 0x24, 0x02,       /* fstcw 0x2(%esp)      */
        0xd9, 0x2c, 0x24,                   /* fldcw (%esp)         */
        0xd9, 0xee,                         /* fldz                 */
        0xd9, 0xe8,                         /* fld1                 */
        0xde, 0xf1,                         /* fdivp                */
        0xdd, 0xd8,                         /* fstp %st(0)          */
        0xdd, 0xd8,                         /* fstp %st(0)          */
        0x9b,                               /* fwait                */
        0xdb, 0xe2,                         /* fnclex               */
        0xd9, 0x6c, 0x24, 0x02,             /* fldcw 0x2(%esp)      */
        0x83, 0xc4, 0x04,                   /* add $0x4,%esp        */
        0xc3,                               /* ret                  */
    };

    static const BYTE fpu_exception_test_de[] =
    {
        0x83, 0xec, 0x04,                   /* sub $0x4,%esp        */
        0x66, 0xc7, 0x04, 0x24, 0xfb, 0x03, /* movw $0x3fb,(%esp)   */
        0x9b, 0xd9, 0x7c, 0x24, 0x02,       /* fstcw 0x2(%esp)      */
        0xd9, 0x2c, 0x24,                   /* fldcw (%esp)         */
        0xdd, 0xd8,                         /* fstp %st(0)          */
        0xd9, 0xee,                         /* fldz                 */
        0xd9, 0xe8,                         /* fld1                 */
        0xde, 0xf1,                         /* fdivp                */
        0x9b,                               /* fwait                */
        0xdb, 0xe2,                         /* fnclex               */
        0xdd, 0xd8,                         /* fstp %st(0)          */
        0xdd, 0xd8,                         /* fstp %st(0)          */
        0xd9, 0x6c, 0x24, 0x02,             /* fldcw 0x2(%esp)      */
        0x83, 0xc4, 0x04,                   /* add $0x4,%esp        */
        0xc3,                               /* ret                  */
    };

    struct fpu_exception_info info;

    memset(&info, 0, sizeof(info));
    run_exception_test(fpu_exception_handler, &info, fpu_exception_test_ie, sizeof(fpu_exception_test_ie), 0);
    ok(info.exception_code == EXCEPTION_FLT_STACK_CHECK,
            "Got exception code %#x, expected EXCEPTION_FLT_STACK_CHECK\n", info.exception_code);
    ok(info.exception_offset == 0x19 ||
       broken( info.exception_offset == info.eip_offset ),
       "Got exception offset %#x, expected 0x19\n", info.exception_offset);
    ok(info.eip_offset == 0x1b, "Got EIP offset %#x, expected 0x1b\n", info.eip_offset);

    memset(&info, 0, sizeof(info));
    run_exception_test(fpu_exception_handler, &info, fpu_exception_test_de, sizeof(fpu_exception_test_de), 0);
    ok(info.exception_code == EXCEPTION_FLT_DIVIDE_BY_ZERO,
            "Got exception code %#x, expected EXCEPTION_FLT_DIVIDE_BY_ZERO\n", info.exception_code);
    ok(info.exception_offset == 0x17 ||
       broken( info.exception_offset == info.eip_offset ),
       "Got exception offset %#x, expected 0x17\n", info.exception_offset);
    ok(info.eip_offset == 0x19, "Got EIP offset %#x, expected 0x19\n", info.eip_offset);
}

struct dpe_exception_info {
    BOOL exception_caught;
    DWORD exception_info;
};

static DWORD dpe_exception_handler(EXCEPTION_RECORD *rec, EXCEPTION_REGISTRATION_RECORD *frame,
        CONTEXT *context, EXCEPTION_REGISTRATION_RECORD **dispatcher)
{
    DWORD old_prot;
    struct dpe_exception_info *info = *(struct dpe_exception_info **)(frame + 1);

    ok(rec->ExceptionCode == EXCEPTION_ACCESS_VIOLATION,
       "Exception code %08x\n", rec->ExceptionCode);
    ok(rec->NumberParameters == 2,
       "Parameter count: %d\n", rec->NumberParameters);
    ok((LPVOID)rec->ExceptionInformation[1] == code_mem,
       "Exception address: %p, expected %p\n",
       (LPVOID)rec->ExceptionInformation[1], code_mem);

    info->exception_info = rec->ExceptionInformation[0];
    info->exception_caught = TRUE;

    VirtualProtect(code_mem, 1, PAGE_EXECUTE_READWRITE, &old_prot);
    return ExceptionContinueExecution;
}

static void test_dpe_exceptions(void)
{
    static const BYTE single_ret[] = {0xC3};
    struct dpe_exception_info info;
    NTSTATUS stat;
    BOOL has_hw_support;
    BOOL is_permanent = FALSE, can_test_without = TRUE, can_test_with = TRUE;
    DWORD val;
    ULONG len;

    /* Query DEP with len too small */
    stat = pNtQueryInformationProcess(GetCurrentProcess(), ProcessExecuteFlags, &val, sizeof val - 1, &len);
    if(stat == STATUS_INVALID_INFO_CLASS)
    {
        skip("This software platform does not support DEP\n");
        return;
    }
    ok(stat == STATUS_INFO_LENGTH_MISMATCH, "buffer too small: %08x\n", stat);

    /* Query DEP */
    stat = pNtQueryInformationProcess(GetCurrentProcess(), ProcessExecuteFlags, &val, sizeof val, &len);
    ok(stat == STATUS_SUCCESS, "querying DEP: status %08x\n", stat);
    if(stat == STATUS_SUCCESS)
    {
        ok(len == sizeof val, "returned length: %d\n", len);
        if(val & MEM_EXECUTE_OPTION_PERMANENT)
        {
            skip("toggling DEP impossible - status locked\n");
            is_permanent = TRUE;
            if(val & MEM_EXECUTE_OPTION_DISABLE)
                can_test_without = FALSE;
            else
                can_test_with = FALSE;
        }
    }

    if(!is_permanent)
    {
        /* Enable DEP */
        val = MEM_EXECUTE_OPTION_DISABLE;
        stat = pNtSetInformationProcess(GetCurrentProcess(), ProcessExecuteFlags, &val, sizeof val);
        ok(stat == STATUS_SUCCESS, "enabling DEP: status %08x\n", stat);
    }

    if(can_test_with)
    {
        /* Try access to locked page with DEP on*/
        info.exception_caught = FALSE;
        run_exception_test(dpe_exception_handler, &info, single_ret, sizeof(single_ret), PAGE_NOACCESS);
        ok(info.exception_caught == TRUE, "Execution of disabled memory succeeded\n");
        ok(info.exception_info == EXCEPTION_READ_FAULT ||
           info.exception_info == EXCEPTION_EXECUTE_FAULT,
              "Access violation type: %08x\n", (unsigned)info.exception_info);
        has_hw_support = info.exception_info == EXCEPTION_EXECUTE_FAULT;
        trace("DEP hardware support: %s\n", has_hw_support?"Yes":"No");

        /* Try execution of data with DEP on*/
        info.exception_caught = FALSE;
        run_exception_test(dpe_exception_handler, &info, single_ret, sizeof(single_ret), PAGE_READWRITE);
        if(has_hw_support)
        {
            ok(info.exception_caught == TRUE, "Execution of data memory succeeded\n");
            ok(info.exception_info == EXCEPTION_EXECUTE_FAULT,
                  "Access violation type: %08x\n", (unsigned)info.exception_info);
        }
        else
            ok(info.exception_caught == FALSE, "Execution trapped without hardware support\n");
    }
    else
        skip("DEP is in AlwaysOff state\n");

    if(!is_permanent)
    {
        /* Disable DEP */
        val = MEM_EXECUTE_OPTION_ENABLE;
        stat = pNtSetInformationProcess(GetCurrentProcess(), ProcessExecuteFlags, &val, sizeof val);
        ok(stat == STATUS_SUCCESS, "disabling DEP: status %08x\n", stat);
    }

    /* page is read without exec here */
    if(can_test_without)
    {
        /* Try execution of data with DEP off */
        info.exception_caught = FALSE;
        run_exception_test(dpe_exception_handler, &info, single_ret, sizeof(single_ret), PAGE_READWRITE);
        ok(info.exception_caught == FALSE, "Execution trapped with DEP turned off\n");

        /* Try access to locked page with DEP off - error code is different than
           with hardware DEP on */
        info.exception_caught = FALSE;
        run_exception_test(dpe_exception_handler, &info, single_ret, sizeof(single_ret), PAGE_NOACCESS);
        ok(info.exception_caught == TRUE, "Execution of disabled memory succeeded\n");
        ok(info.exception_info == EXCEPTION_READ_FAULT,
              "Access violation type: %08x\n", (unsigned)info.exception_info);
    }
    else
        skip("DEP is in AlwaysOn state\n");

    if(!is_permanent)
    {
        /* Turn off DEP permanently */
        val = MEM_EXECUTE_OPTION_ENABLE | MEM_EXECUTE_OPTION_PERMANENT;
        stat = pNtSetInformationProcess(GetCurrentProcess(), ProcessExecuteFlags, &val, sizeof val);
        ok(stat == STATUS_SUCCESS, "disabling DEP permanently: status %08x\n", stat);
    }

    /* Try to turn off DEP */
    val = MEM_EXECUTE_OPTION_ENABLE;
    stat = pNtSetInformationProcess(GetCurrentProcess(), ProcessExecuteFlags, &val, sizeof val);
    ok(stat == STATUS_ACCESS_DENIED, "disabling DEP while permanent: status %08x\n", stat);

    /* Try to turn on DEP */
    val = MEM_EXECUTE_OPTION_DISABLE;
    stat = pNtSetInformationProcess(GetCurrentProcess(), ProcessExecuteFlags, &val, sizeof val);
    ok(stat == STATUS_ACCESS_DENIED, "enabling DEP while permanent: status %08x\n", stat);
}

static void test_thread_context(void)
{
    CONTEXT context;
    NTSTATUS status;
    struct expected
    {
        DWORD Eax, Ebx, Ecx, Edx, Esi, Edi, Ebp, Esp, Eip,
            SegCs, SegDs, SegEs, SegFs, SegGs, SegSs, EFlags, prev_frame;
    } expect;
    NTSTATUS (*func_ptr)( struct expected *res, void *func, void *arg1, void *arg2 ) = (void *)code_mem;

    static const BYTE call_func[] =
    {
        0x55,             /* pushl  %ebp */
        0x89, 0xe5,       /* mov    %esp,%ebp */
        0x50,             /* pushl  %eax ; add a bit of offset to the stack */
        0x50,             /* pushl  %eax */
        0x50,             /* pushl  %eax */
        0x50,             /* pushl  %eax */
        0x8b, 0x45, 0x08, /* mov    0x8(%ebp),%eax */
        0x8f, 0x00,       /* popl   (%eax) */
        0x89, 0x58, 0x04, /* mov    %ebx,0x4(%eax) */
        0x89, 0x48, 0x08, /* mov    %ecx,0x8(%eax) */
        0x89, 0x50, 0x0c, /* mov    %edx,0xc(%eax) */
        0x89, 0x70, 0x10, /* mov    %esi,0x10(%eax) */
        0x89, 0x78, 0x14, /* mov    %edi,0x14(%eax) */
        0x89, 0x68, 0x18, /* mov    %ebp,0x18(%eax) */
        0x89, 0x60, 0x1c, /* mov    %esp,0x1c(%eax) */
        0xff, 0x75, 0x04, /* pushl  0x4(%ebp) */
        0x8f, 0x40, 0x20, /* popl   0x20(%eax) */
        0x8c, 0x48, 0x24, /* mov    %cs,0x24(%eax) */
        0x8c, 0x58, 0x28, /* mov    %ds,0x28(%eax) */
        0x8c, 0x40, 0x2c, /* mov    %es,0x2c(%eax) */
        0x8c, 0x60, 0x30, /* mov    %fs,0x30(%eax) */
        0x8c, 0x68, 0x34, /* mov    %gs,0x34(%eax) */
        0x8c, 0x50, 0x38, /* mov    %ss,0x38(%eax) */
        0x9c,             /* pushf */
        0x8f, 0x40, 0x3c, /* popl   0x3c(%eax) */
        0xff, 0x75, 0x00, /* pushl  0x0(%ebp) ; previous stack frame */
        0x8f, 0x40, 0x40, /* popl   0x40(%eax) */
        0x8b, 0x00,       /* mov    (%eax),%eax */
        0xff, 0x75, 0x14, /* pushl  0x14(%ebp) */
        0xff, 0x75, 0x10, /* pushl  0x10(%ebp) */
        0xff, 0x55, 0x0c, /* call   *0xc(%ebp) */
        0xc9,             /* leave */
        0xc3,             /* ret */
    };

    memcpy( func_ptr, call_func, sizeof(call_func) );

#define COMPARE(reg) \
    ok( context.reg == expect.reg, "wrong " #reg " %08x/%08x\n", context.reg, expect.reg )

    memset( &context, 0xcc, sizeof(context) );
    memset( &expect, 0xcc, sizeof(expect) );
    func_ptr( &expect, pRtlCaptureContext, &context, 0 );
    trace( "expect: eax=%08x ebx=%08x ecx=%08x edx=%08x esi=%08x edi=%08x ebp=%08x esp=%08x "
           "eip=%08x cs=%04x ds=%04x es=%04x fs=%04x gs=%04x ss=%04x flags=%08x prev=%08x\n",
           expect.Eax, expect.Ebx, expect.Ecx, expect.Edx, expect.Esi, expect.Edi,
           expect.Ebp, expect.Esp, expect.Eip, expect.SegCs, expect.SegDs, expect.SegEs,
           expect.SegFs, expect.SegGs, expect.SegSs, expect.EFlags, expect.prev_frame );
    trace( "actual: eax=%08x ebx=%08x ecx=%08x edx=%08x esi=%08x edi=%08x ebp=%08x esp=%08x "
           "eip=%08x cs=%04x ds=%04x es=%04x fs=%04x gs=%04x ss=%04x flags=%08x\n",
           context.Eax, context.Ebx, context.Ecx, context.Edx, context.Esi, context.Edi,
           context.Ebp, context.Esp, context.Eip, context.SegCs, context.SegDs, context.SegEs,
           context.SegFs, context.SegGs, context.SegSs, context.EFlags );

    ok( context.ContextFlags == (CONTEXT_CONTROL | CONTEXT_INTEGER | CONTEXT_SEGMENTS) ||
        broken( context.ContextFlags == 0xcccccccc ),  /* <= vista */
        "wrong flags %08x\n", context.ContextFlags );
    COMPARE( Eax );
    COMPARE( Ebx );
    COMPARE( Ecx );
    COMPARE( Edx );
    COMPARE( Esi );
    COMPARE( Edi );
    COMPARE( Eip );
    COMPARE( SegCs );
    COMPARE( SegDs );
    COMPARE( SegEs );
    COMPARE( SegFs );
    COMPARE( SegGs );
    COMPARE( SegSs );
    COMPARE( EFlags );
    /* Ebp is from the previous stackframe */
    ok( context.Ebp == expect.prev_frame, "wrong Ebp %08x/%08x\n", context.Ebp, expect.prev_frame );
    /* Esp is the value on entry to the previous stackframe */
    ok( context.Esp == expect.Ebp + 8, "wrong Esp %08x/%08x\n", context.Esp, expect.Ebp + 8 );

    memset( &context, 0xcc, sizeof(context) );
    memset( &expect, 0xcc, sizeof(expect) );
    context.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER | CONTEXT_SEGMENTS;
    status = func_ptr( &expect, pNtGetContextThread, (void *)GetCurrentThread(), &context );
    ok( status == STATUS_SUCCESS, "NtGetContextThread failed %08x\n", status );
    trace( "expect: eax=%08x ebx=%08x ecx=%08x edx=%08x esi=%08x edi=%08x ebp=%08x esp=%08x "
           "eip=%08x cs=%04x ds=%04x es=%04x fs=%04x gs=%04x ss=%04x flags=%08x prev=%08x\n",
           expect.Eax, expect.Ebx, expect.Ecx, expect.Edx, expect.Esi, expect.Edi,
           expect.Ebp, expect.Esp, expect.Eip, expect.SegCs, expect.SegDs, expect.SegEs,
           expect.SegFs, expect.SegGs, expect.SegSs, expect.EFlags, expect.prev_frame );
    trace( "actual: eax=%08x ebx=%08x ecx=%08x edx=%08x esi=%08x edi=%08x ebp=%08x esp=%08x "
           "eip=%08x cs=%04x ds=%04x es=%04x fs=%04x gs=%04x ss=%04x flags=%08x\n",
           context.Eax, context.Ebx, context.Ecx, context.Edx, context.Esi, context.Edi,
           context.Ebp, context.Esp, context.Eip, context.SegCs, context.SegDs, context.SegEs,
           context.SegFs, context.SegGs, context.SegSs, context.EFlags );
    /* Eax, Ecx, Edx, EFlags are not preserved */
    COMPARE( Ebx );
    COMPARE( Esi );
    COMPARE( Edi );
    COMPARE( Ebp );
    /* Esp is the stack upon entry to NtGetContextThread */
    ok( context.Esp == expect.Esp - 12 || context.Esp == expect.Esp - 16,
        "wrong Esp %08x/%08x\n", context.Esp, expect.Esp );
    /* Eip is somewhere close to the NtGetContextThread implementation */
    ok( (char *)context.Eip >= (char *)pNtGetContextThread - 0x10000 &&
        (char *)context.Eip <= (char *)pNtGetContextThread + 0x10000,
        "wrong Eip %08x/%08x\n", context.Eip, (DWORD)pNtGetContextThread );
    ok( *(WORD *)context.Eip == 0xc483 || *(WORD *)context.Eip == 0x08c2 || *(WORD *)context.Eip == 0x8dc3,
        "expected 0xc483 or 0x08c2 or 0x8dc3, got %04x\n", *(WORD *)context.Eip );
    /* segment registers clear the high word */
    ok( context.SegCs == LOWORD(expect.SegCs), "wrong SegCs %08x/%08x\n", context.SegCs, expect.SegCs );
    ok( context.SegDs == LOWORD(expect.SegDs), "wrong SegDs %08x/%08x\n", context.SegDs, expect.SegDs );
    ok( context.SegEs == LOWORD(expect.SegEs), "wrong SegEs %08x/%08x\n", context.SegEs, expect.SegEs );
    ok( context.SegFs == LOWORD(expect.SegFs), "wrong SegFs %08x/%08x\n", context.SegFs, expect.SegFs );
    ok( context.SegGs == LOWORD(expect.SegGs), "wrong SegGs %08x/%08x\n", context.SegGs, expect.SegGs );
    ok( context.SegSs == LOWORD(expect.SegSs), "wrong SegSs %08x/%08x\n", context.SegSs, expect.SegGs );
#undef COMPARE
}

#elif defined(__x86_64__)

#define is_wow64 0

#ifndef __REACTOS__
#define UNW_FLAG_NHANDLER  0
#define UNW_FLAG_EHANDLER  1
#define UNW_FLAG_UHANDLER  2
#define UNW_FLAG_CHAININFO 4
#endif // __REACTOS__

#define UWOP_PUSH_NONVOL     0
#define UWOP_ALLOC_LARGE     1
#define UWOP_ALLOC_SMALL     2
#define UWOP_SET_FPREG       3
#define UWOP_SAVE_NONVOL     4
#define UWOP_SAVE_NONVOL_FAR 5
#define UWOP_SAVE_XMM128     8
#define UWOP_SAVE_XMM128_FAR 9
#define UWOP_PUSH_MACHFRAME  10

struct results
{
    int rip_offset;   /* rip offset from code start */
    int rbp_offset;   /* rbp offset from stack pointer */
    int handler;      /* expect handler to be set? */
    int rip;          /* expected final rip value */
    int frame;        /* expected frame return value */
    int regs[8][2];   /* expected values for registers */
};

struct unwind_test
{
    const BYTE *function;
    size_t function_size;
    const BYTE *unwind_info;
    const struct results *results;
    unsigned int nb_results;
};

enum regs
{
    rax, rcx, rdx, rbx, rsp, rbp, rsi, rdi,
    r8,  r9,  r10, r11, r12, r13, r14, r15
};

static const char * const reg_names[16] =
{
    "rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi",
    "r8",  "r9",  "r10", "r11", "r12", "r13", "r14", "r15"
};

#define UWOP(code,info) (UWOP_##code | ((info) << 4))

static void call_virtual_unwind( int testnum, const struct unwind_test *test )
{
    static const int code_offset = 1024;
    static const int unwind_offset = 2048;
    void *handler, *data;
    CONTEXT context;
    RUNTIME_FUNCTION runtime_func;
    KNONVOLATILE_CONTEXT_POINTERS ctx_ptr;
    UINT i, j, k;
    ULONG64 fake_stack[256];
    ULONG64 frame, orig_rip, orig_rbp, unset_reg;
    UINT unwind_size = 4 + 2 * test->unwind_info[2] + 8;

    memcpy( (char *)code_mem + code_offset, test->function, test->function_size );
    memcpy( (char *)code_mem + unwind_offset, test->unwind_info, unwind_size );

    runtime_func.BeginAddress = code_offset;
    runtime_func.EndAddress = code_offset + test->function_size;
    runtime_func.UnwindData = unwind_offset;

    trace( "code: %p stack: %p\n", code_mem, fake_stack );

    for (i = 0; i < test->nb_results; i++)
    {
        memset( &ctx_ptr, 0, sizeof(ctx_ptr) );
        memset( &context, 0x55, sizeof(context) );
        memset( &unset_reg, 0x55, sizeof(unset_reg) );
        for (j = 0; j < 256; j++) fake_stack[j] = j * 8;

        context.Rsp = (ULONG_PTR)fake_stack;
        context.Rbp = (ULONG_PTR)fake_stack + test->results[i].rbp_offset;
        orig_rbp = context.Rbp;
        orig_rip = (ULONG64)code_mem + code_offset + test->results[i].rip_offset;

        trace( "%u/%u: rip=%p (%02x) rbp=%p rsp=%p\n", testnum, i,
               (void *)orig_rip, *(BYTE *)orig_rip, (void *)orig_rbp, (void *)context.Rsp );

        data = (void *)0xdeadbeef;
        handler = RtlVirtualUnwind( UNW_FLAG_EHANDLER, (ULONG64)code_mem, orig_rip,
                                    &runtime_func, &context, &data, &frame, &ctx_ptr );
        if (test->results[i].handler)
        {
            ok( (char *)handler == (char *)code_mem + 0x200,
                "%u/%u: wrong handler %p/%p\n", testnum, i, handler, (char *)code_mem + 0x200 );
            if (handler) ok( *(DWORD *)data == 0x08070605,
                             "%u/%u: wrong handler data %p\n", testnum, i, data );
        }
        else
        {
            ok( handler == NULL, "%u/%u: handler %p instead of NULL\n", testnum, i, handler );
            ok( data == (void *)0xdeadbeef, "%u/%u: handler data set to %p\n", testnum, i, data );
        }

        ok( context.Rip == test->results[i].rip, "%u/%u: wrong rip %p/%x\n",
            testnum, i, (void *)context.Rip, test->results[i].rip );
        ok( frame == (ULONG64)fake_stack + test->results[i].frame, "%u/%u: wrong frame %p/%p\n",
            testnum, i, (void *)frame, (char *)fake_stack + test->results[i].frame );

        for (j = 0; j < 16; j++)
        {
            static const UINT nb_regs = sizeof(test->results[i].regs) / sizeof(test->results[i].regs[0]);

            for (k = 0; k < nb_regs; k++)
            {
                if (test->results[i].regs[k][0] == -1)
                {
                    k = nb_regs;
                    break;
                }
                if (test->results[i].regs[k][0] == j) break;
            }

            if (j == rsp)  /* rsp is special */
            {
                ok( !ctx_ptr.u2.IntegerContext[j],
                    "%u/%u: rsp should not be set in ctx_ptr\n", testnum, i );

                ok( context.Rsp == (ULONG64)fake_stack + test->results[i].regs[k][1],
                    "%u/%u: register rsp wrong %p/%p\n",
                    testnum, i, (void *)context.Rsp, (char *)fake_stack + test->results[i].regs[k][1] );
                continue;
            }

            if (ctx_ptr.u2.IntegerContext[j])
            {
                ok( k < nb_regs, "%u/%u: register %s should not be set to %lx\n",
                    testnum, i, reg_names[j], *(&context.Rax + j) );
                if (k < nb_regs)
                    ok( *(&context.Rax + j) == test->results[i].regs[k][1],
                        "%u/%u: register %s wrong %p/%x\n",
                        testnum, i, reg_names[j], (void *)*(&context.Rax + j), test->results[i].regs[k][1] );
            }
            else
            {
                ok( k == nb_regs, "%u/%u: register %s should be set\n", testnum, i, reg_names[j] );
                if (j == rbp)
                    ok( context.Rbp == orig_rbp, "%u/%u: register rbp wrong %p/unset\n",
                        testnum, i, (void *)context.Rbp );
                else
                    ok( *(&context.Rax + j) == unset_reg,
                        "%u/%u: register %s wrong %p/unset\n",
                        testnum, i, reg_names[j], (void *)*(&context.Rax + j));
            }
        }
    }
}

static void test_virtual_unwind(void)
{
    static const BYTE function_0[] =
    {
        0xff, 0xf5,                                  /* 00: push %rbp */
        0x48, 0x81, 0xec, 0x10, 0x01, 0x00, 0x00,    /* 02: sub $0x110,%rsp */
        0x48, 0x8d, 0x6c, 0x24, 0x30,                /* 09: lea 0x30(%rsp),%rbp */
        0x48, 0x89, 0x9d, 0xf0, 0x00, 0x00, 0x00,    /* 0e: mov %rbx,0xf0(%rbp) */
        0x48, 0x89, 0xb5, 0xf8, 0x00, 0x00, 0x00,    /* 15: mov %rsi,0xf8(%rbp) */
        0x90,                                        /* 1c: nop */
        0x48, 0x8b, 0x9d, 0xf0, 0x00, 0x00, 0x00,    /* 1d: mov 0xf0(%rbp),%rbx */
        0x48, 0x8b, 0xb5, 0xf8, 0x00, 0x00, 0x00,    /* 24: mov 0xf8(%rbp),%rsi */
        0x48, 0x8d, 0xa5, 0xe0, 0x00, 0x00, 0x00,    /* 2b: lea 0xe0(%rbp),%rsp */
        0x5d,                                        /* 32: pop %rbp */
        0xc3                                         /* 33: ret */
    };

    static const BYTE unwind_info_0[] =
    {
        1 | (UNW_FLAG_EHANDLER << 3),  /* version + flags */
        0x1c,                          /* prolog size */
        8,                             /* opcode count */
        (0x03 << 4) | rbp,             /* frame reg rbp offset 0x30 */

        0x1c, UWOP(SAVE_NONVOL, rsi), 0x25, 0, /* 1c: mov %rsi,0x128(%rsp) */
        0x15, UWOP(SAVE_NONVOL, rbx), 0x24, 0, /* 15: mov %rbx,0x120(%rsp) */
        0x0e, UWOP(SET_FPREG, rbp),            /* 0e: lea 0x30(%rsp),rbp */
        0x09, UWOP(ALLOC_LARGE, 0), 0x22, 0,   /* 09: sub $0x110,%rsp */
        0x02, UWOP(PUSH_NONVOL, rbp),          /* 02: push %rbp */

        0x00, 0x02, 0x00, 0x00,  /* handler */
        0x05, 0x06, 0x07, 0x08,  /* data */
    };

    static const struct results results_0[] =
    {
      /* offset  rbp   handler  rip   frame   registers */
        { 0x00,  0x40,  FALSE, 0x000, 0x000, { {rsp,0x008}, {-1,-1} }},
        { 0x02,  0x40,  FALSE, 0x008, 0x000, { {rsp,0x010}, {rbp,0x000}, {-1,-1} }},
        { 0x09,  0x40,  FALSE, 0x118, 0x000, { {rsp,0x120}, {rbp,0x110}, {-1,-1} }},
        { 0x0e,  0x40,  FALSE, 0x128, 0x010, { {rsp,0x130}, {rbp,0x120}, {-1,-1} }},
        { 0x15,  0x40,  FALSE, 0x128, 0x010, { {rsp,0x130}, {rbp,0x120}, {rbx,0x130}, {-1,-1} }},
        { 0x1c,  0x40,  TRUE,  0x128, 0x010, { {rsp,0x130}, {rbp,0x120}, {rbx,0x130}, {rsi,0x138}, {-1,-1}}},
        { 0x1d,  0x40,  TRUE,  0x128, 0x010, { {rsp,0x130}, {rbp,0x120}, {rbx,0x130}, {rsi,0x138}, {-1,-1}}},
        { 0x24,  0x40,  TRUE,  0x128, 0x010, { {rsp,0x130}, {rbp,0x120}, {rbx,0x130}, {rsi,0x138}, {-1,-1}}},
        { 0x2b,  0x40,  FALSE, 0x128, 0x010, { {rsp,0x130}, {rbp,0x120}, {-1,-1}}},
        { 0x32,  0x40,  FALSE, 0x008, 0x010, { {rsp,0x010}, {rbp,0x000}, {-1,-1}}},
        { 0x33,  0x40,  FALSE, 0x000, 0x010, { {rsp,0x008}, {-1,-1}}},
    };


    static const BYTE function_1[] =
    {
        0x53,                     /* 00: push %rbx */
        0x55,                     /* 01: push %rbp */
        0x56,                     /* 02: push %rsi */
        0x57,                     /* 03: push %rdi */
        0x41, 0x54,               /* 04: push %r12 */
        0x48, 0x83, 0xec, 0x30,   /* 06: sub $0x30,%rsp */
        0x90, 0x90,               /* 0a: nop; nop */
        0x48, 0x83, 0xc4, 0x30,   /* 0c: add $0x30,%rsp */
        0x41, 0x5c,               /* 10: pop %r12 */
        0x5f,                     /* 12: pop %rdi */
        0x5e,                     /* 13: pop %rsi */
        0x5d,                     /* 14: pop %rbp */
        0x5b,                     /* 15: pop %rbx */
        0xc3                      /* 16: ret */
     };

    static const BYTE unwind_info_1[] =
    {
        1 | (UNW_FLAG_EHANDLER << 3),  /* version + flags */
        0x0a,                          /* prolog size */
        6,                             /* opcode count */
        0,                             /* frame reg */

        0x0a, UWOP(ALLOC_SMALL, 5),   /* 0a: sub $0x30,%rsp */
        0x06, UWOP(PUSH_NONVOL, r12), /* 06: push %r12 */
        0x04, UWOP(PUSH_NONVOL, rdi), /* 04: push %rdi */
        0x03, UWOP(PUSH_NONVOL, rsi), /* 03: push %rsi */
        0x02, UWOP(PUSH_NONVOL, rbp), /* 02: push %rbp */
        0x01, UWOP(PUSH_NONVOL, rbx), /* 01: push %rbx */

        0x00, 0x02, 0x00, 0x00,  /* handler */
        0x05, 0x06, 0x07, 0x08,  /* data */
    };

    static const struct results results_1[] =
    {
      /* offset  rbp   handler  rip   frame   registers */
        { 0x00,  0x50,  FALSE, 0x000, 0x000, { {rsp,0x008}, {-1,-1} }},
        { 0x01,  0x50,  FALSE, 0x008, 0x000, { {rsp,0x010}, {rbx,0x000}, {-1,-1} }},
        { 0x02,  0x50,  FALSE, 0x010, 0x000, { {rsp,0x018}, {rbx,0x008}, {rbp,0x000}, {-1,-1} }},
        { 0x03,  0x50,  FALSE, 0x018, 0x000, { {rsp,0x020}, {rbx,0x010}, {rbp,0x008}, {rsi,0x000}, {-1,-1} }},
        { 0x04,  0x50,  FALSE, 0x020, 0x000, { {rsp,0x028}, {rbx,0x018}, {rbp,0x010}, {rsi,0x008}, {rdi,0x000}, {-1,-1} }},
        { 0x06,  0x50,  FALSE, 0x028, 0x000, { {rsp,0x030}, {rbx,0x020}, {rbp,0x018}, {rsi,0x010}, {rdi,0x008}, {r12,0x000}, {-1,-1} }},
        { 0x0a,  0x50,  TRUE,  0x058, 0x000, { {rsp,0x060}, {rbx,0x050}, {rbp,0x048}, {rsi,0x040}, {rdi,0x038}, {r12,0x030}, {-1,-1} }},
        { 0x0c,  0x50,  FALSE, 0x058, 0x000, { {rsp,0x060}, {rbx,0x050}, {rbp,0x048}, {rsi,0x040}, {rdi,0x038}, {r12,0x030}, {-1,-1} }},
        { 0x10,  0x50,  FALSE, 0x028, 0x000, { {rsp,0x030}, {rbx,0x020}, {rbp,0x018}, {rsi,0x010}, {rdi,0x008}, {r12,0x000}, {-1,-1} }},
        { 0x12,  0x50,  FALSE, 0x020, 0x000, { {rsp,0x028}, {rbx,0x018}, {rbp,0x010}, {rsi,0x008}, {rdi,0x000}, {-1,-1} }},
        { 0x13,  0x50,  FALSE, 0x018, 0x000, { {rsp,0x020}, {rbx,0x010}, {rbp,0x008}, {rsi,0x000}, {-1,-1} }},
        { 0x14,  0x50,  FALSE, 0x010, 0x000, { {rsp,0x018}, {rbx,0x008}, {rbp,0x000}, {-1,-1} }},
        { 0x15,  0x50,  FALSE, 0x008, 0x000, { {rsp,0x010}, {rbx,0x000}, {-1,-1} }},
        { 0x16,  0x50,  FALSE, 0x000, 0x000, { {rsp,0x008}, {-1,-1} }},
    };

    static const struct unwind_test tests[] =
    {
        { function_0, sizeof(function_0), unwind_info_0,
          results_0, sizeof(results_0)/sizeof(results_0[0]) },
        { function_1, sizeof(function_1), unwind_info_1,
          results_1, sizeof(results_1)/sizeof(results_1[0]) }
    };
    unsigned int i;

    for (i = 0; i < sizeof(tests)/sizeof(tests[0]); i++)
        call_virtual_unwind( i, &tests[i] );
}

static int consolidate_dummy_called;
static PVOID CALLBACK test_consolidate_dummy(EXCEPTION_RECORD *rec)
{
    CONTEXT *ctx = (CONTEXT *)rec->ExceptionInformation[1];
    consolidate_dummy_called = 1;
    ok(ctx->Rip == 0xdeadbeef, "test_consolidate_dummy failed for Rip, expected: 0xdeadbeef, got: %lx\n", ctx->Rip);
    return (PVOID)rec->ExceptionInformation[2];
}

static void test_restore_context(void)
{
    SETJMP_FLOAT128 *fltsave;
    EXCEPTION_RECORD rec;
    _JUMP_BUFFER buf;
    CONTEXT ctx;
    int i;
    volatile LONG pass;

    if (!pRtlUnwindEx || !pRtlRestoreContext || !pRtlCaptureContext || !p_setjmp)
    {
        skip("RtlUnwindEx/RtlCaptureContext/RtlRestoreContext/_setjmp not found\n");
        return;
    }

    /* RtlRestoreContext(NULL, NULL); crashes on Windows */

    /* test simple case of capture and restore context */
    pass = 0;
    InterlockedIncrement(&pass); /* interlocked to prevent compiler from moving after capture */
    pRtlCaptureContext(&ctx);
    if (InterlockedIncrement(&pass) == 2) /* interlocked to prevent compiler from moving before capture */
    {
        pRtlRestoreContext(&ctx, NULL);
        ok(0, "shouldn't be reached\n");
    }
    else
        ok(pass < 4, "unexpected pass %ld\n", (LONG)pass);

    /* test with jmp using RltRestoreContext */
    pass = 0;
    InterlockedIncrement(&pass);
    RtlCaptureContext(&ctx);
    InterlockedIncrement(&pass); /* only called once */
    p_setjmp(&buf);
    InterlockedIncrement(&pass);
    if (pass == 3)
    {
        rec.ExceptionCode = STATUS_LONGJUMP;
        rec.NumberParameters = 1;
        rec.ExceptionInformation[0] = (DWORD64)&buf;

        /* uses buf.Rip instead of ctx.Rip */
        pRtlRestoreContext(&ctx, &rec);
        ok(0, "shouldn't be reached\n");
    }
    else if (pass == 4)
    {
        ok(buf.Rbx == ctx.Rbx, "longjmp failed for Rbx, expected: %lx, got: %lx\n", buf.Rbx, ctx.Rbx);
        ok(buf.Rsp == ctx.Rsp, "longjmp failed for Rsp, expected: %lx, got: %lx\n", buf.Rsp, ctx.Rsp);
        ok(buf.Rbp == ctx.Rbp, "longjmp failed for Rbp, expected: %lx, got: %lx\n", buf.Rbp, ctx.Rbp);
        ok(buf.Rsi == ctx.Rsi, "longjmp failed for Rsi, expected: %lx, got: %lx\n", buf.Rsi, ctx.Rsi);
        ok(buf.Rdi == ctx.Rdi, "longjmp failed for Rdi, expected: %lx, got: %lx\n", buf.Rdi, ctx.Rdi);
        ok(buf.R12 == ctx.R12, "longjmp failed for R12, expected: %lx, got: %lx\n", buf.R12, ctx.R12);
        ok(buf.R13 == ctx.R13, "longjmp failed for R13, expected: %lx, got: %lx\n", buf.R13, ctx.R13);
        ok(buf.R14 == ctx.R14, "longjmp failed for R14, expected: %lx, got: %lx\n", buf.R14, ctx.R14);
        ok(buf.R15 == ctx.R15, "longjmp failed for R15, expected: %lx, got: %lx\n", buf.R15, ctx.R15);

        fltsave = &buf.Xmm6;
        for (i = 0; i < 10; i++)
        {
            ok(fltsave[i].Part[0] == ctx.u.FltSave.XmmRegisters[i + 6].Low,
                "longjmp failed for Xmm%d, expected %lx, got %lx\n", i + 6,
                fltsave[i].Part[0], ctx.u.FltSave.XmmRegisters[i + 6].Low);

            ok(fltsave[i].Part[1] == ctx.u.FltSave.XmmRegisters[i + 6].High,
                "longjmp failed for Xmm%d, expected %lx, got %lx\n", i + 6,
                fltsave[i].Part[1], ctx.u.FltSave.XmmRegisters[i + 6].High);
        }
    }
    else
        ok(0, "unexpected pass %ld\n", (LONG)pass);

    /* test with jmp through RtlUnwindEx */
    pass = 0;
    InterlockedIncrement(&pass);
    pRtlCaptureContext(&ctx);
    InterlockedIncrement(&pass); /* only called once */
    p_setjmp(&buf);
    InterlockedIncrement(&pass);
    if (pass == 3)
    {
        rec.ExceptionCode = STATUS_LONGJUMP;
        rec.NumberParameters = 1;
        rec.ExceptionInformation[0] = (DWORD64)&buf;

        /* uses buf.Rip instead of bogus 0xdeadbeef */
        pRtlUnwindEx((void*)buf.Rsp, (void*)0xdeadbeef, &rec, NULL, &ctx, NULL);
        ok(0, "shouldn't be reached\n");
    }
    else
        ok(pass == 4, "unexpected pass %ld\n", (LONG)pass);


    /* test with consolidate */
    pass = 0;
    InterlockedIncrement(&pass);
    RtlCaptureContext(&ctx);
    InterlockedIncrement(&pass);
    if (pass == 2)
    {
        rec.ExceptionCode = STATUS_UNWIND_CONSOLIDATE;
        rec.NumberParameters = 3;
        rec.ExceptionInformation[0] = (DWORD64)test_consolidate_dummy;
        rec.ExceptionInformation[1] = (DWORD64)&ctx;
        rec.ExceptionInformation[2] = ctx.Rip;
        ctx.Rip = 0xdeadbeef;

        pRtlRestoreContext(&ctx, &rec);
        ok(0, "shouldn't be reached\n");
    }
    else if (pass == 3)
        ok(consolidate_dummy_called, "test_consolidate_dummy not called\n");
    else
        ok(0, "unexpected pass %ld\n", (LONG)pass);
}

static RUNTIME_FUNCTION* CALLBACK dynamic_unwind_callback( DWORD64 pc, PVOID context )
{
    static const int code_offset = 1024;
    static RUNTIME_FUNCTION runtime_func;
    (*(DWORD *)context)++;

    runtime_func.BeginAddress = code_offset + 16;
    runtime_func.EndAddress   = code_offset + 32;
    runtime_func.UnwindData   = 0;
    return &runtime_func;
}

static void test_dynamic_unwind(void)
{
    static const int code_offset = 1024;
    char buf[sizeof(RUNTIME_FUNCTION) + 4];
    RUNTIME_FUNCTION *runtime_func, *func;
    ULONG_PTR table, base;
    DWORD count;

    /* Test RtlAddFunctionTable with aligned RUNTIME_FUNCTION pointer */
    runtime_func = (RUNTIME_FUNCTION *)buf;
    runtime_func->BeginAddress = code_offset;
    runtime_func->EndAddress   = code_offset + 16;
    runtime_func->UnwindData   = 0;
    ok( pRtlAddFunctionTable( runtime_func, 1, (ULONG_PTR)code_mem ),
        "RtlAddFunctionTable failed for runtime_func = %p (aligned)\n", runtime_func );

    /* Lookup function outside of any function table */
    base = 0xdeadbeef;
    func = pRtlLookupFunctionEntry( (ULONG_PTR)code_mem + code_offset + 16, &base, NULL );
    ok( func == NULL,
        "RtlLookupFunctionEntry returned unexpected function, expected: NULL, got: %p\n", func );
    ok( !base || broken(base == 0xdeadbeef),
        "RtlLookupFunctionEntry modified base address, expected: 0, got: %lx\n", base );

    /* Test with pointer inside of our function */
    base = 0xdeadbeef;
    func = pRtlLookupFunctionEntry( (ULONG_PTR)code_mem + code_offset + 8, &base, NULL );
    ok( func == runtime_func,
        "RtlLookupFunctionEntry didn't return expected function, expected: %p, got: %p\n", runtime_func, func );
    ok( base == (ULONG_PTR)code_mem,
        "RtlLookupFunctionEntry returned invalid base, expected: %lx, got: %lx\n", (ULONG_PTR)code_mem, base );

    /* Test RtlDeleteFunctionTable */
    ok( pRtlDeleteFunctionTable( runtime_func ),
        "RtlDeleteFunctionTable failed for runtime_func = %p (aligned)\n", runtime_func );
    ok( !pRtlDeleteFunctionTable( runtime_func ),
        "RtlDeleteFunctionTable returned success for nonexistent table runtime_func = %p\n", runtime_func );

    /* Unaligned RUNTIME_FUNCTION pointer */
    runtime_func = (RUNTIME_FUNCTION *)((ULONG_PTR)buf | 0x3);
    runtime_func->BeginAddress = code_offset;
    runtime_func->EndAddress   = code_offset + 16;
    runtime_func->UnwindData   = 0;
    ok( pRtlAddFunctionTable( runtime_func, 1, (ULONG_PTR)code_mem ),
        "RtlAddFunctionTable failed for runtime_func = %p (unaligned)\n", runtime_func );
    ok( pRtlDeleteFunctionTable( runtime_func ),
        "RtlDeleteFunctionTable failed for runtime_func = %p (unaligned)\n", runtime_func );

    /* Attempt to insert the same entry twice */
    runtime_func = (RUNTIME_FUNCTION *)buf;
    runtime_func->BeginAddress = code_offset;
    runtime_func->EndAddress   = code_offset + 16;
    runtime_func->UnwindData   = 0;
    ok( pRtlAddFunctionTable( runtime_func, 1, (ULONG_PTR)code_mem ),
        "RtlAddFunctionTable failed for runtime_func = %p (first attempt)\n", runtime_func );
    ok( pRtlAddFunctionTable( runtime_func, 1, (ULONG_PTR)code_mem ),
        "RtlAddFunctionTable failed for runtime_func = %p (second attempt)\n", runtime_func );
    ok( pRtlDeleteFunctionTable( runtime_func ),
        "RtlDeleteFunctionTable failed for runtime_func = %p (first attempt)\n", runtime_func );
    ok( pRtlDeleteFunctionTable( runtime_func ),
        "RtlDeleteFunctionTable failed for runtime_func = %p (second attempt)\n", runtime_func );
    ok( !pRtlDeleteFunctionTable( runtime_func ),
        "RtlDeleteFunctionTable returned success for nonexistent table runtime_func = %p\n", runtime_func );

    /* Test RtlInstallFunctionTableCallback with both low bits unset */
    table = (ULONG_PTR)code_mem;
    ok( !pRtlInstallFunctionTableCallback( table, (ULONG_PTR)code_mem, code_offset + 32, &dynamic_unwind_callback, (PVOID*)&count, NULL ),
        "RtlInstallFunctionTableCallback returned success for table = %lx\n", table );

    /* Test RtlInstallFunctionTableCallback with both low bits set */
    table = (ULONG_PTR)code_mem | 0x3;
    ok( pRtlInstallFunctionTableCallback( table, (ULONG_PTR)code_mem, code_offset + 32, &dynamic_unwind_callback, (PVOID*)&count, NULL ),
        "RtlInstallFunctionTableCallback failed for table = %lx\n", table );

    /* Lookup function outside of any function table */
    count = 0;
    base = 0xdeadbeef;
    func = pRtlLookupFunctionEntry( (ULONG_PTR)code_mem + code_offset + 32, &base, NULL );
    ok( func == NULL,
        "RtlLookupFunctionEntry returned unexpected function, expected: NULL, got: %p\n", func );
    ok( !base || broken(base == 0xdeadbeef),
        "RtlLookupFunctionEntry modified base address, expected: 0, got: %lx\n", base );
    ok( !count,
        "RtlLookupFunctionEntry issued %d unexpected calls to dynamic_unwind_callback\n", count );

    /* Test with pointer inside of our function */
    count = 0;
    base = 0xdeadbeef;
    func = pRtlLookupFunctionEntry( (ULONG_PTR)code_mem + code_offset + 24, &base, NULL );
    ok( func != NULL && func->BeginAddress == code_offset + 16 && func->EndAddress == code_offset + 32,
        "RtlLookupFunctionEntry didn't return expected function, got: %p\n", func );
    ok( base == (ULONG_PTR)code_mem,
        "RtlLookupFunctionEntry returned invalid base, expected: %lx, got: %lx\n", (ULONG_PTR)code_mem, base );
    ok( count == 1,
        "RtlLookupFunctionEntry issued %d calls to dynamic_unwind_callback, expected: 1\n", count );

    /* Clean up again */
    ok( pRtlDeleteFunctionTable( (PRUNTIME_FUNCTION)table ),
        "RtlDeleteFunctionTable failed for table = %p\n", (PVOID)table );
    ok( !pRtlDeleteFunctionTable( (PRUNTIME_FUNCTION)table ),
        "RtlDeleteFunctionTable returned success for nonexistent table = %p\n", (PVOID)table );

}

static int termination_handler_called;
static void WINAPI termination_handler(ULONG flags, ULONG64 frame)
{
    termination_handler_called++;

    ok(flags == 1 || broken(flags == 0x401), "flags = %x\n", flags);
    ok(frame == 0x1234, "frame = %p\n", (void*)frame);
}

static void test___C_specific_handler(void)
{
    DISPATCHER_CONTEXT dispatch;
    EXCEPTION_RECORD rec;
    CONTEXT context;
    ULONG64 frame;
    EXCEPTION_DISPOSITION ret;
    SCOPE_TABLE scope_table;

    if (!p__C_specific_handler)
    {
        win_skip("__C_specific_handler not available\n");
        return;
    }

    memset(&rec, 0, sizeof(rec));
    rec.ExceptionFlags = 2; /* EH_UNWINDING */
    frame = 0x1234;
    memset(&dispatch, 0, sizeof(dispatch));
    dispatch.ImageBase = (ULONG_PTR)GetModuleHandleA(NULL);
    dispatch.ControlPc = dispatch.ImageBase + 0x200;
    dispatch.HandlerData = &scope_table;
    dispatch.ContextRecord = &context;
    scope_table.Count = 1;
    scope_table.ScopeRecord[0].BeginAddress = 0x200;
    scope_table.ScopeRecord[0].EndAddress = 0x400;
#ifndef __REACTOS__
    scope_table.ScopeRecord[0].HandlerAddress = (ULONG_PTR)termination_handler-dispatch.ImageBase;
#else
    scope_table.ScopeRecord[0].HandlerAddress = ((ULONG_PTR)termination_handler - (ULONG_PTR)dispatch.ImageBase);
#endif 
    scope_table.ScopeRecord[0].JumpTarget = 0;
    memset(&context, 0, sizeof(context));

    termination_handler_called = 0;
    ret = p__C_specific_handler(&rec, frame, &context, &dispatch);
    ok(ret == ExceptionContinueSearch, "__C_specific_handler returned %x\n", ret);
    ok(termination_handler_called == 1, "termination_handler_called = %d\n",
            termination_handler_called);
    ok(dispatch.ScopeIndex == 1, "dispatch.ScopeIndex = %d\n", dispatch.ScopeIndex);

    ret = p__C_specific_handler(&rec, frame, &context, &dispatch);
    ok(ret == ExceptionContinueSearch, "__C_specific_handler returned %x\n", ret);
    ok(termination_handler_called == 1, "termination_handler_called = %d\n",
            termination_handler_called);
    ok(dispatch.ScopeIndex == 1, "dispatch.ScopeIndex = %d\n", dispatch.ScopeIndex);
}

#elif defined(__aarch64__)


static void test_thread_context(void)
{
    CONTEXT context;
    NTSTATUS status;
    struct expected
    {
        ULONG64 X0, X1, X2, X3, X4, X5, X6, X7, X8, X9, X10, X11, X12, X13, X14, X15, X16,
            X17, X18, X19, X20, X21, X22, X23, X24, X25, X26, X27, X28, Fp, Lr, Sp, Pc;
        ULONG Cpsr, Fpcr, Fpsr;
    } expect;
    NTSTATUS (*func_ptr)( void *arg1, void *arg2, struct expected *res, void *func ) = code_mem;

    static const DWORD call_func[] =
    {
        0xa9bf7bfd,  /* stp     x29, x30, [sp, #-16]! */
        0xa9000440,  /* stp     x0, x1, [x2] */
        0xa9010c42,  /* stp     x2, x3, [x2, #16] */
        0xa9021444,  /* stp     x4, x5, [x2, #32] */
        0xa9031c46,  /* stp     x6, x7, [x2, #48] */
        0xa9042448,  /* stp     x8, x9, [x2, #64] */
        0xa9052c4a,  /* stp     x10, x11, [x2, #80] */
        0xa906344c,  /* stp     x12, x13, [x2, #96] */
        0xa9073c4e,  /* stp     x14, x15, [x2, #112] */
        0xa9084450,  /* stp     x16, x17, [x2, #128] */
        0xa9094c52,  /* stp     x18, x19, [x2, #144] */
        0xa90a5454,  /* stp     x20, x21, [x2, #160] */
        0xa90b5c56,  /* stp     x22, x23, [x2, #176] */
        0xa90c6458,  /* stp     x24, x25, [x2, #192] */
        0xa90d6c5a,  /* stp     x26, x27, [x2, #208] */
        0xa90e745c,  /* stp     x28, x29, [x2, #224] */
        0xf900785e,  /* str     x30, [x2, #240] */
        0x910003e1,  /* mov     x1, sp */
        0xf9007c41,  /* str     x1, [x2, #248] */
        0x90000001,  /* adrp    x1, 1f */
        0x9101e021,  /* add     x1, x1, #:lo12:1f */
        0xf9008041,  /* str     x1, [x2, #256] */
        0xd53b4201,  /* mrs     x1, nzcv */
        0xb9010841,  /* str     w1, [x2, #264] */
        0xd53b4401,  /* mrs     x1, fpcr */
        0xb9010c41,  /* str     w1, [x2, #268] */
        0xd53b4421,  /* mrs     x1, fpsr */
        0xb9011041,  /* str     w1, [x2, #272] */
        0xf9400441,  /* ldr     x1, [x2, #8] */
        0xd63f0060,  /* blr     x3 */
        0xa8c17bfd,  /* 1: ldp     x29, x30, [sp], #16 */
        0xd65f03c0,  /* ret */
     };

    memcpy( func_ptr, call_func, sizeof(call_func) );

#define COMPARE(reg) \
    ok( context.reg == expect.reg, "wrong " #reg " %p/%p\n", (void *)(ULONG64)context.reg, (void *)(ULONG64)expect.reg )

    memset( &context, 0xcc, sizeof(context) );
    memset( &expect, 0xcc, sizeof(expect) );
    func_ptr( &context, 0, &expect, pRtlCaptureContext );
    trace( "expect: x0=%p x1=%p x2=%p x3=%p x4=%p x5=%p x6=%p x7=%p x8=%p x9=%p x10=%p x11=%p x12=%p x13=%p x14=%p x15=%p x16=%p x17=%p x18=%p x19=%p x20=%p x21=%p x22=%p x23=%p x24=%p x25=%p x26=%p x27=%p x28=%p fp=%p lr=%p sp=%p pc=%p cpsr=%08lx\n",
           (void *)expect.X0, (void *)expect.X1, (void *)expect.X2, (void *)expect.X3,
           (void *)expect.X4, (void *)expect.X5, (void *)expect.X6, (void *)expect.X7,
           (void *)expect.X8, (void *)expect.X9, (void *)expect.X10, (void *)expect.X11,
           (void *)expect.X12, (void *)expect.X13, (void *)expect.X14, (void *)expect.X15,
           (void *)expect.X16, (void *)expect.X17, (void *)expect.X18, (void *)expect.X19,
           (void *)expect.X20, (void *)expect.X21, (void *)expect.X22, (void *)expect.X23,
           (void *)expect.X24, (void *)expect.X25, (void *)expect.X26, (void *)expect.X27,
           (void *)expect.X28, (void *)expect.Fp, (void *)expect.Lr, (void *)expect.Sp,
           (void *)expect.Pc, expect.Cpsr );
    trace( "actual: x0=%p x1=%p x2=%p x3=%p x4=%p x5=%p x6=%p x7=%p x8=%p x9=%p x10=%p x11=%p x12=%p x13=%p x14=%p x15=%p x16=%p x17=%p x18=%p x19=%p x20=%p x21=%p x22=%p x23=%p x24=%p x25=%p x26=%p x27=%p x28=%p fp=%p lr=%p sp=%p pc=%p cpsr=%08lx\n",
           (void *)context.X0, (void *)context.X1, (void *)context.X2, (void *)context.X3,
           (void *)context.X4, (void *)context.X5, (void *)context.X6, (void *)context.X7,
           (void *)context.X8, (void *)context.X9, (void *)context.X10, (void *)context.X11,
           (void *)context.X12, (void *)context.X13, (void *)context.X14, (void *)context.X15,
           (void *)context.X16, (void *)context.X17, (void *)context.X18, (void *)context.X19,
           (void *)context.X20, (void *)context.X21, (void *)context.X22, (void *)context.X23,
           (void *)context.X24, (void *)context.X25, (void *)context.X26, (void *)context.X27,
           (void *)context.X28, (void *)context.Fp, (void *)context.Lr, (void *)context.Sp,
           (void *)context.Pc, context.Cpsr );

    ok( context.ContextFlags == CONTEXT_FULL,
        "wrong flags %08lx\n", context.ContextFlags );
    ok( !context.X0, "wrong X0 %p\n", (void *)context.X0 );
    COMPARE( X1 );
    COMPARE( X2 );
    COMPARE( X3 );
    COMPARE( X4 );
    COMPARE( X5 );
    COMPARE( X6 );
    COMPARE( X7 );
    COMPARE( X8 );
    COMPARE( X9 );
    COMPARE( X10 );
    COMPARE( X11 );
    COMPARE( X12 );
    COMPARE( X13 );
    COMPARE( X14 );
    COMPARE( X15 );
    COMPARE( X16 );
    COMPARE( X17 );
    COMPARE( X18 );
    COMPARE( X19 );
    COMPARE( X20 );
    COMPARE( X21 );
    COMPARE( X22 );
    COMPARE( X23 );
    COMPARE( X24 );
    COMPARE( X25 );
    COMPARE( X26 );
    COMPARE( X27 );
    COMPARE( X28 );
    COMPARE( Fp );
    COMPARE( Sp );
    COMPARE( Pc );
    COMPARE( Cpsr );
    COMPARE( Fpcr );
    COMPARE( Fpsr );
    ok( !context.Lr, "wrong Lr %p\n", (void *)context.Lr );

    memset( &context, 0xcc, sizeof(context) );
    memset( &expect, 0xcc, sizeof(expect) );
    context.ContextFlags = CONTEXT_FULL;

    status = func_ptr( GetCurrentThread(), &context, &expect, pNtGetContextThread );
    ok( status == STATUS_SUCCESS, "NtGetContextThread failed %08lx\n", status );
    trace( "expect: x0=%p x1=%p x2=%p x3=%p x4=%p x5=%p x6=%p x7=%p x8=%p x9=%p x10=%p x11=%p x12=%p x13=%p x14=%p x15=%p x16=%p x17=%p x18=%p x19=%p x20=%p x21=%p x22=%p x23=%p x24=%p x25=%p x26=%p x27=%p x28=%p fp=%p lr=%p sp=%p pc=%p cpsr=%08lx\n",
           (void *)expect.X0, (void *)expect.X1, (void *)expect.X2, (void *)expect.X3,
           (void *)expect.X4, (void *)expect.X5, (void *)expect.X6, (void *)expect.X7,
           (void *)expect.X8, (void *)expect.X9, (void *)expect.X10, (void *)expect.X11,
           (void *)expect.X12, (void *)expect.X13, (void *)expect.X14, (void *)expect.X15,
           (void *)expect.X16, (void *)expect.X17, (void *)expect.X18, (void *)expect.X19,
           (void *)expect.X20, (void *)expect.X21, (void *)expect.X22, (void *)expect.X23,
           (void *)expect.X24, (void *)expect.X25, (void *)expect.X26, (void *)expect.X27,
           (void *)expect.X28, (void *)expect.Fp, (void *)expect.Lr, (void *)expect.Sp,
           (void *)expect.Pc, expect.Cpsr );
    trace( "actual: x0=%p x1=%p x2=%p x3=%p x4=%p x5=%p x6=%p x7=%p x8=%p x9=%p x10=%p x11=%p x12=%p x13=%p x14=%p x15=%p x16=%p x17=%p x18=%p x19=%p x20=%p x21=%p x22=%p x23=%p x24=%p x25=%p x26=%p x27=%p x28=%p fp=%p lr=%p sp=%p pc=%p cpsr=%08lx\n",
           (void *)context.X0, (void *)context.X1, (void *)context.X2, (void *)context.X3,
           (void *)context.X4, (void *)context.X5, (void *)context.X6, (void *)context.X7,
           (void *)context.X8, (void *)context.X9, (void *)context.X10, (void *)context.X11,
           (void *)context.X12, (void *)context.X13, (void *)context.X14, (void *)context.X15,
           (void *)context.X16, (void *)context.X17, (void *)context.X18, (void *)context.X19,
           (void *)context.X20, (void *)context.X21, (void *)context.X22, (void *)context.X23,
           (void *)context.X24, (void *)context.X25, (void *)context.X26, (void *)context.X27,
           (void *)context.X28, (void *)context.Fp, (void *)context.Lr, (void *)context.Sp,
           (void *)context.Pc, context.Cpsr );
    /* other registers are not preserved */
    COMPARE( X19 );
    COMPARE( X20 );
    COMPARE( X21 );
    COMPARE( X22 );
    COMPARE( X23 );
    COMPARE( X24 );
    COMPARE( X25 );
    COMPARE( X26 );
    COMPARE( X27 );
    COMPARE( X28 );
    COMPARE( Fp );
    COMPARE( Fpcr );
    COMPARE( Fpsr );
    ok( context.Lr == expect.Pc, "wrong Lr %p/%p\n", (void *)context.Lr, (void *)expect.Pc );
    ok( context.Sp == expect.Sp, "wrong Sp %p/%p\n", (void *)context.Sp, (void *)expect.Sp );
    ok( (char *)context.Pc >= (char *)pNtGetContextThread &&
        (char *)context.Pc <= (char *)pNtGetContextThread + 32,
        "wrong Pc %p/%p\n", (void *)context.Pc, pNtGetContextThread );
#undef COMPARE
}

#ifndef __REACTOS__
static void test_debugger(DWORD cont_status, BOOL with_WaitForDebugEventEx)
{
    char cmdline[MAX_PATH];
    PROCESS_INFORMATION pi;
    STARTUPINFOA si = { 0 };
    DEBUG_EVENT de;
    DWORD continuestatus;
    PVOID code_mem_address = NULL;
    NTSTATUS status;
    SIZE_T size_read;
    BOOL ret;
    int counter = 0;
    si.cb = sizeof(si);

    if(!pNtGetContextThread || !pNtSetContextThread || !pNtReadVirtualMemory || !pNtTerminateProcess)
    {
        skip("NtGetContextThread, NtSetContextThread, NtReadVirtualMemory or NtTerminateProcess not found\n");
        return;
    }

    if (with_WaitForDebugEventEx && !pWaitForDebugEventEx)
    {
        skip("WaitForDebugEventEx not found, skipping unicode strings in OutputDebugStringW\n");
        return;
    }

    sprintf(cmdline, "%s %s %s %p", my_argv[0], my_argv[1], "debuggee", &test_stage);
    ret = CreateProcessA(NULL, cmdline, NULL, NULL, FALSE, DEBUG_PROCESS, NULL, NULL, &si, &pi);
    ok(ret, "could not create child process error: %lu\n", GetLastError());
    if (!ret)
        return;

    do
    {
        continuestatus = cont_status;
        ret = with_WaitForDebugEventEx ? pWaitForDebugEventEx(&de, INFINITE) : WaitForDebugEvent(&de, INFINITE);
        ok(ret, "reading debug event\n");

        ret = ContinueDebugEvent(de.dwProcessId, de.dwThreadId, 0xdeadbeef);
        ok(!ret, "ContinueDebugEvent unexpectedly succeeded\n");
        ok(GetLastError() == ERROR_INVALID_PARAMETER, "Unexpected last error: %lu\n", GetLastError());

        if (de.dwThreadId != pi.dwThreadId)
        {
            trace("event %ld not coming from main thread, ignoring\n", de.dwDebugEventCode);
            ContinueDebugEvent(de.dwProcessId, de.dwThreadId, cont_status);
            continue;
        }

        if (de.dwDebugEventCode == CREATE_PROCESS_DEBUG_EVENT)
        {
            if(de.u.CreateProcessInfo.lpBaseOfImage != NtCurrentTeb()->Peb->ImageBaseAddress)
            {
                skip("child process loaded at different address, terminating it\n");
                pNtTerminateProcess(pi.hProcess, 0);
            }
        }
        else if (de.dwDebugEventCode == EXCEPTION_DEBUG_EVENT)
        {
            CONTEXT ctx;
            enum debugger_stages stage;

            counter++;
            status = pNtReadVirtualMemory(pi.hProcess, &code_mem, &code_mem_address,
                                          sizeof(code_mem_address), &size_read);
            ok(!status,"NtReadVirtualMemory failed with 0x%lx\n", status);
            status = pNtReadVirtualMemory(pi.hProcess, &test_stage, &stage,
                                          sizeof(stage), &size_read);
            ok(!status,"NtReadVirtualMemory failed with 0x%lx\n", status);

            ctx.ContextFlags = CONTEXT_FULL;
            status = pNtGetContextThread(pi.hThread, &ctx);
            ok(!status, "NtGetContextThread failed with 0x%lx\n", status);

            trace("exception 0x%lx at %p firstchance=%ld pc=%p, x0=%p\n",
                  de.u.Exception.ExceptionRecord.ExceptionCode,
                  de.u.Exception.ExceptionRecord.ExceptionAddress,
                  de.u.Exception.dwFirstChance, (char *)ctx.Pc, (char *)ctx.X0);

            if (counter > 100)
            {
                ok(FALSE, "got way too many exceptions, probably caught in an infinite loop, terminating child\n");
                pNtTerminateProcess(pi.hProcess, 1);
            }
            else if (counter < 2) /* startup breakpoint */
            {
                /* breakpoint is inside ntdll */
                void *ntdll = GetModuleHandleA( "ntdll.dll" );
                IMAGE_NT_HEADERS *nt = RtlImageNtHeader( ntdll );

                ok( (char *)ctx.Pc >= (char *)ntdll &&
                    (char *)ctx.Pc < (char *)ntdll + nt->OptionalHeader.SizeOfImage,
                    "wrong pc %p ntdll %p-%p\n", (void *)ctx.Pc, ntdll,
                    (char *)ntdll + nt->OptionalHeader.SizeOfImage );
            }
            else
            {
                if (stage == STAGE_RTLRAISE_NOT_HANDLED)
                {
                    ok((char *)ctx.Pc == (char *)code_mem_address + 0xc, "Pc at %p instead of %p\n",
                       (char *)ctx.Pc, (char *)code_mem_address + 0xc);
                    /* setting the context from debugger does not affect the context that the
                     * exception handler gets, except on w2008 */
                    ctx.Pc = (UINT_PTR)code_mem_address + 0x10;
                    ctx.X0 = 0xf00f00f1;
                    /* let the debuggee handle the exception */
                    continuestatus = DBG_EXCEPTION_NOT_HANDLED;
                }
                else if (stage == STAGE_RTLRAISE_HANDLE_LAST_CHANCE)
                {
                    if (de.u.Exception.dwFirstChance)
                    {
                        /* debugger gets first chance exception with unmodified ctx.Pc */
                        ok((char *)ctx.Pc == (char *)code_mem_address + 0xc, "Pc at %p instead of %p\n",
                           (char *)ctx.Pc, (char *)code_mem_address + 0xc);
                        ctx.Pc = (UINT_PTR)code_mem_address + 0x10;
                        ctx.X0 = 0xf00f00f1;
                        /* pass exception to debuggee
                         * exception will not be handled and a second chance exception will be raised */
                        continuestatus = DBG_EXCEPTION_NOT_HANDLED;
                    }
                    else
                    {
                        /* debugger gets context after exception handler has played with it */
                        /* ctx.Pc is the same value the exception handler got */
                        if (de.u.Exception.ExceptionRecord.ExceptionCode == EXCEPTION_BREAKPOINT)
                        {
                            ok((char *)ctx.Pc == (char *)code_mem_address + 0xc,
                               "Pc at %p instead of %p\n", (char *)ctx.Pc, (char *)code_mem_address + 0xc);
                            /* need to fixup Pc for debuggee */
                            ctx.Pc += 4;
                        }
                        else ok((char *)ctx.Pc == (char *)code_mem_address + 0xc,
                                "Pc at %p instead of %p\n", (void *)ctx.Pc, (char *)code_mem_address + 0xc);
                        /* here we handle exception */
                    }
                }
                else if (stage == STAGE_SERVICE_CONTINUE || stage == STAGE_SERVICE_NOT_HANDLED)
                {
                    ok(de.u.Exception.ExceptionRecord.ExceptionCode == EXCEPTION_BREAKPOINT,
                       "expected EXCEPTION_BREAKPOINT, got %08lx\n", de.u.Exception.ExceptionRecord.ExceptionCode);
                    ok((char *)ctx.Pc == (char *)code_mem_address + 0x1d,
                       "expected Pc = %p, got %p\n", (char *)code_mem_address + 0x1d, (char *)ctx.Pc);
                    if (stage == STAGE_SERVICE_NOT_HANDLED) continuestatus = DBG_EXCEPTION_NOT_HANDLED;
                }
                else if (stage == STAGE_BREAKPOINT_CONTINUE || stage == STAGE_BREAKPOINT_NOT_HANDLED)
                {
                    ok(de.u.Exception.ExceptionRecord.ExceptionCode == EXCEPTION_BREAKPOINT,
                       "expected EXCEPTION_BREAKPOINT, got %08lx\n", de.u.Exception.ExceptionRecord.ExceptionCode);
                    ok((char *)ctx.Pc == (char *)code_mem_address + 4,
                       "expected Pc = %p, got %p\n", (char *)code_mem_address + 4, (char *)ctx.Pc);
                    if (stage == STAGE_BREAKPOINT_NOT_HANDLED) continuestatus = DBG_EXCEPTION_NOT_HANDLED;
                }
                else if (stage == STAGE_EXCEPTION_INVHANDLE_CONTINUE || stage == STAGE_EXCEPTION_INVHANDLE_NOT_HANDLED)
                {
                    ok(de.u.Exception.ExceptionRecord.ExceptionCode == EXCEPTION_INVALID_HANDLE,
                       "unexpected exception code %08lx, expected %08lx\n", de.u.Exception.ExceptionRecord.ExceptionCode,
                       EXCEPTION_INVALID_HANDLE);
                    ok(de.u.Exception.ExceptionRecord.NumberParameters == 0,
                       "unexpected number of parameters %ld, expected 0\n", de.u.Exception.ExceptionRecord.NumberParameters);

                    if (stage == STAGE_EXCEPTION_INVHANDLE_NOT_HANDLED) continuestatus = DBG_EXCEPTION_NOT_HANDLED;
                }
                else if (stage == STAGE_NO_EXCEPTION_INVHANDLE_NOT_HANDLED)
                {
                    ok(FALSE || broken(TRUE) /* < Win10 */, "should not throw exception\n");
                    continuestatus = DBG_EXCEPTION_NOT_HANDLED;
                }
                else
                    ok(FALSE, "unexpected stage %x\n", stage);

                status = pNtSetContextThread(pi.hThread, &ctx);
                ok(!status, "NtSetContextThread failed with 0x%lx\n", status);
            }
        }
        else if (de.dwDebugEventCode == OUTPUT_DEBUG_STRING_EVENT)
        {
            enum debugger_stages stage;
            char buffer[128 * sizeof(WCHAR)];
            unsigned char_size = de.u.DebugString.fUnicode ? sizeof(WCHAR) : sizeof(char);

            status = pNtReadVirtualMemory(pi.hProcess, &test_stage, &stage,
                                          sizeof(stage), &size_read);
            ok(!status,"NtReadVirtualMemory failed with 0x%lx\n", status);

            if (de.u.DebugString.fUnicode)
                ok(with_WaitForDebugEventEx &&
                   (stage == STAGE_OUTPUTDEBUGSTRINGW_CONTINUE || stage == STAGE_OUTPUTDEBUGSTRINGW_NOT_HANDLED),
                   "unexpected unicode debug string event\n");
            else
                ok(!with_WaitForDebugEventEx || stage != STAGE_OUTPUTDEBUGSTRINGW_CONTINUE || cont_status != DBG_CONTINUE,
                   "unexpected ansi debug string event %u %s %lx\n",
                   stage, with_WaitForDebugEventEx ? "with" : "without", cont_status);

            ok(de.u.DebugString.nDebugStringLength < sizeof(buffer) / char_size - 1,
               "buffer not large enough to hold %d bytes\n", de.u.DebugString.nDebugStringLength);

            memset(buffer, 0, sizeof(buffer));
            status = pNtReadVirtualMemory(pi.hProcess, de.u.DebugString.lpDebugStringData, buffer,
                                          de.u.DebugString.nDebugStringLength * char_size, &size_read);
            ok(!status,"NtReadVirtualMemory failed with 0x%lx\n", status);

            if (stage == STAGE_OUTPUTDEBUGSTRINGA_CONTINUE || stage == STAGE_OUTPUTDEBUGSTRINGA_NOT_HANDLED ||
                stage == STAGE_OUTPUTDEBUGSTRINGW_CONTINUE || stage == STAGE_OUTPUTDEBUGSTRINGW_NOT_HANDLED)
            {
                if (de.u.DebugString.fUnicode)
                    ok(!wcscmp((WCHAR*)buffer, L"Hello World"), "got unexpected debug string '%ls'\n", (WCHAR*)buffer);
                else
                    ok(!strcmp(buffer, "Hello World"), "got unexpected debug string '%s'\n", buffer);
            }
            else /* ignore unrelated debug strings like 'SHIMVIEW: ShimInfo(Complete)' */
                ok(strstr(buffer, "SHIMVIEW") || !strncmp(buffer, "RTL:", 4),
                     "unexpected stage %x, got debug string event '%s'\n", stage, buffer);

            if (stage == STAGE_OUTPUTDEBUGSTRINGA_NOT_HANDLED || stage == STAGE_OUTPUTDEBUGSTRINGW_NOT_HANDLED)
                continuestatus = DBG_EXCEPTION_NOT_HANDLED;
        }
        else if (de.dwDebugEventCode == RIP_EVENT)
        {
            enum debugger_stages stage;

            status = pNtReadVirtualMemory(pi.hProcess, &test_stage, &stage,
                                          sizeof(stage), &size_read);
            ok(!status,"NtReadVirtualMemory failed with 0x%lx\n", status);

            if (stage == STAGE_RIPEVENT_CONTINUE || stage == STAGE_RIPEVENT_NOT_HANDLED)
            {
                ok(de.u.RipInfo.dwError == 0x11223344, "got unexpected rip error code %08lx, expected %08x\n",
                   de.u.RipInfo.dwError, 0x11223344);
                ok(de.u.RipInfo.dwType  == 0x55667788, "got unexpected rip type %08lx, expected %08x\n",
                   de.u.RipInfo.dwType, 0x55667788);
            }
            else
                ok(FALSE, "unexpected stage %x\n", stage);

            if (stage == STAGE_RIPEVENT_NOT_HANDLED) continuestatus = DBG_EXCEPTION_NOT_HANDLED;
        }

        ContinueDebugEvent(de.dwProcessId, de.dwThreadId, continuestatus);

    } while (de.dwDebugEventCode != EXIT_PROCESS_DEBUG_EVENT);

    wait_child_process( &pi );
}
#endif /* !__REACTOS__ */

static void test_debug_service(DWORD numexc)
{
    /* not supported */
}

#ifndef __REACTOS__
static void test_continue(void)
{
    struct context_pair {
        CONTEXT before;
        CONTEXT after;
    } contexts;
    KCONTINUE_ARGUMENT args = { .ContinueType = KCONTINUE_UNWIND };
    unsigned int i;
    NTSTATUS (*func_ptr)( struct context_pair *, void *arg, void *continue_func, void *capture_func ) = code_mem;

    static const DWORD call_func[] =
    {
        0xa9bd7bfd, /* stp x29, x30, [sp, #-0x30]! */
                    /* stash volatile registers before calling capture */
        0xa90107e0, /* stp x0, x1, [sp, #0x10] */
        0xa9020fe2, /* stp x2, x3, [sp, #0x20] */
        0xd63f0060, /* blr x3 * capture context from before NtContinue to contexts->before */
        0xa9420fe2, /* ldp x2, x3, [sp, #0x20] */
        0xa94107e0, /* ldp x0, x1, [sp, #0x10] */
                    /* overwrite the contents of x4...k28 with a dummy value */
        0xd297dde4, /* mov x4, #0xbeef */
        0xf2bbd5a4, /* movk x4, #0xdead, lsl #16 */
        0xaa048084, /* orr x4, x4, x4, lsl #32 */
        0xaa0403e5, /* mov x5,  x4 */
        0xaa0403e6, /* mov x6,  x4 */
        0xaa0403e7, /* mov x7,  x4 */
        0xaa0403e8, /* mov x8,  x4 */
        0xaa0403e9, /* mov x9,  x4 */
        0xaa0403ea, /* mov x10, x4 */
        0xaa0403eb, /* mov x11, x4 */
        0xaa0403ec, /* mov x12, x4 */
        0xaa0403ed, /* mov x13, x4 */
        0xaa0403ee, /* mov x14, x4 */
        0xaa0403ef, /* mov x15, x4 */
        0xaa0403f0, /* mov x16, x4 */
        0xaa0403f1, /* mov x17, x4 */
                    /* avoid overwriting the TEB in x18 */
        0xaa0403f3, /* mov x19, x4 */
        0xaa0403f4, /* mov x20, x4 */
        0xaa0403f5, /* mov x21, x4 */
        0xaa0403f6, /* mov x22, x4 */
        0xaa0403f7, /* mov x23, x4 */
        0xaa0403f8, /* mov x24, x4 */
        0xaa0403f9, /* mov x25, x4 */
        0xaa0403fa, /* mov x26, x4 */
        0xaa0403fb, /* mov x27, x4 */
        0xaa0403fc, /* mov x28, x4 */
                    /* overwrite the contents all vector registers a dummy value */
        0x4e080c80, /* dup v0.2d,  x4 */
        0x4ea01c01, /* mov v1.2d,  v0.2d */
        0x4ea01c02, /* mov v2.2d,  v0.2d */
        0x4ea01c03, /* mov v3.2d,  v0.2d */
        0x4ea01c04, /* mov v4.2d,  v0.2d */
        0x4ea01c05, /* mov v5.2d,  v0.2d */
        0x4ea01c06, /* mov v6.2d,  v0.2d */
        0x4ea01c07, /* mov v7.2d,  v0.2d */
        0x4ea01c08, /* mov v8.2d,  v0.2d */
        0x4ea01c09, /* mov v9.2d,  v0.2d */
        0x4ea01c0a, /* mov v10.2d, v0.2d */
        0x4ea01c0b, /* mov v11.2d, v0.2d */
        0x4ea01c0c, /* mov v12.2d, v0.2d */
        0x4ea01c0d, /* mov v13.2d, v0.2d */
        0x4ea01c0e, /* mov v14.2d, v0.2d */
        0x4ea01c0f, /* mov v15.2d, v0.2d */
        0x4ea01c10, /* mov v16.2d, v0.2d */
        0x4ea01c11, /* mov v17.2d, v0.2d */
        0x4ea01c12, /* mov v18.2d, v0.2d */
        0x4ea01c13, /* mov v19.2d, v0.2d */
        0x4ea01c14, /* mov v20.2d, v0.2d */
        0x4ea01c15, /* mov v21.2d, v0.2d */
        0x4ea01c16, /* mov v22.2d, v0.2d */
        0x4ea01c17, /* mov v23.2d, v0.2d */
        0x4ea01c18, /* mov v24.2d, v0.2d */
        0x4ea01c19, /* mov v25.2d, v0.2d */
        0x4ea01c1a, /* mov v26.2d, v0.2d */
        0x4ea01c1b, /* mov v27.2d, v0.2d */
        0x4ea01c1c, /* mov v28.2d, v0.2d */
        0x4ea01c1d, /* mov v29.2d, v0.2d */
        0x4ea01c1e, /* mov v30.2d, v0.2d */
        0x4ea01c1f, /* mov v31.2d, v0.2d */
        0xd51b441f, /* msr fpcr, xzr */
        0xd51b443f, /* msr fpsr, xzr */
                    /* setup the control context so execution continues from label 1 */
        0x10000064, /* adr x4, #0xc */
        0xf9008404, /* str x4, [x0, #0x108] */
        0xd63f0040, /* blr x2 * restore the captured integer and floating point context */
        0xf94017e3, /* 1: ldr x3, [sp, #0x28] */
        0xf9400be0, /* ldr x0, [sp, #0x10] */
        0x910e4000, /* add x0, x0, #0x390 * adjust contexts to point to contexts->after */
        0xd63f0060, /* blr x3 * capture context from after NtContinue to contexts->after */
        0xa8c37bfd, /* ldp x29, x30, [sp], #0x30 */
        0xd65f03c0, /* ret */
     };

    if (!pRtlCaptureContext)
    {
        win_skip("RtlCaptureContext is not available.\n");
        return;
    }

    memcpy( func_ptr, call_func, sizeof(call_func) );
    FlushInstructionCache( GetCurrentProcess(), func_ptr, sizeof(call_func) );

#define COMPARE(reg) \
    ok( contexts.before.reg == contexts.after.reg, "wrong " #reg " %p/%p\n", (void *)(ULONG64)contexts.before.reg, (void *)(ULONG64)contexts.after.reg )
#define COMPARE_INDEXED(reg) \
    ok( contexts.before.reg == contexts.after.reg, "wrong " #reg " i: %u, %p/%p\n", i, (void *)(ULONG64)contexts.before.reg, (void *)(ULONG64)contexts.after.reg )

    func_ptr( &contexts, 0, NtContinue, pRtlCaptureContext );

    for (i = 1; i < 29; i++) COMPARE_INDEXED( X[i] );

    COMPARE( Fpcr );
    COMPARE( Fpsr );

    for (i = 0; i < 32; i++)
    {
        COMPARE_INDEXED( V[i].Low );
        COMPARE_INDEXED( V[i].High );
    }

    apc_count = 0;
    pNtQueueApcThread( GetCurrentThread(), apc_func, 0x1234, 0x5678, 0xdeadbeef );
    func_ptr( &contexts, 0, NtContinue, pRtlCaptureContext );
    ok( apc_count == 0, "apc called\n" );
    func_ptr( &contexts, (void *)1, NtContinue, pRtlCaptureContext );
    ok( apc_count == 1, "apc not called\n" );

    if (!pNtContinueEx)
    {
        win_skip( "NtContinueEx not supported\n" );
        return;
    }

    func_ptr( &contexts, &args, pNtContinueEx, pRtlCaptureContext );

    for (i = 1; i < 29; i++) COMPARE_INDEXED( X[i] );

    COMPARE( Fpcr );
    COMPARE( Fpsr );

    for (i = 0; i < 32; i++)
    {
        COMPARE_INDEXED( V[i].Low );
        COMPARE_INDEXED( V[i].High );
    }

    pNtQueueApcThread( GetCurrentThread(), apc_func, 0x1234 + apc_count, 0x5678, 0xdeadbeef );
    func_ptr( &contexts, &args, pNtContinueEx, pRtlCaptureContext );
    ok( apc_count == 1, "apc called\n" );
    args.ContinueFlags = KCONTINUE_FLAG_TEST_ALERT;
    func_ptr( &contexts, &args, pNtContinueEx, pRtlCaptureContext );
    ok( apc_count == 2, "apc not called\n" );

#undef COMPARE
}
#endif /* !__REACTOS__ */

#ifndef __REACTOS__
static BOOL hook_called;
static BOOL got_exception;

static ULONG patched_code[] =
{
    0x910003e0, /* mov x0, sp */
    0x5800004f, /* ldr x15, 1f */
    0xd61f01e0, /* br x15 */
    0, 0,       /* 1: hook_trampoline */
};
static ULONG saved_code[ARRAY_SIZE(patched_code)];

static LONG WINAPI dbg_except_continue_vectored_handler(struct _EXCEPTION_POINTERS *ptrs)
{
    EXCEPTION_RECORD *rec = ptrs->ExceptionRecord;
    CONTEXT *context = ptrs->ContextRecord;

    trace("dbg_except_continue_vectored_handler, code %#lx, pc %#Ix.\n", rec->ExceptionCode, context->Pc);
    got_exception = TRUE;

    ok(rec->ExceptionCode == 0x80000003, "Got unexpected exception code %#lx.\n", rec->ExceptionCode);
    return EXCEPTION_CONTINUE_EXECUTION;
}

static void * WINAPI hook_KiUserExceptionDispatcher(void *stack)
{
    struct
    {
        CONTEXT              context;    /* 000 */
        CONTEXT_EX           context_ex; /* 390 */
        EXCEPTION_RECORD     rec;        /* 3b0 */
        ULONG64              align;      /* 448 */
    } *args = stack;
    EXCEPTION_RECORD *old_rec = (EXCEPTION_RECORD *)&args->context_ex;

    trace("stack %p context->Pc %#Ix, context->Sp %#Ix, ContextFlags %#lx.\n",
          stack, args->context.Pc, args->context.Sp, args->context.ContextFlags);

    hook_called = TRUE;
    ok( !((ULONG_PTR)stack & 15), "unaligned stack %p\n", stack );

    if (!broken( old_rec->ExceptionCode == 0x80000003 )) /* Windows 11 versions prior to 27686 */
    {
        ok( args->rec.ExceptionCode == 0x80000003, "Got unexpected ExceptionCode %#lx.\n", args->rec.ExceptionCode );

        ok( args->context_ex.All.Offset == -sizeof(CONTEXT), "wrong All.Offset %lx\n", args->context_ex.All.Offset );
        ok( args->context_ex.All.Length >= sizeof(CONTEXT) + offsetof(CONTEXT_EX, align), "wrong All.Length %lx\n", args->context_ex.All.Length );
        ok( args->context_ex.Legacy.Offset == -sizeof(CONTEXT), "wrong Legacy.Offset %lx\n", args->context_ex.All.Offset );
        ok( args->context_ex.Legacy.Length == sizeof(CONTEXT), "wrong Legacy.Length %lx\n", args->context_ex.All.Length );
    }

    memcpy(pKiUserExceptionDispatcher, saved_code, sizeof(saved_code));
    FlushInstructionCache( GetCurrentProcess(), pKiUserExceptionDispatcher, sizeof(saved_code));
    return pKiUserExceptionDispatcher;
}

static void test_KiUserExceptionDispatcher(void)
{
    ULONG hook_trampoline[] =
    {
        0x910003e0, /* mov x0, sp */
        0x5800006f, /* ldr x15, 1f */
        0xd63f01e0, /* blr x15 */
        0xd61f0000, /* br x0 */
        0, 0,       /* 1: hook_KiUserExceptionDispatcher */
    };

    EXCEPTION_RECORD record = { EXCEPTION_BREAKPOINT };
    void *trampoline_ptr, *vectored_handler;
    DWORD old_protect;
    BOOL ret;

    *(void **)&hook_trampoline[4] = hook_KiUserExceptionDispatcher;
    trampoline_ptr = (char *)code_mem + 1024;
    memcpy( trampoline_ptr, hook_trampoline, sizeof(hook_trampoline));

    ret = VirtualProtect( pKiUserExceptionDispatcher, sizeof(saved_code),
                          PAGE_EXECUTE_READWRITE, &old_protect );
    ok( ret, "Got unexpected ret %#x, GetLastError() %lu.\n", ret, GetLastError() );

    memcpy( saved_code, pKiUserExceptionDispatcher, sizeof(saved_code) );
    *(void **)&patched_code[3] = trampoline_ptr;

    vectored_handler = AddVectoredExceptionHandler(TRUE, dbg_except_continue_vectored_handler);

    memcpy( pKiUserExceptionDispatcher, patched_code, sizeof(patched_code) );
    FlushInstructionCache( GetCurrentProcess(), pKiUserExceptionDispatcher, sizeof(patched_code));

    got_exception = FALSE;
    hook_called = FALSE;

    pRtlRaiseException(&record);

    ok(got_exception, "Handler was not called.\n");
    ok(!hook_called, "Hook was called.\n");

    memcpy( pKiUserExceptionDispatcher, patched_code, sizeof(patched_code) );
    FlushInstructionCache( GetCurrentProcess(), pKiUserExceptionDispatcher, sizeof(patched_code));

    got_exception = 0;
    hook_called = FALSE;
    NtCurrentTeb()->Peb->BeingDebugged = 1;

    pRtlRaiseException(&record);

    ok(got_exception, "Handler was not called.\n");
    ok(hook_called, "Hook was not called.\n");
    NtCurrentTeb()->Peb->BeingDebugged = 0;

    RemoveVectoredExceptionHandler(vectored_handler);
    VirtualProtect(pKiUserExceptionDispatcher, sizeof(saved_code), old_protect, &old_protect);
}
#endif /* !__REACTOS__ */

#ifndef __REACTOS__
static void * WINAPI hook_KiUserApcDispatcher(void *stack)
{
    struct
    {
        void *func;
        ULONG64 args[3];
        ULONG64 alertable;
        ULONG64 align;
        CONTEXT context;
    } *args = stack;

    trace( "stack=%p func=%p args=%Ix,%Ix,%Ix alertable=%Ix context=%p pc=%Ix sp=%Ix (%Ix)\n",
           args, args->func, args->args[0], args->args[1], args->args[2],
           args->alertable, &args->context, args->context.Pc, args->context.Sp,
           args->context.Sp - (ULONG_PTR)stack );

    ok( args->func == apc_func, "wrong func %p / %p\n", args->func, apc_func );
    ok( args->args[0] == 0x1234 + apc_count, "wrong arg1 %Ix\n", args->args[0] );
    ok( args->args[1] == 0x5678, "wrong arg2 %Ix\n", args->args[1] );
    ok( args->args[2] == 0xdeadbeef, "wrong arg3 %Ix\n", args->args[2] );

    if (apc_count) args->alertable = FALSE;
    pNtQueueApcThread( GetCurrentThread(), apc_func, 0x1234 + apc_count + 1, 0x5678, 0xdeadbeef );

    hook_called = TRUE;
    memcpy( pKiUserApcDispatcher, saved_code, sizeof(saved_code));
    FlushInstructionCache( GetCurrentProcess(), pKiUserApcDispatcher, sizeof(saved_code));
    return pKiUserApcDispatcher;
}

static void test_KiUserApcDispatcher(void)
{
    ULONG hook_trampoline[] =
    {
        0x910003e0, /* mov x0, sp */
        0x5800006f, /* ldr x15, 1f */
        0xd63f01e0, /* blr x15 */
        0xd61f0000, /* br x0 */
        0, 0,       /* 1: hook_KiUserApcDispatcher */
    };
    DWORD old_protect;
    BOOL ret;

    *(void **)&hook_trampoline[4] = hook_KiUserApcDispatcher;
    memcpy(code_mem, hook_trampoline, sizeof(hook_trampoline));

    ret = VirtualProtect( pKiUserApcDispatcher, sizeof(saved_code),
                          PAGE_EXECUTE_READWRITE, &old_protect );
    ok( ret, "Got unexpected ret %#x, GetLastError() %lu.\n", ret, GetLastError() );

    memcpy( saved_code, pKiUserApcDispatcher, sizeof(saved_code) );
    *(void **)&patched_code[3] = code_mem;
    memcpy( pKiUserApcDispatcher, patched_code, sizeof(patched_code) );
    FlushInstructionCache( GetCurrentProcess(), pKiUserApcDispatcher, sizeof(patched_code));

    hook_called = FALSE;
    apc_count = 0;
    pNtQueueApcThread( GetCurrentThread(), apc_func, 0x1234, 0x5678, 0xdeadbeef );
    SleepEx( 0, TRUE );
    ok( apc_count == 2, "APC count %u\n", apc_count );
    ok( hook_called, "hook was not called\n" );

    memcpy( pKiUserApcDispatcher, patched_code, sizeof(patched_code) );
    FlushInstructionCache( GetCurrentProcess(), pKiUserApcDispatcher, sizeof(patched_code));
    pNtQueueApcThread( GetCurrentThread(), apc_func, 0x1234 + apc_count, 0x5678, 0xdeadbeef );
    SleepEx( 0, TRUE );
    ok( apc_count == 3, "APC count %u\n", apc_count );
    SleepEx( 0, TRUE );
    ok( apc_count == 4, "APC count %u\n", apc_count );

    VirtualProtect( pKiUserApcDispatcher, sizeof(saved_code), old_protect, &old_protect );
}
#endif /* !__REACTOS__ */

#ifndef __REACTOS__
static void WINAPI hook_KiUserCallbackDispatcher(void *sp)
{
    struct
    {
        void *args;
        ULONG len;
        ULONG id;
        ULONG64 unknown;
        ULONG64 lr;
        ULONG64 sp;
        ULONG64 pc;
        BYTE args_data[0];
    } *stack = sp;
    ULONG_PTR redzone = (BYTE *)stack->sp - &stack->args_data[stack->len];
    KERNEL_CALLBACK_PROC func = NtCurrentTeb()->Peb->KernelCallbackTable[stack->id];

    trace( "stack=%p len=%lx id=%lx unk=%Ix lr=%Ix sp=%Ix pc=%Ix\n",
           stack, stack->len, stack->id, stack->unknown, stack->lr, stack->sp, stack->pc );

    ok( stack->args == stack->args_data, "wrong args %p / %p\n", stack->args, stack->args_data );
    ok( redzone >= 16 && redzone <= 32, "wrong sp %p / %p (%Iu)\n",
        (void *)stack->sp, stack->args_data, redzone );

    if (pRtlPcToFileHeader)
    {
        void *mod, *win32u = GetModuleHandleA("win32u.dll");

        pRtlPcToFileHeader( (void *)stack->pc, &mod );
        ok( mod == win32u, "pc %Ix not in win32u %p\n", stack->pc, win32u );
    }
    NtCallbackReturn( NULL, 0, func( stack->args, stack->len ));
}

static void test_KiUserCallbackDispatcher(void)
{
    DWORD old_protect;
    BOOL ret;

    ret = VirtualProtect( pKiUserCallbackDispatcher, sizeof(saved_code),
                          PAGE_EXECUTE_READWRITE, &old_protect );
    ok( ret, "Got unexpected ret %#x, GetLastError() %lu.\n", ret, GetLastError() );

    memcpy( saved_code, pKiUserCallbackDispatcher, sizeof(saved_code));
    *(void **)&patched_code[3] = hook_KiUserCallbackDispatcher;
    memcpy( pKiUserCallbackDispatcher, patched_code, sizeof(patched_code));
    FlushInstructionCache(GetCurrentProcess(), pKiUserCallbackDispatcher, sizeof(patched_code));

    DestroyWindow( CreateWindowA( "Static", "test", 0, 0, 0, 0, 0, 0, 0, 0, 0 ));

    memcpy( pKiUserCallbackDispatcher, saved_code, sizeof(saved_code));
    FlushInstructionCache(GetCurrentProcess(), pKiUserCallbackDispatcher, sizeof(saved_code));
    VirtualProtect( pKiUserCallbackDispatcher, sizeof(saved_code), old_protect, &old_protect );
}
#endif /* !__REACTOS__ */

static void run_exception_test(void *handler, const void* context,
                               const void *code, unsigned int code_size,
                               unsigned int func2_offset, DWORD access, DWORD handler_flags,
                               void *arg1, void *arg2)
{
    DWORD buf[14];
    RUNTIME_FUNCTION runtime_func[2];
    IMAGE_ARM64_RUNTIME_FUNCTION_ENTRY_XDATA unwind;
    void (*func)(void*,void*) = code_mem;
    DWORD oldaccess, oldaccess2;

    runtime_func[0].BeginAddress = 0;
    runtime_func[0].UnwindData = 0x1000;
    runtime_func[1].BeginAddress = func2_offset;
    runtime_func[1].UnwindData = 0x1014;

    unwind.FunctionLength = func2_offset / 4;
    unwind.Version = 0;
    unwind.ExceptionDataPresent = 1;
    unwind.EpilogInHeader = 1;
    unwind.EpilogCount = 1;
    unwind.CodeWords = 1;
    buf[0] = unwind.HeaderData;
    buf[1] = 0xe3e481e1; /* mov x29,sp; stp r29,lr,[sp,-#0x10]!; end; nop */
    buf[2] = 0x1028;
    *(const void **)&buf[3] = context;

    unwind.FunctionLength = (code_size - func2_offset) / 4;
    buf[5] = unwind.HeaderData;
    buf[6] = 0xe3e481e1; /* mov x29,sp; stp r29,lr,[sp,-#0x10]!; end; nop */
    buf[7] = 0x1028;
    *(const void **)&buf[8] = context;

    buf[10] = 0x5800004f; /* ldr x15, 1f */
    buf[11] = 0xd61f01e0; /* br x15 */
    *(const void **)&buf[12] = handler;

    memcpy((unsigned char *)code_mem + 0x1000, buf, sizeof(buf));
    memcpy(code_mem, code, code_size);
    if (access) VirtualProtect(code_mem, code_size, access, &oldaccess);
    FlushInstructionCache( GetCurrentProcess(), code_mem, 0x2000 );

    pRtlAddFunctionTable(runtime_func, ARRAY_SIZE(runtime_func), (ULONG_PTR)code_mem);
    func( arg1, arg2 );
    pRtlDeleteFunctionTable(runtime_func);

    if (access) VirtualProtect(code_mem, code_size, oldaccess, &oldaccess2);
}

static BOOL got_nested_exception, got_prev_frame_exception;
static void *nested_exception_initial_frame;

static DWORD nested_exception_handler(EXCEPTION_RECORD *rec, EXCEPTION_REGISTRATION_RECORD *frame,
                                      CONTEXT *context, EXCEPTION_REGISTRATION_RECORD **dispatcher)
{
    trace("nested_exception_handler pc %p, sp %p, code %#lx, flags %#lx, ExceptionAddress %p.\n",
          (void *)context->Pc, (void *)context->Sp, rec->ExceptionCode, rec->ExceptionFlags, rec->ExceptionAddress);

    if (rec->ExceptionCode == 0x80000003 && !(rec->ExceptionFlags & EXCEPTION_NESTED_CALL))
    {
        ok(rec->NumberParameters == 1, "Got unexpected rec->NumberParameters %lu.\n", rec->NumberParameters);
        ok((char *)context->Sp == (char *)frame - 0x10, "Got unexpected frame %p / %p.\n", frame, (void *)context->Sp);
        ok((char *)context->Fp == (char *)frame - 0x10, "Got unexpected frame %p / %p.\n", frame, (void *)context->Fp);
        ok((char *)context->Lr == (char *)code_mem + 0x0c, "Got unexpected lr %p.\n", (void *)context->Lr);
        ok((char *)context->Pc == (char *)code_mem + 0x1c, "Got unexpected pc %p.\n", (void *)context->Pc);

        nested_exception_initial_frame = frame;
        RaiseException(0xdeadbeef, 0, 0, 0);
        context->Pc += 4;
        return ExceptionContinueExecution;
    }

    if (rec->ExceptionCode == 0xdeadbeef &&
        (rec->ExceptionFlags == EXCEPTION_NESTED_CALL ||
         rec->ExceptionFlags == (EXCEPTION_NESTED_CALL | EXCEPTION_SOFTWARE_ORIGINATE)))
    {
        ok(!rec->NumberParameters, "Got unexpected rec->NumberParameters %lu.\n", rec->NumberParameters);
        got_nested_exception = TRUE;
        ok(frame == nested_exception_initial_frame, "Got unexpected frame %p / %p.\n",
           frame, nested_exception_initial_frame);
        return ExceptionContinueSearch;
    }

    ok(rec->ExceptionCode == 0xdeadbeef && (!rec->ExceptionFlags || rec->ExceptionFlags == EXCEPTION_SOFTWARE_ORIGINATE),
       "Got unexpected exception code %#lx, flags %#lx.\n", rec->ExceptionCode, rec->ExceptionFlags);
    ok(!rec->NumberParameters, "Got unexpected rec->NumberParameters %lu.\n", rec->NumberParameters);
    ok((char *)frame == (char *)nested_exception_initial_frame + 0x10, "Got unexpected frame %p / %p.\n",
        frame, nested_exception_initial_frame);
    got_prev_frame_exception = TRUE;
    return ExceptionContinueExecution;
}

static const DWORD nested_except_code[] =
{
    0xa9bf7bfd, /* 00: stp x29, x30, [sp, #-16]! */
    0x910003fd, /* 04: mov x29, sp */
    0x94000003, /* 08: bl 1f */
    0xa8c17bfd, /* 0c: ldp x29, x30, [sp], #16 */
    0xd65f03c0, /* 10: ret */

    0xa9bf7bfd, /* 14: stp x29, x30, [sp, #-16]! */
    0x910003fd, /* 18: mov x29, sp */
    0xd43e0000, /* 1c: brk #0xf000 */
    0xd503201f, /* 20: nop */
    0xa8c17bfd, /* 24: ldp x29, x30, [sp], #16 */
    0xd65f03c0, /* 28: ret */
};

static void test_nested_exception(void)
{
    got_nested_exception = got_prev_frame_exception = FALSE;
    run_exception_test(nested_exception_handler, NULL, nested_except_code, sizeof(nested_except_code),
                       5 * sizeof(DWORD), PAGE_EXECUTE_READ, UNW_FLAG_EHANDLER, 0, 0);
    ok(got_nested_exception, "Did not get nested exception.\n");
    ok(got_prev_frame_exception, "Did not get nested exception in the previous frame.\n");
}

static unsigned int collided_unwind_exception_count;

static DWORD collided_exception_handler(EXCEPTION_RECORD *rec, EXCEPTION_REGISTRATION_RECORD *frame,
        CONTEXT *context, EXCEPTION_REGISTRATION_RECORD **dispatcher)
{
    CONTEXT ctx;

    trace("collided_exception_handler pc %p, sp %p, code %#lx, flags %#lx, ExceptionAddress %p, frame %p.\n",
          (void *)context->Pc, (void *)context->Sp, rec->ExceptionCode, rec->ExceptionFlags, rec->ExceptionAddress, frame);

    switch(collided_unwind_exception_count++)
    {
        case 0:
            /* Initial exception from nested_except_code. */
            ok(rec->ExceptionCode == STATUS_BREAKPOINT, "got %#lx.\n", rec->ExceptionCode);
            nested_exception_initial_frame = frame;
            /* Start unwind. */
            pRtlUnwindEx((char *)frame + 0x10, (char *)code_mem + 0x0c, NULL, NULL, &ctx, NULL);
            ok(0, "shouldn't be reached\n");
            break;
        case 1:
            ok(rec->ExceptionCode == STATUS_UNWIND, "got %#lx.\n", rec->ExceptionCode);
            ok(rec->ExceptionFlags == EXCEPTION_UNWINDING, "got %#lx.\n", rec->ExceptionFlags);
            ok((char *)context->Pc == (char *)code_mem + 0x1c, "got %p.\n", (void *)context->Pc);
            /* generate exception in unwind handler. */
            RaiseException(0xdeadbeef, 0, 0, 0);
            ok(0, "shouldn't be reached\n");
            break;
        case 2:
            /* Inner call frame, continue search. */
            ok(rec->ExceptionCode == 0xdeadbeef, "got %#lx.\n", rec->ExceptionCode);
            ok(!rec->ExceptionFlags || rec->ExceptionFlags == EXCEPTION_SOFTWARE_ORIGINATE, "got %#lx.\n", rec->ExceptionFlags);
            ok(frame == nested_exception_initial_frame, "got %p, expected %p.\n", frame, nested_exception_initial_frame);
            break;
        case 3:
            /* Top level call frame, handle exception by unwinding. */
            ok(rec->ExceptionCode == 0xdeadbeef, "got %#lx.\n", rec->ExceptionCode);
            ok(!rec->ExceptionFlags || rec->ExceptionFlags == EXCEPTION_SOFTWARE_ORIGINATE, "got %#lx.\n", rec->ExceptionFlags);
            ok((char *)frame == (char *)nested_exception_initial_frame + 0x10, "got %p, expected %p.\n", frame, nested_exception_initial_frame);
            pRtlUnwindEx((char *)nested_exception_initial_frame + 0x10, (char *)code_mem + 0x0c, NULL, NULL, &ctx, NULL);
            ok(0, "shouldn't be reached\n");
            break;
        case 4:
            /* Collided unwind. */
            ok(rec->ExceptionCode == STATUS_UNWIND, "got %#lx.\n", rec->ExceptionCode);
            ok(rec->ExceptionFlags == (EXCEPTION_UNWINDING | EXCEPTION_COLLIDED_UNWIND), "got %#lx.\n", rec->ExceptionFlags);
            ok(frame == nested_exception_initial_frame, "got %p, expected %p.\n", frame, nested_exception_initial_frame);
            break;
        case 5:
            /* EXCEPTION_COLLIDED_UNWIND cleared for the following frames. */
            ok(rec->ExceptionCode == STATUS_UNWIND, "got %#lx.\n", rec->ExceptionCode);
            ok(rec->ExceptionFlags == (EXCEPTION_UNWINDING | EXCEPTION_TARGET_UNWIND), "got %#lx.\n", rec->ExceptionFlags);
            ok((char *)frame == (char *)nested_exception_initial_frame + 0x10, "got %p, expected %p.\n", frame, nested_exception_initial_frame);
            break;
    }
    return ExceptionContinueSearch;
}

static void test_collided_unwind(void)
{
    got_nested_exception = got_prev_frame_exception = FALSE;
    collided_unwind_exception_count = 0;
    run_exception_test(collided_exception_handler, NULL, nested_except_code, sizeof(nested_except_code),
                       5 * sizeof(DWORD), PAGE_EXECUTE_READ, UNW_FLAG_EHANDLER | UNW_FLAG_UHANDLER, 0, 0);
    ok(collided_unwind_exception_count == 6, "got %u.\n", collided_unwind_exception_count);
}


#ifndef __REACTOS__
static int rtlraiseexception_unhandled_handler_called;
static int rtlraiseexception_teb_handler_called;
static int rtlraiseexception_handler_called;

static void rtlraiseexception_handler_( EXCEPTION_RECORD *rec, void *frame, CONTEXT *context,
                                        void *dispatcher, BOOL unhandled_handler )
{
    void *addr = rec->ExceptionAddress;

    trace( "exception: %08lx flags:%lx addr:%p context: Pc:%p\n",
           rec->ExceptionCode, rec->ExceptionFlags, rec->ExceptionAddress, (void *)context->Pc );

    ok( addr == (char *)code_mem + 0x0c,
        "ExceptionAddress at %p instead of %p\n", addr, (char *)code_mem + 0x0c );
    ok( context->ContextFlags == (CONTEXT_FULL | CONTEXT_UNWOUND_TO_CALL) ||
        context->ContextFlags == (CONTEXT_FULL | CONTEXT_DEBUG_REGISTERS),
        "wrong context flags %lx\n", context->ContextFlags );
    ok( context->Pc == (UINT_PTR)addr,
        "%d: Pc at %Ix instead of %Ix\n", test_stage, context->Pc, (UINT_PTR)addr );

    ok( context->X0 == 0xf00f00f0, "context->X0 is %Ix, should have been set to 0xf00f00f0 in vectored handler\n", context->X0 );
}

static LONG CALLBACK rtlraiseexception_unhandled_handler(EXCEPTION_POINTERS *ExceptionInfo)
{
    PCONTEXT context = ExceptionInfo->ContextRecord;
    PEXCEPTION_RECORD rec = ExceptionInfo->ExceptionRecord;

    rtlraiseexception_unhandled_handler_called = 1;
    rtlraiseexception_handler_(rec, NULL, context, NULL, TRUE);
    if (test_stage == STAGE_RTLRAISE_HANDLE_LAST_CHANCE) return EXCEPTION_CONTINUE_SEARCH;

    return EXCEPTION_CONTINUE_EXECUTION;
}

static DWORD WINAPI rtlraiseexception_teb_handler( EXCEPTION_RECORD *rec,
                                                   EXCEPTION_REGISTRATION_RECORD *frame,
                                                   CONTEXT *context,
                                                   EXCEPTION_REGISTRATION_RECORD **dispatcher )
{
    rtlraiseexception_teb_handler_called = 1;
    rtlraiseexception_handler_(rec, frame, context, dispatcher, FALSE);
    return ExceptionContinueSearch;
}

static DWORD WINAPI rtlraiseexception_handler( EXCEPTION_RECORD *rec, void *frame,
                                               CONTEXT *context, DISPATCHER_CONTEXT *dispatcher )
{
    DISPATCHER_CONTEXT_NONVOLREG_ARM64 *nonvol_regs = (void *)dispatcher->NonVolatileRegisters;
    int i;

    for (i = 0; i < NONVOL_INT_NUMREG_ARM64; i++)
        ok( nonvol_regs->GpNvRegs[i] == ((DWORD64 *)&context->X19)[i],
            "wrong non volatile reg x%u %I64x / %I64x\n", i + 19,
            nonvol_regs->GpNvRegs[i] , ((DWORD64 *)&context->X19)[i] );
    for (i = 0; i < NONVOL_FP_NUMREG_ARM64; i++)
        ok( nonvol_regs->FpNvRegs[i] == context->V[i + 8].D[0],
            "wrong non volatile reg d%u %g / %g\n", i + 8,
            nonvol_regs->FpNvRegs[i] , context->V[i + 8].D[0] );

    rtlraiseexception_handler_called = 1;
    rtlraiseexception_handler_(rec, frame, context, dispatcher, FALSE);
    return ExceptionContinueSearch;
}

static LONG CALLBACK rtlraiseexception_vectored_handler(EXCEPTION_POINTERS *ExceptionInfo)
{
    PCONTEXT context = ExceptionInfo->ContextRecord;
    PEXCEPTION_RECORD rec = ExceptionInfo->ExceptionRecord;
    void *addr = rec->ExceptionAddress;

    ok( addr == (char *)code_mem + 0xc,
        "ExceptionAddress at %p instead of %p\n", addr, (char *)code_mem + 0xc );
    ok( context->Pc == (UINT_PTR)addr,
        "%d: Pc at %Ix instead of %Ix\n", test_stage, context->Pc, (UINT_PTR)addr );

    context->X0 = 0xf00f00f0;

    return EXCEPTION_CONTINUE_SEARCH;
}

static const DWORD call_one_arg_code[] =
{
    0xa9bf7bfd, /* 00: stp x29, x30, [sp, #-16]! */
    0x910003fd, /* 04: mov x29, sp */
    0xd63f0020, /* 08: blr x1 */
    0xd503201f, /* 0c: nop */
    0xa8c17bfd, /* 10: ldp x29, x30, [sp], #16 */
    0xd65f03c0, /* 14: ret */
};

static void run_rtlraiseexception_test(DWORD exceptioncode)
{
    EXCEPTION_REGISTRATION_RECORD frame;
    EXCEPTION_RECORD record;
    PVOID vectored_handler = NULL;

    record.ExceptionCode = exceptioncode;
    record.ExceptionFlags = 0;
    record.ExceptionRecord = NULL;
    record.ExceptionAddress = NULL; /* does not matter, copied return address */
    record.NumberParameters = 0;

    frame.Handler = rtlraiseexception_teb_handler;
    frame.Prev = NtCurrentTeb()->Tib.ExceptionList;

    NtCurrentTeb()->Tib.ExceptionList = &frame;
    vectored_handler = pRtlAddVectoredExceptionHandler(TRUE, rtlraiseexception_vectored_handler);
    ok(vectored_handler != 0, "RtlAddVectoredExceptionHandler failed\n");
    if (pRtlSetUnhandledExceptionFilter) pRtlSetUnhandledExceptionFilter(rtlraiseexception_unhandled_handler);

    rtlraiseexception_handler_called = 0;
    rtlraiseexception_teb_handler_called = 0;
    rtlraiseexception_unhandled_handler_called = 0;

    run_exception_test( rtlraiseexception_handler, NULL, call_one_arg_code,
                        sizeof(call_one_arg_code), sizeof(call_one_arg_code),
                        PAGE_EXECUTE_READ, UNW_FLAG_EHANDLER,
                        &record, pRtlRaiseException);

    ok( record.ExceptionAddress == (char *)code_mem + 0x0c,
        "address set to %p instead of %p\n", record.ExceptionAddress, (char *)code_mem + 0x0c );

    todo_wine
    ok( !rtlraiseexception_teb_handler_called, "Frame TEB handler called\n" );
    ok( rtlraiseexception_handler_called, "Frame handler called\n" );
    ok( rtlraiseexception_unhandled_handler_called, "UnhandledExceptionFilter wasn't called\n" );

    pRtlRemoveVectoredExceptionHandler(vectored_handler);

    if (pRtlSetUnhandledExceptionFilter) pRtlSetUnhandledExceptionFilter(NULL);
    NtCurrentTeb()->Tib.ExceptionList = frame.Prev;
}

static void test_rtlraiseexception(void)
{
    run_rtlraiseexception_test(0x12345);
    run_rtlraiseexception_test(EXCEPTION_BREAKPOINT);
    run_rtlraiseexception_test(EXCEPTION_INVALID_HANDLE);
}
#endif /* !__REACTOS__ */

static DWORD brk_exception_handler_code;

static DWORD WINAPI brk_exception_handler( EXCEPTION_RECORD *rec, void *frame,
                                           CONTEXT *context, DISPATCHER_CONTEXT *dispatcher )
{
    ok( rec->ExceptionCode == brk_exception_handler_code, "got: %08lx\n", rec->ExceptionCode );
    ok( rec->NumberParameters == 0, "got: %ld\n", rec->NumberParameters );
    ok( rec->ExceptionAddress == (void *)context->Pc, "got addr: %p, pc: %p\n", rec->ExceptionAddress, (void *)context->Pc );
    context->Pc += 4;
    return ExceptionContinueExecution;
}


static void test_brk(void)
{
    DWORD call_brk[] =
    {
        0xa9bf7bfd, /* 00: stp x29, x30, [sp, #-16]! */
        0x910003fd, /* 04: mov x29, sp */
        0x00000000, /* 08: <filled in later> */
        0xd503201f, /* 0c: nop */
        0xa8c17bfd, /* 10: ldp x29, x30, [sp], #16 */
        0xd65f03c0, /* 14: ret */
    };

    /* brk #0xf000 is tested as part of breakpoint tests */

    brk_exception_handler_code = STATUS_ASSERTION_FAILURE;
    call_brk[2] = 0xd43e0020; /* 08: brk #0xf001 */
    run_exception_test( brk_exception_handler, NULL, call_brk,
                        sizeof(call_brk), sizeof(call_brk),
                        PAGE_EXECUTE_READ, UNW_FLAG_EHANDLER,
                        0, 0 );

    /* FIXME: brk #0xf002 needs debug service tests */

    /* brk #0xf003 is tested as part of fastfail tests*/

    brk_exception_handler_code = EXCEPTION_INT_DIVIDE_BY_ZERO;
    call_brk[2] = 0xd43e0080; /* 08: brk #0xf004 */
    run_exception_test( brk_exception_handler, NULL, call_brk,
                        sizeof(call_brk), sizeof(call_brk),
                        PAGE_EXECUTE_READ, UNW_FLAG_EHANDLER,
                        0, 0 );

    /* Any unknown immediate raises EXCEPTION_ILLEGAL_INSTRUCTION */

    brk_exception_handler_code = EXCEPTION_ILLEGAL_INSTRUCTION;
    call_brk[2] = 0xd43e00a0; /* 08: brk #0xf005 */
    run_exception_test( brk_exception_handler, NULL, call_brk,
                        sizeof(call_brk), sizeof(call_brk),
                        PAGE_EXECUTE_READ, UNW_FLAG_EHANDLER,
                        0, 0 );

    brk_exception_handler_code = EXCEPTION_ILLEGAL_INSTRUCTION;
    call_brk[2] = 0xd4200000; /* 08: brk #0x0 */
    run_exception_test( brk_exception_handler, NULL, call_brk,
                        sizeof(call_brk), sizeof(call_brk),
                        PAGE_EXECUTE_READ, UNW_FLAG_EHANDLER,
                        0, 0 );
}

static LONG consolidate_dummy_called;
static LONG pass;

static const DWORD call_rtlunwind[] =
{
    0xa88150f3, /* stp x19, x20, [x7], #0x10 */
    0xa88158f5, /* stp x21, x22, [x7], #0x10 */
    0xa88160f7, /* stp x23, x24, [x7], #0x10 */
    0xa88168f9, /* stp x25, x26, [x7], #0x10 */
    0xa88170fb, /* stp x27, x28, [x7], #0x10 */
    0xf80084fd, /* str x29,      [x7], #0x8 */
    0x6c8124e8, /* stp d8,  d9,  [x7], #0x10 */
    0x6c812cea, /* stp d10, d11, [x7], #0x10 */
    0x6c8134ec, /* stp d12, d13, [x7], #0x10 */
    0x6c813cee, /* stp d14, d15, [x7], #0x10 */
    0xd61f00c0, /* br x6 */
};

static PVOID CALLBACK test_consolidate_dummy(EXCEPTION_RECORD *rec)
{
    CONTEXT *ctx = (CONTEXT *)rec->ExceptionInformation[1];
    DWORD64 *saved_regs = (DWORD64 *)rec->ExceptionInformation[3];
    DISPATCHER_CONTEXT_NONVOLREG_ARM64 *regs;
    int i;

    switch (InterlockedIncrement(&consolidate_dummy_called))
    {
    case 1:  /* RtlRestoreContext */
        ok(ctx->Pc == 0xdeadbeef, "RtlRestoreContext wrong Pc, expected: 0xdeadbeef, got: %Ix\n", ctx->Pc);
        ok( rec->ExceptionInformation[10] == -1, "wrong info %Ix\n", rec->ExceptionInformation[10] );
        break;
    case 2: /* RtlUnwindEx */
        ok(ctx->Pc != 0xdeadbeef, "RtlUnwindEx wrong Pc, got: %Ix\n", ctx->Pc );
        ok( rec->ExceptionInformation[10] != -1, "wrong info %Ix\n", rec->ExceptionInformation[10] );
        regs = (DISPATCHER_CONTEXT_NONVOLREG_ARM64 *)rec->ExceptionInformation[10];
        for (i = 0; i < 11; i++)
            ok( saved_regs[i] == regs->GpNvRegs[i], "wrong reg X%u, expected: %Ix, got: %Ix\n",
                19 + i, saved_regs[i], regs->GpNvRegs[i] );
        for (i = 0; i < 8; i++)
            ok( saved_regs[i + 11] == *(DWORD64 *)&regs->FpNvRegs[i],
                "wrong reg D%u, expected: %Ix, got: %Ix\n",
                i + 8, saved_regs[i + 11], *(DWORD64 *)&regs->FpNvRegs[i] );
        break;
    }
    return (PVOID)rec->ExceptionInformation[2];
}

static void test_restore_context(void)
{
    EXCEPTION_RECORD rec;
    _JUMP_BUFFER buf;
    CONTEXT ctx;
    int i;

    if (!pRtlUnwindEx || !pRtlRestoreContext || !pRtlCaptureContext)
    {
        skip("RtlUnwindEx/RtlCaptureContext/RtlRestoreContext not found\n");
        return;
    }

    /* test simple case of capture and restore context */
    pass = 0;
    InterlockedIncrement(&pass); /* interlocked to prevent compiler from moving after capture */
    pRtlCaptureContext(&ctx);
    if (InterlockedIncrement(&pass) == 2) /* interlocked to prevent compiler from moving before capture */
    {
        pRtlRestoreContext(&ctx, NULL);
        ok(0, "shouldn't be reached\n");
    }
    else
        ok(pass < 4, "unexpected pass %ld\n", pass);

    /* test with jmp using RtlRestoreContext */
    pass = 0;
    InterlockedIncrement(&pass);
    RtlCaptureContext(&ctx);
    InterlockedIncrement(&pass); /* only called once */
    setjmp((_JBTYPE *)&buf);
    InterlockedIncrement(&pass);
    if (pass == 3)
    {
        rec.ExceptionCode = STATUS_LONGJUMP;
        rec.NumberParameters = 1;
        rec.ExceptionInformation[0] = (DWORD64)&buf;
        /* uses buf.Pc instead of ctx.Pc */
        pRtlRestoreContext(&ctx, &rec);
        ok(0, "shouldn't be reached\n");
    }
    else if (pass == 4)
    {
        ok(buf.X19 == ctx.X19, "longjmp failed for X19, expected: %Ix, got: %Ix\n", buf.X19, ctx.X19);
        ok(buf.X20 == ctx.X20, "longjmp failed for X20, expected: %Ix, got: %Ix\n", buf.X20, ctx.X20);
        ok(buf.X21 == ctx.X21, "longjmp failed for X21, expected: %Ix, got: %Ix\n", buf.X21, ctx.X21);
        ok(buf.X22 == ctx.X22, "longjmp failed for X22, expected: %Ix, got: %Ix\n", buf.X22, ctx.X22);
        ok(buf.X23 == ctx.X23, "longjmp failed for X23, expected: %Ix, got: %Ix\n", buf.X23, ctx.X23);
        ok(buf.X24 == ctx.X24, "longjmp failed for X24, expected: %Ix, got: %Ix\n", buf.X24, ctx.X24);
        ok(buf.X25 == ctx.X25, "longjmp failed for X25, expected: %Ix, got: %Ix\n", buf.X25, ctx.X25);
        ok(buf.X26 == ctx.X26, "longjmp failed for X26, expected: %Ix, got: %Ix\n", buf.X26, ctx.X26);
        ok(buf.X27 == ctx.X27, "longjmp failed for X27, expected: %Ix, got: %Ix\n", buf.X27, ctx.X27);
        ok(buf.X28 == ctx.X28, "longjmp failed for X28, expected: %Ix, got: %Ix\n", buf.X28, ctx.X28);
        ok(buf.Fp  == ctx.Fp,  "longjmp failed for Fp, expected: %Ix, got: %Ix\n",  buf.Fp,  ctx.Fp);
        for (i = 0; i < 8; i++)
            ok(buf.D[i] == ctx.V[i + 8].D[0], "longjmp failed for D%u, expected: %g, got: %g\n",
               i + 8, buf.D[i], ctx.V[i + 8].D[0]);
        pRtlRestoreContext(&ctx, &rec);
        ok(0, "shouldn't be reached\n");
    }
    else
        ok(pass == 5, "unexpected pass %ld\n", pass);

    /* test with jmp through RtlUnwindEx */
    pass = 0;
    InterlockedIncrement(&pass);
    pRtlCaptureContext(&ctx);
    InterlockedIncrement(&pass); /* only called once */
    setjmp((_JBTYPE *)&buf);
    InterlockedIncrement(&pass);
    if (pass == 3)
    {
        rec.ExceptionCode = STATUS_LONGJUMP;
        rec.NumberParameters = 1;
        rec.ExceptionInformation[0] = (DWORD64)&buf;

        /* uses buf.Pc instead of bogus 0xdeadbeef */
        pRtlUnwindEx((void*)buf.Sp, (void*)0xdeadbeef, &rec, NULL, &ctx, NULL);
        ok(0, "shouldn't be reached\n");
    }
    else
        ok(pass == 4, "unexpected pass %ld\n", pass);


    /* test with consolidate */
    pass = 0;
    InterlockedIncrement(&pass);
    RtlCaptureContext(&ctx);
    InterlockedIncrement(&pass);
    if (pass == 2)
    {
        rec.ExceptionCode = STATUS_UNWIND_CONSOLIDATE;
        rec.NumberParameters = 3;
        rec.ExceptionInformation[0] = (DWORD64)test_consolidate_dummy;
        rec.ExceptionInformation[1] = (DWORD64)&ctx;
        rec.ExceptionInformation[2] = ctx.Pc;
        rec.ExceptionInformation[10] = -1;
        ctx.Pc = 0xdeadbeef;

        pRtlRestoreContext(&ctx, &rec);
        ok(0, "shouldn't be reached\n");
    }
    else if (pass == 3)
        ok(consolidate_dummy_called == 1, "test_consolidate_dummy not called\n");
    else
        ok(0, "unexpected pass %ld\n", pass);

    /* test with consolidate through RtlUnwindEx */
    pass = 0;
    InterlockedIncrement(&pass);
    pRtlCaptureContext(&ctx);
    InterlockedIncrement(&pass);
    setjmp((_JBTYPE *)&buf);
    if (pass == 2)
    {
        void (*func)(DWORD64,DWORD64,EXCEPTION_RECORD*,DWORD64,CONTEXT*,void*,void*,void*) = code_mem;
        DWORD64 nonvol_regs[19];

        rec.ExceptionCode = STATUS_UNWIND_CONSOLIDATE;
        rec.NumberParameters = 4;
        rec.ExceptionInformation[0] = (DWORD64)test_consolidate_dummy;
        rec.ExceptionInformation[1] = (DWORD64)&ctx;
        rec.ExceptionInformation[2] = ctx.Pc;
        rec.ExceptionInformation[3] = (DWORD64)nonvol_regs;
        rec.ExceptionInformation[10] = -1;  /* otherwise it doesn't get set */
        ctx.Pc = 0xdeadbeef;
        /* uses consolidate callback Pc instead of bogus 0xdeadbeef */
        memcpy( code_mem, call_rtlunwind, sizeof(call_rtlunwind) );
        FlushInstructionCache( GetCurrentProcess(), code_mem, sizeof(call_rtlunwind) );
        func( buf.Frame, 0xdeadbeef, &rec, 0, &ctx, NULL, pRtlUnwindEx, nonvol_regs );
        ok(0, "shouldn't be reached\n");
    }
    else if (pass == 3)
        ok(consolidate_dummy_called == 2, "test_consolidate_dummy not called\n");
    else
        ok(0, "unexpected pass %ld\n", pass);
}

static void test_mrs_currentel(void)
{
    DWORD64 (*func_ptr)(void) = code_mem;
    DWORD64 result;

    static const DWORD call_func[] =
    {
        0xd5384240, /* mrs x0, CurrentEL */
        0xd538425f, /* mrs xzr, CurrentEL */
        0xd65f03c0, /* ret */
    };

    memcpy( func_ptr, call_func, sizeof(call_func) );
    FlushInstructionCache( GetCurrentProcess(), func_ptr, sizeof(call_func) );
    result = func_ptr();
    ok( result == 0, "expected 0, got %llx\n", result );
}



#endif  /* __x86_64__ || __aarch64__ */

#if defined(__i386__) || defined(__x86_64__)

static DWORD WINAPI register_check_thread(void *arg)
{
    NTSTATUS status;
    CONTEXT ctx;

    memset(&ctx, 0, sizeof(ctx));
    ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;

    status = pNtGetContextThread(GetCurrentThread(), &ctx);
    ok(status == STATUS_SUCCESS, "NtGetContextThread failed with %x\n", status);
    ok(!ctx.Dr0, "expected 0, got %lx\n", (DWORD_PTR)ctx.Dr0);
    ok(!ctx.Dr1, "expected 0, got %lx\n", (DWORD_PTR)ctx.Dr1);
    ok(!ctx.Dr2, "expected 0, got %lx\n", (DWORD_PTR)ctx.Dr2);
    ok(!ctx.Dr3, "expected 0, got %lx\n", (DWORD_PTR)ctx.Dr3);
    ok(!ctx.Dr6, "expected 0, got %lx\n", (DWORD_PTR)ctx.Dr6);
    ok(!ctx.Dr7, "expected 0, got %lx\n", (DWORD_PTR)ctx.Dr7);

    return 0;
}

static void test_debug_registers(void)
{
    static const struct
    {
        ULONG_PTR dr0, dr1, dr2, dr3, dr6, dr7;
    }
    tests[] =
    {
        { 0x42424240, 0, 0x126bb070, 0x0badbad0, 0, 0xffff0115 },
        { 0x42424242, 0, 0x100f0fe7, 0x0abebabe, 0, 0x115 },
    };
    NTSTATUS status;
    CONTEXT ctx;
    HANDLE thread;
    int i;

    for (i = 0; i < sizeof(tests)/sizeof(tests[0]); i++)
    {
        memset(&ctx, 0, sizeof(ctx));
        ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
        ctx.Dr0 = tests[i].dr0;
        ctx.Dr1 = tests[i].dr1;
        ctx.Dr2 = tests[i].dr2;
        ctx.Dr3 = tests[i].dr3;
        ctx.Dr6 = tests[i].dr6;
        ctx.Dr7 = tests[i].dr7;

        status = pNtSetContextThread(GetCurrentThread(), &ctx);
        ok(status == STATUS_SUCCESS, "NtGetContextThread failed with %08x\n", status);

        memset(&ctx, 0, sizeof(ctx));
        ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;

        status = pNtGetContextThread(GetCurrentThread(), &ctx);
        ok(status == STATUS_SUCCESS, "NtGetContextThread failed with %08x\n", status);
        ok(ctx.Dr0 == tests[i].dr0, "test %d: expected %lx, got %lx\n", i, tests[i].dr0, (DWORD_PTR)ctx.Dr0);
        ok(ctx.Dr1 == tests[i].dr1, "test %d: expected %lx, got %lx\n", i, tests[i].dr1, (DWORD_PTR)ctx.Dr1);
        ok(ctx.Dr2 == tests[i].dr2, "test %d: expected %lx, got %lx\n", i, tests[i].dr2, (DWORD_PTR)ctx.Dr2);
        ok(ctx.Dr3 == tests[i].dr3, "test %d: expected %lx, got %lx\n", i, tests[i].dr3, (DWORD_PTR)ctx.Dr3);
        ok((ctx.Dr6 &  0xf00f) == tests[i].dr6, "test %d: expected %lx, got %lx\n", i, tests[i].dr6, (DWORD_PTR)ctx.Dr6);
        ok((ctx.Dr7 & ~0xdc00) == tests[i].dr7, "test %d: expected %lx, got %lx\n", i, tests[i].dr7, (DWORD_PTR)ctx.Dr7);
    }

    memset(&ctx, 0, sizeof(ctx));
    ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    ctx.Dr0 = 0xffffffff;
    ctx.Dr1 = 0xffffffff;
    ctx.Dr2 = 0xffffffff;
    ctx.Dr3 = 0xffffffff;
    ctx.Dr6 = 0xffffffff;
    ctx.Dr7 = 0x00000400;
    status = pNtSetContextThread(GetCurrentThread(), &ctx);
    ok(status == STATUS_SUCCESS, "NtSetContextThread failed with %x\n", status);

    thread = CreateThread(NULL, 0, register_check_thread, NULL, CREATE_SUSPENDED, NULL);
    ok(thread != INVALID_HANDLE_VALUE, "CreateThread failed with %d\n", GetLastError());

    ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    status = pNtGetContextThread(thread, &ctx);
    ok(status == STATUS_SUCCESS, "NtGetContextThread failed with %x\n", status);
    ok(!ctx.Dr0, "expected 0, got %lx\n", (DWORD_PTR)ctx.Dr0);
    ok(!ctx.Dr1, "expected 0, got %lx\n", (DWORD_PTR)ctx.Dr1);
    ok(!ctx.Dr2, "expected 0, got %lx\n", (DWORD_PTR)ctx.Dr2);
    ok(!ctx.Dr3, "expected 0, got %lx\n", (DWORD_PTR)ctx.Dr3);
    ok(!ctx.Dr6, "expected 0, got %lx\n", (DWORD_PTR)ctx.Dr6);
    ok(!ctx.Dr7, "expected 0, got %lx\n", (DWORD_PTR)ctx.Dr7);

    ResumeThread(thread);
    WaitForSingleObject(thread, 10000);
    CloseHandle(thread);
}

static DWORD outputdebugstring_exceptions;

static LONG CALLBACK outputdebugstring_vectored_handler(EXCEPTION_POINTERS *ExceptionInfo)
{
    PEXCEPTION_RECORD rec = ExceptionInfo->ExceptionRecord;
    trace("vect. handler %08x addr:%p\n", rec->ExceptionCode, rec->ExceptionAddress);

    ok(rec->ExceptionCode == DBG_PRINTEXCEPTION_C, "ExceptionCode is %08x instead of %08x\n",
        rec->ExceptionCode, DBG_PRINTEXCEPTION_C);
    ok(rec->NumberParameters == 2, "ExceptionParameters is %d instead of 2\n", rec->NumberParameters);
    ok(rec->ExceptionInformation[0] == 12, "ExceptionInformation[0] = %d instead of 12\n", (DWORD)rec->ExceptionInformation[0]);
    ok(!strcmp((char *)rec->ExceptionInformation[1], "Hello World"),
        "ExceptionInformation[1] = '%s' instead of 'Hello World'\n", (char *)rec->ExceptionInformation[1]);

    outputdebugstring_exceptions++;
    return EXCEPTION_CONTINUE_SEARCH;
}

static void test_outputdebugstring(DWORD numexc)
{
    PVOID vectored_handler;

    if (!pRtlAddVectoredExceptionHandler || !pRtlRemoveVectoredExceptionHandler)
    {
        skip("RtlAddVectoredExceptionHandler or RtlRemoveVectoredExceptionHandler not found\n");
        return;
    }

    vectored_handler = pRtlAddVectoredExceptionHandler(TRUE, &outputdebugstring_vectored_handler);
    ok(vectored_handler != 0, "RtlAddVectoredExceptionHandler failed\n");

    outputdebugstring_exceptions = 0;
    OutputDebugStringA("Hello World");

    ok(outputdebugstring_exceptions == numexc, "OutputDebugStringA generated %d exceptions, expected %d\n",
       outputdebugstring_exceptions, numexc);

    pRtlRemoveVectoredExceptionHandler(vectored_handler);
}

static DWORD ripevent_exceptions;

static LONG CALLBACK ripevent_vectored_handler(EXCEPTION_POINTERS *ExceptionInfo)
{
    PEXCEPTION_RECORD rec = ExceptionInfo->ExceptionRecord;
    trace("vect. handler %08x addr:%p\n", rec->ExceptionCode, rec->ExceptionAddress);

    ok(rec->ExceptionCode == DBG_RIPEXCEPTION, "ExceptionCode is %08x instead of %08x\n",
       rec->ExceptionCode, DBG_RIPEXCEPTION);
    ok(rec->NumberParameters == 2, "ExceptionParameters is %d instead of 2\n", rec->NumberParameters);
    ok(rec->ExceptionInformation[0] == 0x11223344, "ExceptionInformation[0] = %08x instead of %08x\n",
       (NTSTATUS)rec->ExceptionInformation[0], 0x11223344);
    ok(rec->ExceptionInformation[1] == 0x55667788, "ExceptionInformation[1] = %08x instead of %08x\n",
       (NTSTATUS)rec->ExceptionInformation[1], 0x55667788);

    ripevent_exceptions++;
    return (rec->ExceptionCode == DBG_RIPEXCEPTION) ? EXCEPTION_CONTINUE_EXECUTION : EXCEPTION_CONTINUE_SEARCH;
}

static void test_ripevent(DWORD numexc)
{
    EXCEPTION_RECORD record;
    PVOID vectored_handler;

    if (!pRtlAddVectoredExceptionHandler || !pRtlRemoveVectoredExceptionHandler || !pRtlRaiseException)
    {
        skip("RtlAddVectoredExceptionHandler or RtlRemoveVectoredExceptionHandler or RtlRaiseException not found\n");
        return;
    }

    vectored_handler = pRtlAddVectoredExceptionHandler(TRUE, &ripevent_vectored_handler);
    ok(vectored_handler != 0, "RtlAddVectoredExceptionHandler failed\n");

    record.ExceptionCode = DBG_RIPEXCEPTION;
    record.ExceptionFlags = 0;
    record.ExceptionRecord = NULL;
    record.ExceptionAddress = NULL;
    record.NumberParameters = 2;
    record.ExceptionInformation[0] = 0x11223344;
    record.ExceptionInformation[1] = 0x55667788;

    ripevent_exceptions = 0;
    pRtlRaiseException(&record);
    ok(ripevent_exceptions == numexc, "RtlRaiseException generated %d exceptions, expected %d\n",
       ripevent_exceptions, numexc);

    pRtlRemoveVectoredExceptionHandler(vectored_handler);
}

static DWORD debug_service_exceptions;

static LONG CALLBACK debug_service_handler(EXCEPTION_POINTERS *ExceptionInfo)
{
    EXCEPTION_RECORD *rec = ExceptionInfo->ExceptionRecord;
    trace("vect. handler %08x addr:%p\n", rec->ExceptionCode, rec->ExceptionAddress);

    ok(rec->ExceptionCode == EXCEPTION_BREAKPOINT, "ExceptionCode is %08x instead of %08x\n",
       rec->ExceptionCode, EXCEPTION_BREAKPOINT);

#ifdef __i386__
    ok(ExceptionInfo->ContextRecord->Eip == (DWORD)code_mem + 0x1c,
       "expected Eip = %x, got %x\n", (DWORD)code_mem + 0x1c, ExceptionInfo->ContextRecord->Eip);
    ok(rec->NumberParameters == (is_wow64 ? 1 : 3),
       "ExceptionParameters is %d instead of %d\n", rec->NumberParameters, is_wow64 ? 1 : 3);
    ok(rec->ExceptionInformation[0] == ExceptionInfo->ContextRecord->Eax,
       "expected ExceptionInformation[0] = %x, got %lx\n",
       ExceptionInfo->ContextRecord->Eax, rec->ExceptionInformation[0]);
    if (!is_wow64)
    {
        ok(rec->ExceptionInformation[1] == 0x11111111,
           "got ExceptionInformation[1] = %lx\n", rec->ExceptionInformation[1]);
        ok(rec->ExceptionInformation[2] == 0x22222222,
           "got ExceptionInformation[2] = %lx\n", rec->ExceptionInformation[2]);
    }
#else
    ok(ExceptionInfo->ContextRecord->Rip == (DWORD_PTR)code_mem + 0x2f,
       "expected Rip = %lx, got %lx\n", (DWORD_PTR)code_mem + 0x2f, ExceptionInfo->ContextRecord->Rip);
    ok(rec->NumberParameters == 1,
       "ExceptionParameters is %d instead of 1\n", rec->NumberParameters);
    ok(rec->ExceptionInformation[0] == ExceptionInfo->ContextRecord->Rax,
       "expected ExceptionInformation[0] = %lx, got %lx\n",
       ExceptionInfo->ContextRecord->Rax, rec->ExceptionInformation[0]);
#endif

    debug_service_exceptions++;
    return (rec->ExceptionCode == EXCEPTION_BREAKPOINT) ? EXCEPTION_CONTINUE_EXECUTION : EXCEPTION_CONTINUE_SEARCH;
}

#ifdef __i386__

static const BYTE call_debug_service_code[] = {
    0x53,                         /* pushl %ebx */
    0x57,                         /* pushl %edi */
    0x8b, 0x44, 0x24, 0x0c,       /* movl 12(%esp),%eax */
    0xb9, 0x11, 0x11, 0x11, 0x11, /* movl $0x11111111,%ecx */
    0xba, 0x22, 0x22, 0x22, 0x22, /* movl $0x22222222,%edx */
    0xbb, 0x33, 0x33, 0x33, 0x33, /* movl $0x33333333,%ebx */
    0xbf, 0x44, 0x44, 0x44, 0x44, /* movl $0x44444444,%edi */
    0xcd, 0x2d,                   /* int $0x2d */
    0xeb,                         /* jmp $+17 */
    0x0f, 0x1f, 0x00,             /* nop */
    0x31, 0xc0,                   /* xorl %eax,%eax */
    0xeb, 0x0c,                   /* jmp $+14 */
    0x90, 0x90, 0x90, 0x90,       /* nop */
    0x90, 0x90, 0x90, 0x90,
    0x90,
    0x31, 0xc0,                   /* xorl %eax,%eax */
    0x40,                         /* incl %eax */
    0x5f,                         /* popl %edi */
    0x5b,                         /* popl %ebx */
    0xc3,                         /* ret */
};

#else

static const BYTE call_debug_service_code[] = {
    0x53,                         /* push %rbx */
    0x57,                         /* push %rdi */
    0x48, 0x89, 0xc8,             /* movl %rcx,%rax */
    0x48, 0xb9, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, /* movabs $0x1111111111111111,%rcx */
    0x48, 0xba, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, /* movabs $0x2222222222222222,%rdx */
    0x48, 0xbb, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, /* movabs $0x3333333333333333,%rbx */
    0x48, 0xbf, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, /* movabs $0x4444444444444444,%rdi */
    0xcd, 0x2d,                   /* int $0x2d */
    0xeb,                         /* jmp $+17 */
    0x0f, 0x1f, 0x00,             /* nop */
    0x48, 0x31, 0xc0,             /* xor %rax,%rax */
    0xeb, 0x0e,                   /* jmp $+16 */
    0x90, 0x90, 0x90, 0x90,       /* nop */
    0x90, 0x90, 0x90, 0x90,
    0x48, 0x31, 0xc0,             /* xor %rax,%rax */
    0x48, 0xff, 0xc0,             /* inc %rax */
    0x5f,                         /* pop %rdi */
    0x5b,                         /* pop %rbx */
    0xc3,                         /* ret */
};

#endif

static void test_debug_service(DWORD numexc)
{
    DWORD (CDECL *func)(DWORD_PTR) = code_mem;
    DWORD expected_exc, expected_ret;
    void *vectored_handler;
    DWORD ret;

    /* code will return 0 if execution resumes immediately after "int $0x2d", otherwise 1 */
    memcpy(code_mem, call_debug_service_code, sizeof(call_debug_service_code));

    vectored_handler = pRtlAddVectoredExceptionHandler(TRUE, &debug_service_handler);
    ok(vectored_handler != 0, "RtlAddVectoredExceptionHandler failed\n");

    expected_exc = numexc;
    expected_ret = (numexc != 0);

    /* BREAKPOINT_BREAK */
    debug_service_exceptions = 0;
    ret = func(0);
    ok(debug_service_exceptions == expected_exc,
       "BREAKPOINT_BREAK generated %u exceptions, expected %u\n",
       debug_service_exceptions, expected_exc);
    ok(ret == expected_ret,
       "BREAKPOINT_BREAK returned %u, expected %u\n", ret, expected_ret);

    /* BREAKPOINT_PROMPT */
    debug_service_exceptions = 0;
    ret = func(2);
    ok(debug_service_exceptions == expected_exc,
       "BREAKPOINT_PROMPT generated %u exceptions, expected %u\n",
       debug_service_exceptions, expected_exc);
    ok(ret == expected_ret,
       "BREAKPOINT_PROMPT returned %u, expected %u\n", ret, expected_ret);

    /* invalid debug service */
    debug_service_exceptions = 0;
    ret = func(6);
    ok(debug_service_exceptions == expected_exc,
       "invalid debug service generated %u exceptions, expected %u\n",
       debug_service_exceptions, expected_exc);
    ok(ret == expected_ret,
      "invalid debug service returned %u, expected %u\n", ret, expected_ret);

    expected_exc = (is_wow64 ? numexc : 0);
    expected_ret = (is_wow64 && numexc);

    /* BREAKPOINT_PRINT */
    debug_service_exceptions = 0;
    ret = func(1);
    ok(debug_service_exceptions == expected_exc,
       "BREAKPOINT_PRINT generated %u exceptions, expected %u\n",
       debug_service_exceptions, expected_exc);
    ok(ret == expected_ret,
       "BREAKPOINT_PRINT returned %u, expected %u\n", ret, expected_ret);

    /* BREAKPOINT_LOAD_SYMBOLS */
    debug_service_exceptions = 0;
    ret = func(3);
    ok(debug_service_exceptions == expected_exc,
       "BREAKPOINT_LOAD_SYMBOLS generated %u exceptions, expected %u\n",
       debug_service_exceptions, expected_exc);
    ok(ret == expected_ret,
       "BREAKPOINT_LOAD_SYMBOLS returned %u, expected %u\n", ret, expected_ret);

    /* BREAKPOINT_UNLOAD_SYMBOLS */
    debug_service_exceptions = 0;
    ret = func(4);
    ok(debug_service_exceptions == expected_exc,
       "BREAKPOINT_UNLOAD_SYMBOLS generated %u exceptions, expected %u\n",
       debug_service_exceptions, expected_exc);
    ok(ret == expected_ret,
       "BREAKPOINT_UNLOAD_SYMBOLS returned %u, expected %u\n", ret, expected_ret);

    /* BREAKPOINT_COMMAND_STRING */
    debug_service_exceptions = 0;
    ret = func(5);
    ok(debug_service_exceptions == expected_exc || broken(debug_service_exceptions == numexc),
       "BREAKPOINT_COMMAND_STRING generated %u exceptions, expected %u\n",
       debug_service_exceptions, expected_exc);
    ok(ret == expected_ret || broken(ret == (numexc != 0)),
       "BREAKPOINT_COMMAND_STRING returned %u, expected %u\n", ret, expected_ret);

    pRtlRemoveVectoredExceptionHandler(vectored_handler);
}
#endif /* defined(__i386__) || defined(__x86_64__) */

#if defined(__i386__) || defined(__x86_64__) || defined(__aarch64__)

static DWORD breakpoint_exceptions;

static LONG CALLBACK breakpoint_handler(EXCEPTION_POINTERS *ExceptionInfo)
{
    EXCEPTION_RECORD *rec = ExceptionInfo->ExceptionRecord;
    trace("vect. handler %08x addr:%p\n", rec->ExceptionCode, rec->ExceptionAddress);

    ok(rec->ExceptionCode == EXCEPTION_BREAKPOINT, "ExceptionCode is %08x instead of %08x\n",
       rec->ExceptionCode, EXCEPTION_BREAKPOINT);

#ifdef __i386__
    ok(ExceptionInfo->ContextRecord->Eip == (DWORD)code_mem + 1,
       "expected Eip = %x, got %x\n", (DWORD)code_mem + 1, ExceptionInfo->ContextRecord->Eip);
    ok(rec->NumberParameters == (is_wow64 ? 1 : 3),
       "ExceptionParameters is %d instead of %d\n", rec->NumberParameters, is_wow64 ? 1 : 3);
    ok(rec->ExceptionInformation[0] == 0,
       "got ExceptionInformation[0] = %lx\n", rec->ExceptionInformation[0]);
    ExceptionInfo->ContextRecord->Eip = (DWORD)code_mem + 2;
#elif defined(__x86_64__)
    ok(ExceptionInfo->ContextRecord->Rip == (DWORD_PTR)code_mem + 1,
       "expected Rip = %lx, got %lx\n", (DWORD_PTR)code_mem + 1, ExceptionInfo->ContextRecord->Rip);
    ok(rec->NumberParameters == 1,
       "ExceptionParameters is %d instead of 1\n", rec->NumberParameters);
    ok(rec->ExceptionInformation[0] == 0,
       "got ExceptionInformation[0] = %lx\n", rec->ExceptionInformation[0]);
    ExceptionInfo->ContextRecord->Rip = (DWORD_PTR)code_mem + 2;
#elif defined(__aarch64__)
    ok(ExceptionInfo->ContextRecord->Pc == (DWORD_PTR)code_mem,
       "expected Pc = %p, got %p\n", code_mem, (void *)ExceptionInfo->ContextRecord->Pc);
    ok(rec->ExceptionAddress == code_mem,
       "expected address %p, got %p\n", code_mem, rec->ExceptionAddress);
    ok(rec->NumberParameters == 1,
       "ExceptionParameters is %d instead of 1\n", rec->NumberParameters);
    ok(rec->ExceptionInformation[0] == 0,
       "got ExceptionInformation[0] = %lx\n", rec->ExceptionInformation[0]);
    ExceptionInfo->ContextRecord->Pc += 4;
#endif

    breakpoint_exceptions++;
    return (rec->ExceptionCode == EXCEPTION_BREAKPOINT) ? EXCEPTION_CONTINUE_EXECUTION : EXCEPTION_CONTINUE_SEARCH;
}

#if defined(__i386__) || defined(__x86_64__)
static const BYTE breakpoint_code[] = {
    0xcd, 0x03,                   /* int $0x3 */
    0xc3,                         /* ret */
};
#elif defined(__aarch64__)
static const DWORD breakpoint_code[] = { 0xd43e0000, 0xd65f03c0 };  /* brk #0xf000; ret */
#endif

static void test_breakpoint(DWORD numexc)
{
    DWORD (CDECL *func)(void) = code_mem;
    void *vectored_handler;

    memcpy(code_mem, breakpoint_code, sizeof(breakpoint_code));

    vectored_handler = pRtlAddVectoredExceptionHandler(TRUE, &breakpoint_handler);
    ok(vectored_handler != 0, "RtlAddVectoredExceptionHandler failed\n");

    breakpoint_exceptions = 0;
    func();
    ok(breakpoint_exceptions == numexc, "int $0x3 generated %u exceptions, expected %u\n",
       breakpoint_exceptions, numexc);

    pRtlRemoveVectoredExceptionHandler(vectored_handler);
}

static DWORD invalid_handle_exceptions;

static LONG CALLBACK invalid_handle_vectored_handler(EXCEPTION_POINTERS *ExceptionInfo)
{
    PEXCEPTION_RECORD rec = ExceptionInfo->ExceptionRecord;
    trace("vect. handler %08x addr:%p\n", rec->ExceptionCode, rec->ExceptionAddress);

    ok(rec->ExceptionCode == EXCEPTION_INVALID_HANDLE, "ExceptionCode is %08x instead of %08x\n",
       rec->ExceptionCode, EXCEPTION_INVALID_HANDLE);
    ok(rec->NumberParameters == 0, "ExceptionParameters is %d instead of 0\n", rec->NumberParameters);

    invalid_handle_exceptions++;
    return (rec->ExceptionCode == EXCEPTION_INVALID_HANDLE) ? EXCEPTION_CONTINUE_EXECUTION : EXCEPTION_CONTINUE_SEARCH;
}

static void test_closehandle(DWORD numexc)
{
    PVOID vectored_handler;
    NTSTATUS status;
    DWORD res;

    if (!pRtlAddVectoredExceptionHandler || !pRtlRemoveVectoredExceptionHandler || !pRtlRaiseException)
    {
        skip("RtlAddVectoredExceptionHandler or RtlRemoveVectoredExceptionHandler or RtlRaiseException not found\n");
        return;
    }

    vectored_handler = pRtlAddVectoredExceptionHandler(TRUE, &invalid_handle_vectored_handler);
    ok(vectored_handler != 0, "RtlAddVectoredExceptionHandler failed\n");

    invalid_handle_exceptions = 0;
    res = CloseHandle((HANDLE)0xdeadbeef);
    ok(!res, "CloseHandle(0xdeadbeef) unexpectedly succeeded\n");
    ok(GetLastError() == ERROR_INVALID_HANDLE, "wrong error code %d instead of %d\n",
       GetLastError(), ERROR_INVALID_HANDLE);
    ok(invalid_handle_exceptions == numexc, "CloseHandle generated %d exceptions, expected %d\n",
       invalid_handle_exceptions, numexc);

    invalid_handle_exceptions = 0;
    status = pNtClose((HANDLE)0xdeadbeef);
    ok(status == STATUS_INVALID_HANDLE, "NtClose(0xdeadbeef) returned status %08x\n", status);
    ok(invalid_handle_exceptions == numexc, "NtClose generated %d exceptions, expected %d\n",
       invalid_handle_exceptions, numexc);

    pRtlRemoveVectoredExceptionHandler(vectored_handler);
}

static void test_vectored_continue_handler(void)
{
    PVOID handler1, handler2;
    ULONG ret;

    if (!pRtlAddVectoredContinueHandler || !pRtlRemoveVectoredContinueHandler)
    {
        skip("RtlAddVectoredContinueHandler or RtlRemoveVectoredContinueHandler not found\n");
        return;
    }

    handler1 = pRtlAddVectoredContinueHandler(TRUE, (void *)0xdeadbeef);
    ok(handler1 != 0, "RtlAddVectoredContinueHandler failed\n");

    handler2 = pRtlAddVectoredContinueHandler(TRUE, (void *)0xdeadbeef);
    ok(handler2 != 0, "RtlAddVectoredContinueHandler failed\n");
    ok(handler1 != handler2, "RtlAddVectoredContinueHandler returned same handler\n");

    if (pRtlRemoveVectoredExceptionHandler)
    {
        ret = pRtlRemoveVectoredExceptionHandler(handler1);
        ok(!ret, "RtlRemoveVectoredExceptionHandler succeeded\n");
    }

    ret = pRtlRemoveVectoredContinueHandler(handler1);
    ok(ret, "RtlRemoveVectoredContinueHandler failed\n");

    ret = pRtlRemoveVectoredContinueHandler(handler2);
    ok(ret, "RtlRemoveVectoredContinueHandler failed\n");

    ret = pRtlRemoveVectoredContinueHandler(handler1);
    ok(!ret, "RtlRemoveVectoredContinueHandler succeeded\n");

    ret = pRtlRemoveVectoredContinueHandler((void *)0x11223344);
    ok(!ret, "RtlRemoveVectoredContinueHandler succeeded\n");
}
#endif /* defined(__i386__) || defined(__x86_64__) || defined(__aarch64__) */

START_TEST(exception)
{
    HMODULE hntdll = GetModuleHandleA("ntdll.dll");
#if defined(__x86_64__)
    HMODULE hmsvcrt = LoadLibraryA("msvcrt.dll");
#endif

#if defined(__REACTOS__) && !defined(_M_AMD64)
    if (!winetest_interactive &&
        !strcmp(winetest_platform, "windows"))
    {
        skip("ROSTESTS-240: Skipping ntdll_winetest:exception because it hangs on WHS-Testbot. Set winetest_interactive to run it anyway.\n");
        return;
    }
#endif
    code_mem = VirtualAlloc(NULL, 65536, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    if(!code_mem) {
        trace("VirtualAlloc failed\n");
        return;
    }

    pNtGetContextThread  = (void *)GetProcAddress( hntdll, "NtGetContextThread" );
    pNtSetContextThread  = (void *)GetProcAddress( hntdll, "NtSetContextThread" );
    pNtReadVirtualMemory = (void *)GetProcAddress( hntdll, "NtReadVirtualMemory" );
    pNtClose             = (void *)GetProcAddress( hntdll, "NtClose" );
    pRtlUnwind           = (void *)GetProcAddress( hntdll, "RtlUnwind" );
    pRtlRaiseException   = (void *)GetProcAddress( hntdll, "RtlRaiseException" );
    pRtlCaptureContext   = (void *)GetProcAddress( hntdll, "RtlCaptureContext" );
    pNtTerminateProcess  = (void *)GetProcAddress( hntdll, "NtTerminateProcess" );
    pRtlAddVectoredExceptionHandler    = (void *)GetProcAddress( hntdll,
                                                                 "RtlAddVectoredExceptionHandler" );
    pRtlRemoveVectoredExceptionHandler = (void *)GetProcAddress( hntdll,
                                                                 "RtlRemoveVectoredExceptionHandler" );
    pRtlAddVectoredContinueHandler     = (void *)GetProcAddress( hntdll,
                                                                 "RtlAddVectoredContinueHandler" );
    pRtlRemoveVectoredContinueHandler  = (void *)GetProcAddress( hntdll,
                                                                 "RtlRemoveVectoredContinueHandler" );
    pNtQueryInformationProcess         = (void*)GetProcAddress( hntdll,
                                                                 "NtQueryInformationProcess" );
    pNtSetInformationProcess           = (void*)GetProcAddress( hntdll,
                                                                 "NtSetInformationProcess" );
    pIsWow64Process = (void *)GetProcAddress(GetModuleHandleA("kernel32.dll"), "IsWow64Process");

#ifdef __i386__
    if (!pIsWow64Process || !pIsWow64Process( GetCurrentProcess(), &is_wow64 )) is_wow64 = FALSE;

    if (pRtlAddVectoredExceptionHandler && pRtlRemoveVectoredExceptionHandler)
        have_vectored_api = TRUE;
    else
        skip("RtlAddVectoredExceptionHandler or RtlRemoveVectoredExceptionHandler not found\n");

    my_argc = winetest_get_mainargs( &my_argv );
    if (my_argc >= 4)
    {
        void *addr;
        sscanf( my_argv[3], "%p", &addr );

        if (addr != &test_stage)
        {
            skip( "child process not mapped at same address (%p/%p)\n", &test_stage, addr);
            return;
        }

        /* child must be run under a debugger */
        if (!NtCurrentTeb()->Peb->BeingDebugged)
        {
            ok(FALSE, "child process not being debugged?\n");
            return;
        }

        if (pRtlRaiseException)
        {
            test_stage = 1;
            run_rtlraiseexception_test(0x12345);
            run_rtlraiseexception_test(EXCEPTION_BREAKPOINT);
            run_rtlraiseexception_test(EXCEPTION_INVALID_HANDLE);
            test_stage = 2;
            run_rtlraiseexception_test(0x12345);
            run_rtlraiseexception_test(EXCEPTION_BREAKPOINT);
            run_rtlraiseexception_test(EXCEPTION_INVALID_HANDLE);
            test_stage = 3;
            test_outputdebugstring(0);
            test_stage = 4;
            test_outputdebugstring(2);
            test_stage = 5;
            test_ripevent(0);
            test_stage = 6;
            test_ripevent(1);
            test_stage = 7;
            test_debug_service(0);
            test_stage = 8;
            test_debug_service(1);
            test_stage = 9;
            test_breakpoint(0);
            test_stage = 10;
            test_breakpoint(1);
            test_stage = 11;
            test_closehandle(0);
            test_stage = 12;
            test_closehandle(1);
        }
        else
            skip( "RtlRaiseException not found\n" );

        /* rest of tests only run in parent */
        return;
    }

    test_unwind();
    test_exceptions();
    test_rtlraiseexception();
    test_debug_registers();
    test_outputdebugstring(1);
    test_ripevent(1);
    test_debug_service(1);
    test_breakpoint(1);
    test_closehandle(0);
    test_vectored_continue_handler();
    test_debugger();
    test_simd_exceptions();
    test_fpu_exceptions();
    test_dpe_exceptions();
    test_prot_fault();
    test_thread_context();

#elif defined(__x86_64__)
    pRtlAddFunctionTable               = (void *)GetProcAddress( hntdll,
                                                                 "RtlAddFunctionTable" );
    pRtlDeleteFunctionTable            = (void *)GetProcAddress( hntdll,
                                                                 "RtlDeleteFunctionTable" );
    pRtlInstallFunctionTableCallback   = (void *)GetProcAddress( hntdll,
                                                                 "RtlInstallFunctionTableCallback" );
    pRtlLookupFunctionEntry            = (void *)GetProcAddress( hntdll,
                                                                 "RtlLookupFunctionEntry" );
    p__C_specific_handler              = (void *)GetProcAddress( hntdll,
                                                                 "__C_specific_handler" );
    pRtlCaptureContext                 = (void *)GetProcAddress( hntdll,
                                                                 "RtlCaptureContext" );
    pRtlRestoreContext                 = (void *)GetProcAddress( hntdll,
                                                                 "RtlRestoreContext" );
    pRtlUnwindEx                       = (void *)GetProcAddress( hntdll,
                                                                 "RtlUnwindEx" );
    p_setjmp                           = (void *)GetProcAddress( hmsvcrt,
                                                                 "_setjmp" );

    test_debug_registers();
    test_outputdebugstring(1);
    test_ripevent(1);
    test_debug_service(1);
    test_breakpoint(1);
    test_closehandle(0);
    test_vectored_continue_handler();
    test_virtual_unwind();
    test___C_specific_handler();
    test_restore_context();

    if (pRtlAddFunctionTable && pRtlDeleteFunctionTable && pRtlInstallFunctionTableCallback && pRtlLookupFunctionEntry)
      test_dynamic_unwind();
    else
      skip( "Dynamic unwind functions not found\n" );

#elif defined(__aarch64__)
    pRtlAddFunctionTable    = (void *)GetProcAddress( hntdll, "RtlAddFunctionTable" );
    pRtlDeleteFunctionTable = (void *)GetProcAddress( hntdll, "RtlDeleteFunctionTable" );
    pRtlRestoreContext      = (void *)GetProcAddress( hntdll, "RtlRestoreContext" );
    pRtlUnwindEx            = (void *)GetProcAddress( hntdll, "RtlUnwindEx" );

    test_thread_context();
    test_debug_service(1);
    test_brk();
    test_breakpoint(1);
    test_closehandle(0);
    test_vectored_continue_handler();
    test_nested_exception();
    test_collided_unwind();
    test_restore_context();
    test_mrs_currentel();

#ifndef __REACTOS__
    test_continue();
    test_debugger(DBG_EXCEPTION_HANDLED, FALSE);
    test_debugger(DBG_CONTINUE, FALSE);
    test_debugger(DBG_EXCEPTION_HANDLED, TRUE);
    test_debugger(DBG_CONTINUE, TRUE);
    test_KiUserExceptionDispatcher();
    test_KiUserApcDispatcher();
    test_KiUserCallbackDispatcher();
    test_rtlraiseexception();
#else
    skip("ARM64 debugger/dispatcher/APC tests not yet supported on ReactOS\n");
#endif

#endif

    VirtualFree(code_mem, 0, MEM_RELEASE);
}
