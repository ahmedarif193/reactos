/*
 * msvcrt C++ exception handling
 *
 * Copyright 2011 Alexandre Julliard
 * Copyright 2013 André Hentschel
 * Copyright 2017 Martin Storsjo
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

#ifdef __arm64ec__

#include <setjmp.h>
#include <stdarg.h>
#include "include/fpieee.h"

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winbase.h"
#include "winternl.h"
#include "rtlsupportapi.h"
#include "msvcrt.h"
#include "excpt.h"
#include "wine/debug.h"

#include "cppexcept.h"

WINE_DEFAULT_DEBUG_CHANNEL(seh);

static void * __attribute__((naked,used)) call_handler_arm64( void *func, uintptr_t frame,
                                                                  UINT flags, BYTE *nonvol_regs )
{
    asm( ".seh_proc \"#call_handler_arm64\"\n\t"
         "stp x29, x30, [sp, #-80]!\n\t"
         ".seh_save_fplr_x 80\n\t"
         "stp x19, x20, [sp, #16]\n\t"
         ".seh_save_regp x19, 16\n\t"
         "stp x21, x22, [sp, #32]\n\t"
         ".seh_save_regp x21, 32\n\t"
         "stp x25, x26, [sp, #48]\n\t"
         ".seh_save_regp x25, 48\n\t"
         "str x27, [sp, #64]\n\t"
         ".seh_save_reg x27, 64\n\t"
         "str x1, [sp, #-16]!\n\t"
         ".seh_stackalloc 16\n\t"
         ".seh_endprologue\n\t"
         "ldp x19, x20, [x3, #0]\n\t" /* nonvolatile regs */
         "ldp x21, x22, [x3, #16]\n\t"
         "ldp x25, x26, [x3, #48]\n\t"
         "ldr x27, [x3, #64]\n\t"
         "ldr x29,  [x3, #80]\n\t"
         "blr x0\n\t"
         "add sp, sp, 16\n\t"
         "ldp x19, x20, [sp, #16]\n\t"
         "ldp x21, x22, [sp, #32]\n\t"
         "ldp x25, x26, [sp, #48]\n\t"
         "ldr x27,      [sp, #64]\n\t"
         "ldp x29, x30, [sp], #80\n\t"
         "ret\n\t"
         ".seh_endproc" );
}

static void * __attribute__((naked,used)) call_handler_x64( void *func, uintptr_t frame, UINT flags )
{
    asm( ".seh_proc \"#call_handler_x64\"\n\t"
         "stp x29, x30, [sp, #-16]!\n\t"
         ".seh_save_fplr_x 16\n\t"
         ".seh_endprologue\n\t"
         "mov x11, x0\n\t"
         "adr x10, $iexit_thunk$cdecl$i8$i8i8i8\n\t"
         "adrp x16, __os_arm64x_dispatch_icall\n\t"
         "ldr x16, [x16, #:lo12:__os_arm64x_dispatch_icall]\n\t"
         "blr x16\n\t"
         "blr x11\n\t"
         "ldp x29, x30, [sp], #16\n\t"
         "ret\n\t"
         ".seh_endproc" );
}


/*******************************************************************
 *		call_catch_handler
 */
void *call_catch_handler( EXCEPTION_RECORD *rec )
{
    ULONG_PTR frame = rec->ExceptionInformation[1];
    void *handler = (void *)rec->ExceptionInformation[5];
    BOOLEAN is_ec_handler;
    static int log_count;

    TRACE( "calling %p frame %Ix\n", handler, frame );
    is_ec_handler = RtlIsEcCode( (ULONG_PTR)handler );
    if (log_count < 16)
    {
        log_count++;
        DbgPrint("MSVCRT ARM64EC catch handler=%p ec=%u rec=%p code=%08lx frame=%p params=%lu\n",
                 handler, is_ec_handler, rec, rec->ExceptionCode,
                 (void *)frame, rec->NumberParameters);
    }
    if (!is_ec_handler) return call_handler_x64( handler, frame, 0x100 );
    return call_handler_arm64( handler, frame, 0x100, (BYTE *)rec->ExceptionInformation[10] );
}


/*******************************************************************
 *		call_unwind_handler
 */
void *call_unwind_handler( void *handler, ULONG_PTR frame, DISPATCHER_CONTEXT *dispatch )
{
    BOOLEAN is_ec_handler;
    static int log_count;

    TRACE( "calling %p frame %Ix\n", handler, frame );
    is_ec_handler = RtlIsEcCode( (ULONG_PTR)handler );
    if (log_count < 16)
    {
        log_count++;
        DbgPrint("MSVCRT ARM64EC unwind handler=%p ec=%u frame=%p pc=%p base=%p scope=%lu flags=%08lx data=%p\n",
                 handler, is_ec_handler, (void *)frame,
                 (void *)dispatch->ControlPc, (void *)dispatch->ImageBase,
                 dispatch->ScopeIndex, dispatch->ContextRecord->ContextFlags,
                 dispatch->HandlerData);
    }
    if (!is_ec_handler) return call_handler_x64( handler, frame, 0x100 );
    return call_handler_arm64( handler, frame, 0x100,
                               ((DISPATCHER_CONTEXT_ARM64EC *)dispatch)->NonVolatileRegisters );
}


/*******************************************************************
 *		get_exception_pc
 */
ULONG_PTR get_exception_pc( DISPATCHER_CONTEXT *dispatch )
{
    ULONG_PTR pc = dispatch->ControlPc;
    if (RtlIsEcCode( pc ) && ((DISPATCHER_CONTEXT_ARM64EC *)dispatch)->ControlPcIsUnwound) pc -= 4;
    return pc;
}


/*********************************************************************
 *              handle_fpieee_flt
 */
int handle_fpieee_flt( __msvcrt_ulong exception_code, EXCEPTION_POINTERS *ep,
                       int (__cdecl *handler)(_FPIEEE_RECORD*) )
{
    FIXME("(%lx %p %p)\n", exception_code, ep, handler);
    return EXCEPTION_CONTINUE_SEARCH;
}

void __cdecl _local_unwind( void *frame, void *target )
{
    RtlUnwind( frame, target, NULL, 0 );
}

LONG __attribute__((naked)) __C_ExecuteExceptionFilter( EXCEPTION_POINTERS *ptrs, void *frame,
    PEXCEPTION_FILTER filter, BYTE *nonvolatile )
{
    asm( ".seh_proc _C_ExecuteExceptionFilter\n\t"
         "stp x29, x30, [sp, #-80]!\n\t"
         ".seh_save_fplr_x 80\n\t"
         "stp x19, x20, [sp, #16]\n\t"
         ".seh_save_regp x19, 16\n\t"
         "stp x21, x22, [sp, #32]\n\t"
         ".seh_save_regp x21, 32\n\t"
         "stp x25, x26, [sp, #48]\n\t"
         ".seh_save_regp x25, 48\n\t"
         "str x27, [sp, #64]\n\t"
         ".seh_save_reg x27, 64\n\t"
         ".seh_endprologue\n\t"
         "ldp x19, x20, [x3, #0]\n\t" /* nonvolatile regs */
         "ldp x21, x22, [x3, #16]\n\t"
         "ldp x25, x26, [x3, #48]\n\t"
         "ldr x27, [x3, #64]\n\t"
         "ldr x1,  [x3, #80]\n\t" /* x29 = frame */
         "blr x2\n\t"             /* filter */
         "ldp x19, x20, [sp, #16]\n\t"
         "ldp x21, x22, [sp, #32]\n\t"
         "ldp x25, x26, [sp, #48]\n\t"
         "ldr x27,      [sp, #64]\n\t"
         "ldp x29, x30, [sp], #80\n\t"
         "ret\n\t"
         ".seh_endproc" );
}

static EXCEPTION_DISPOSITION __cdecl __C_specific_handler_arm64( EXCEPTION_RECORD *rec, void *frame,
                                                                 ARM64_NT_CONTEXT *context,
                                                                 DISPATCHER_CONTEXT_ARM64 *dispatch )
{
    const SCOPE_TABLE *table = dispatch->HandlerData;
    ULONG_PTR base = dispatch->ImageBase;
    ULONG_PTR pc = dispatch->ControlPc;
    unsigned int i;
    void *handler;

    TRACE( "%p %p %p %p pc %Ix\n", rec, frame, context, dispatch, pc );

    if (dispatch->ControlPcIsUnwound) pc -= 4;

    if (rec->ExceptionFlags & (EXCEPTION_UNWINDING | EXCEPTION_EXIT_UNWIND))
    {
        for (i = dispatch->ScopeIndex; i < table->Count; i++)
        {
            if (pc < base + table->ScopeRecord[i].BeginAddress) continue;
            if (pc >= base + table->ScopeRecord[i].EndAddress) continue;
            if (table->ScopeRecord[i].JumpTarget) continue;

            if (rec->ExceptionFlags & EXCEPTION_TARGET_UNWIND &&
                dispatch->TargetPc >= base + table->ScopeRecord[i].BeginAddress &&
                dispatch->TargetPc < base + table->ScopeRecord[i].EndAddress)
                break;

            handler = (void *)(base + table->ScopeRecord[i].HandlerAddress);
            dispatch->ScopeIndex = i + 1;
            TRACE( "scope %u calling __finally %p frame %p\n", i, handler, frame );
            __C_ExecuteExceptionFilter( ULongToPtr(TRUE), frame, handler, dispatch->NonVolatileRegisters );
        }
    }
    else
    {
        for (i = dispatch->ScopeIndex; i < table->Count; i++)
        {
            if (pc < base + table->ScopeRecord[i].BeginAddress) continue;
            if (pc >= base + table->ScopeRecord[i].EndAddress) continue;
            if (!table->ScopeRecord[i].JumpTarget) continue;

            if (table->ScopeRecord[i].HandlerAddress != EXCEPTION_EXECUTE_HANDLER)
            {
                EXCEPTION_POINTERS ptrs = { rec, (CONTEXT *)context };

                handler = (void *)(base + table->ScopeRecord[i].HandlerAddress);
                TRACE( "scope %u calling filter %p ptrs %p frame %p\n", i, handler, &ptrs, frame );
                switch (__C_ExecuteExceptionFilter( &ptrs, frame, handler, dispatch->NonVolatileRegisters ))
                {
                case EXCEPTION_EXECUTE_HANDLER:
                    break;
                case EXCEPTION_CONTINUE_SEARCH:
                    continue;
                case EXCEPTION_CONTINUE_EXECUTION:
                    return ExceptionContinueExecution;
                }
            }

            TRACE( "unwinding to target %Ix\n", base + table->ScopeRecord[i].JumpTarget );
            RtlUnwindEx( frame, (char *)base + table->ScopeRecord[i].JumpTarget,
                         rec, ULongToPtr(rec->ExceptionCode), (CONTEXT *)dispatch->ContextRecord,
                         dispatch->HistoryTable );
        }
    }

    return ExceptionContinueSearch;
}

EXCEPTION_DISPOSITION __cdecl __C_specific_handler( EXCEPTION_RECORD *rec, void *frame,
                                                    CONTEXT *context, DISPATCHER_CONTEXT *dispatch )
{
    const SCOPE_TABLE *table = dispatch->HandlerData;
    ULONG_PTR base = dispatch->ImageBase;
    ULONG_PTR pc = dispatch->ControlPc;
    unsigned int i;

    if (RtlIsEcCode( pc ))
        return __C_specific_handler_arm64( rec, frame, (ARM64_NT_CONTEXT *)context,
                                           (DISPATCHER_CONTEXT_ARM64 *)dispatch );

    TRACE( "%p %p %p %p pc %Ix\n", rec, frame, context, dispatch, pc );

    if (rec->ExceptionFlags & (EXCEPTION_UNWINDING | EXCEPTION_EXIT_UNWIND))
    {
        for (i = dispatch->ScopeIndex; i < table->Count; i++)
        {
            if (pc < base + table->ScopeRecord[i].BeginAddress) continue;
            if (pc >= base + table->ScopeRecord[i].EndAddress) continue;
            if (table->ScopeRecord[i].JumpTarget) continue;

            if (rec->ExceptionFlags & EXCEPTION_TARGET_UNWIND &&
                dispatch->TargetIp >= base + table->ScopeRecord[i].BeginAddress &&
                dispatch->TargetIp < base + table->ScopeRecord[i].EndAddress)
                break;

            PTERMINATION_HANDLER handler = (void *)(base + table->ScopeRecord[i].HandlerAddress);
            dispatch->ScopeIndex = i + 1;
            TRACE( "scope %u calling __finally %p frame %p\n", i, handler, frame );
            handler( TRUE, frame );
        }
    }
    else
    {
        for (i = dispatch->ScopeIndex; i < table->Count; i++)
        {
            if (pc < base + table->ScopeRecord[i].BeginAddress) continue;
            if (pc >= base + table->ScopeRecord[i].EndAddress) continue;
            if (!table->ScopeRecord[i].JumpTarget) continue;

            if (table->ScopeRecord[i].HandlerAddress != EXCEPTION_EXECUTE_HANDLER)
            {
                EXCEPTION_POINTERS ptrs = { rec, context };
                PEXCEPTION_FILTER filter = (void *)(base + table->ScopeRecord[i].HandlerAddress);

                TRACE( "scope %u calling filter %p ptrs %p frame %p\n", i, filter, &ptrs, frame );
                switch (filter( &ptrs, frame ))
                {
                case EXCEPTION_EXECUTE_HANDLER:
                    break;
                case EXCEPTION_CONTINUE_SEARCH:
                    continue;
                case EXCEPTION_CONTINUE_EXECUTION:
                    return ExceptionContinueExecution;
                }
            }

            TRACE( "unwinding to target %Ix\n", base + table->ScopeRecord[i].JumpTarget );
            RtlUnwindEx( frame, (char *)base + table->ScopeRecord[i].JumpTarget,
                         rec, ULongToPtr(rec->ExceptionCode), dispatch->ContextRecord,
                         dispatch->HistoryTable );
        }
    }

    return ExceptionContinueSearch;
}

#if _MSVCR_VER>=110 && _MSVCR_VER<=120
/*********************************************************************
 *  __crtCapturePreviousContext (MSVCR110.@)
 */
void __cdecl __crtCapturePreviousContext( CONTEXT *ctx )
{
    UNWIND_HISTORY_TABLE table;
    RUNTIME_FUNCTION *func;
    PEXCEPTION_ROUTINE handler;
    ULONG_PTR pc, frame, base;
    void *data;
    ULONG i;

    RtlCaptureContext( ctx );
    for (i = 0; i < 2; i++)
    {
        pc = ctx->Rip;
        if ((ctx->ContextFlags & CONTEXT_UNWOUND_TO_CALL) && RtlIsEcCode( pc )) pc -= 4;
        if (!(func = RtlLookupFunctionEntry( pc, &base, &table ))) break;
        if (RtlVirtualUnwind2( UNW_FLAG_NHANDLER, base, pc, func, ctx, NULL,
                               &data, &frame, NULL, NULL, NULL, &handler, 0 ))
            break;
        if (!ctx->Rip) break;
        if (!frame) break;
    }
}
#endif

#endif  /* __arm64ec__ */
