/*
 * File cpu_arm64.c
 *
 * Copyright (C) 2009 Eric Pouech
 * Copyright (C) 2010-2013 André Hentschel
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

#include <assert.h>

#define NONAMELESSUNION
#define NONAMELESSSTRUCT
#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "dbghelp_private.h"
#include "winternl.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(dbghelp);

static BOOL arm64_get_addr(HANDLE hThread, const CONTEXT* ctx,
                           enum cpu_addr ca, ADDRESS64* addr)
{
    addr->Mode    = AddrModeFlat;
    addr->Segment = 0; /* don't need segment */
    switch (ca)
    {
#ifdef __aarch64__
    case cpu_addr_pc:    addr->Offset = ctx->Pc;  return TRUE;
    case cpu_addr_stack: addr->Offset = ctx->Sp;  return TRUE;
    case cpu_addr_frame: addr->Offset = ctx->u.s.Fp; return TRUE;
#endif
    default: addr->Mode = -1;
        return FALSE;
    }
}

#ifdef __aarch64__
enum st_mode {stm_start, stm_arm64, stm_done};

/* indexes in Reserved array */
#define __CurrentModeCount      0

#define curr_mode   (frame->Reserved[__CurrentModeCount] & 0x0F)
#define curr_count  (frame->Reserved[__CurrentModeCount] >> 4)

#define set_curr_mode(m) {frame->Reserved[__CurrentModeCount] &= ~0x0F; frame->Reserved[__CurrentModeCount] |= (m & 0x0F);}
#define inc_curr_count() (frame->Reserved[__CurrentModeCount] += 0x10)

#ifdef __REACTOS__

/* RtlVirtualUnwind only reads local addresses, while StackWalk can operate on a
 * different process. Keep all image and stack accesses behind the callbacks;
 * opcode semantics mirror sdk/lib/rtl/arm64/unwind.c. */

#define ARM64_UNWIND_FLAG_MASK                0x3
#define ARM64_PACKED_FUNCTION_LENGTH_SHIFT    2
#define ARM64_PACKED_FUNCTION_LENGTH_MASK     0x7ff
#define ARM64_PACKED_REG_F_SHIFT              13
#define ARM64_PACKED_REG_F_MASK               0x7
#define ARM64_PACKED_REG_I_SHIFT              16
#define ARM64_PACKED_REG_I_MASK               0xf
#define ARM64_PACKED_H_SHIFT                  20
#define ARM64_PACKED_CR_SHIFT                 21
#define ARM64_PACKED_CR_MASK                  0x3
#define ARM64_PACKED_FRAME_SIZE_SHIFT         23
#define ARM64_PACKED_FRAME_SIZE_MASK          0x1ff
#define ARM64_XDATA_FUNCTION_LENGTH_MASK      0x3ffff
#define ARM64_XDATA_VERSION_SHIFT             18
#define ARM64_XDATA_VERSION_MASK              0x3
#define ARM64_XDATA_EPILOG_IN_HEADER_SHIFT    21
#define ARM64_XDATA_EPILOG_COUNT_SHIFT        22
#define ARM64_XDATA_EPILOG_COUNT_MASK         0x1f
#define ARM64_XDATA_CODE_WORDS_SHIFT          27
#define ARM64_XDATA_CODE_WORDS_MASK           0x1f
#define ARM64_XDATA_EPILOG_OFFSET_MASK        0x3ffff
#define ARM64_XDATA_EPILOG_INDEX_SHIFT        22
#define ARM64_XDATA_EPILOG_INDEX_MASK         0x3ff
#define ARM64_XDATA_MAX_CODE_WORDS            0xff

struct arm64_runtime_function
{
    DWORD begin_address;
    DWORD unwind_data;
};

struct arm64_image_info
{
    DWORD64 base;
    DWORD size;
    IMAGE_DATA_DIRECTORY exception;
};

static BOOL arm64_image_range_valid(const struct arm64_image_info *image, DWORD rva, DWORD size)
{
    return rva <= image->size && size <= image->size - rva;
}

static BOOL arm64_read_image(struct cpu_stack_walk *csw, const struct arm64_image_info *image, DWORD rva, void *buffer, DWORD size)
{
    if (!arm64_image_range_valid(image, rva, size) || image->base > ~(DWORD64)0 - rva) return FALSE;
    return sw_read_mem(csw, image->base + rva, buffer, size);
}

static BOOL arm64_get_image_info(struct cpu_stack_walk *csw, DWORD64 pc, struct arm64_image_info *image)
{
    IMAGE_DOS_HEADER dos;
    IMAGE_NT_HEADERS64 nt;

    if (!pc || !(image->base = sw_module_base(csw, pc)) || pc < image->base ||
        pc - image->base > MAXDWORD ||
        !sw_read_mem(csw, image->base, &dos, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
        dos.e_lfanew < (LONG)sizeof(dos) || dos.e_lfanew > 0x100000 ||
        image->base > ~(DWORD64)0 - dos.e_lfanew ||
        !sw_read_mem(csw, image->base + dos.e_lfanew, &nt, sizeof(nt)) ||
        nt.Signature != IMAGE_NT_SIGNATURE ||
        nt.FileHeader.Machine != IMAGE_FILE_MACHINE_ARM64 ||
        nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt.OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_EXCEPTION)
    {
        return FALSE;
    }

    image->size = nt.OptionalHeader.SizeOfImage;
    image->exception = nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    return image->size && pc - image->base < image->size && image->exception.VirtualAddress &&
           image->exception.Size >= sizeof(struct arm64_runtime_function) &&
           arm64_image_range_valid(image, image->exception.VirtualAddress, image->exception.Size);
}

static BOOL arm64_function_length(struct cpu_stack_walk *csw, const struct arm64_image_info *image, const struct arm64_runtime_function *function, DWORD *length)
{
    DWORD flag = function->unwind_data & ARM64_UNWIND_FLAG_MASK;
    DWORD header;

    if (flag == 3) return FALSE;
    if (flag)
    {
        *length = ((function->unwind_data >> ARM64_PACKED_FUNCTION_LENGTH_SHIFT) &
                   ARM64_PACKED_FUNCTION_LENGTH_MASK) * sizeof(DWORD);
    }
    else
    {
        if (!function->unwind_data ||
            !arm64_read_image(csw, image, function->unwind_data, &header, sizeof(header))) return FALSE;
        *length = (header & ARM64_XDATA_FUNCTION_LENGTH_MASK) * sizeof(DWORD);
    }
    return *length && function->begin_address <= image->size &&
           *length <= image->size - function->begin_address;
}

static BOOL arm64_lookup_runtime_function(struct cpu_stack_walk *csw, DWORD64 pc, struct arm64_image_info *image, struct arm64_runtime_function *function, void **table_entry)
{
    DWORD count, low, high, middle, length, rva;
    RUNTIME_FUNCTION *provided;

    *table_entry = NULL;
    if (!arm64_get_image_info(csw, pc, image)) return FALSE;
    rva = pc - image->base;

    provided = sw_table_access(csw, pc);
    if (provided)
    {
        function->begin_address = provided->BeginAddress;
        function->unwind_data = provided->DUMMYUNIONNAME.UnwindData;
        if (arm64_function_length(csw, image, function, &length) &&
            rva >= function->begin_address && rva - function->begin_address < length)
        {
            *table_entry = provided;
            return TRUE;
        }
    }

    count = image->exception.Size / sizeof(*function);
    low = 0;
    high = count;
    while (low < high)
    {
        middle = low + (high - low) / 2;
        if (!arm64_read_image(csw, image,
                              image->exception.VirtualAddress + middle * sizeof(*function),
                              function, sizeof(*function)) ||
            !arm64_function_length(csw, image, function, &length)) return FALSE;
        if (rva < function->begin_address)
            high = middle;
        else if (rva - function->begin_address >= length)
            low = middle + 1;
        else
            return TRUE;
    }
    return FALSE;
}

static unsigned arm64_unwind_code_length(BYTE code)
{
    if (code < 0xc0) return 1;
    if (code < 0xe0) return 2;
    if (code == 0xe0) return 4;
    if (code == 0xe2) return 2;
    if (code == 0xe7) return 3;
    return 1;
}

static BOOL arm64_unwind_code_supported(BYTE code)
{
    return code < 0xdf || (code >= 0xe0 && code <= 0xe7) || code == 0xe9 ||
           code == 0xea || code == 0xec || code == 0xfc;
}

static BOOL arm64_sequence_length(const BYTE *ptr, const BYTE *end, unsigned *length)
{
    unsigned code_length;

    *length = 0;
    while (ptr < end)
    {
        if (!arm64_unwind_code_supported(*ptr)) return FALSE;
        if (*ptr == 0xe4 || *ptr == 0xe5) return TRUE;
        code_length = arm64_unwind_code_length(*ptr);
        if ((SIZE_T)(end - ptr) < code_length) return FALSE;
        if ((*ptr & 0xf8) != 0xe8) (*length)++;
        ptr += code_length;
    }
    return FALSE;
}

static BOOL arm64_add_sp(CONTEXT *context, DWORD64 size)
{
    if (context->Sp > ~(DWORD64)0 - size) return FALSE;
    context->Sp += size;
    return TRUE;
}

static BOOL arm64_read_stack(struct cpu_stack_walk *csw, const CONTEXT *context, DWORD64 offset, void *buffer, DWORD size)
{
    if (context->Sp > ~(DWORD64)0 - offset) return FALSE;
    return sw_read_mem(csw, context->Sp + offset, buffer, size);
}

static BOOL arm64_restore_regs(struct cpu_stack_walk *csw, CONTEXT *context, unsigned reg, unsigned count, int pos)
{
    unsigned offset = pos > 0 ? pos : 0;
    DWORD64 adjustment;

    if (!count || reg >= ARRAY_SIZE(context->u.X) || count > ARRAY_SIZE(context->u.X) - reg ||
        !arm64_read_stack(csw, context, (DWORD64)offset * sizeof(DWORD64), &context->u.X[reg], count * sizeof(DWORD64))) return FALSE;
    if (pos >= 0) return TRUE;
    adjustment = (DWORD64)(-(LONGLONG)pos) * sizeof(DWORD64);
    return arm64_add_sp(context, adjustment);
}

static BOOL arm64_restore_fpregs(struct cpu_stack_walk *csw, CONTEXT *context, unsigned reg, unsigned count, int pos)
{
    unsigned i, offset = pos > 0 ? pos : 0;
    DWORD64 value, adjustment;

    if (!count || reg >= ARRAY_SIZE(context->V) || count > ARRAY_SIZE(context->V) - reg) return FALSE;
    for (i = 0; i < count; i++)
    {
        if (!arm64_read_stack(csw, context, (DWORD64)(offset + i) * sizeof(DWORD64), &value, sizeof(value))) return FALSE;
        context->V[reg + i].s.Low = value;
    }
    if (pos >= 0) return TRUE;
    adjustment = (DWORD64)(-(LONGLONG)pos) * sizeof(DWORD64);
    return arm64_add_sp(context, adjustment);
}

static BOOL arm64_restore_qregs(struct cpu_stack_walk *csw, CONTEXT *context, unsigned reg, unsigned count, int pos)
{
    unsigned i, offset = pos > 0 ? pos : 0;
    DWORD64 value[2], adjustment;

    if (!count || reg >= ARRAY_SIZE(context->V) || count > ARRAY_SIZE(context->V) - reg) return FALSE;
    for (i = 0; i < count; i++)
    {
        if (!arm64_read_stack(csw, context, (DWORD64)(offset + i) * sizeof(value), value, sizeof(value))) return FALSE;
        context->V[reg + i].s.Low = value[0];
        context->V[reg + i].s.High = value[1];
    }
    if (pos >= 0) return TRUE;
    adjustment = (DWORD64)(-(LONGLONG)pos) * sizeof(value);
    return arm64_add_sp(context, adjustment);
}

static BOOL arm64_restore_any_reg(struct cpu_stack_walk *csw, CONTEXT *context, unsigned reg, unsigned count, unsigned type, int pos)
{
    if (reg & 0x80) return FALSE;
    if (reg & 0x20) pos = -pos - 1;

    switch (type)
    {
    case 0:
        if (count > 1 || pos < 0) pos *= 2;
        return arm64_restore_regs(csw, context, reg & 0x1f, count, pos);
    case 1:
        if (count > 1 || pos < 0) pos *= 2;
        return arm64_restore_fpregs(csw, context, reg & 0x1f, count, pos);
    case 2:
        return arm64_restore_qregs(csw, context, reg & 0x1f, count, pos);
    default:
        return FALSE;
    }
}

static void arm64_strip_pac(CONTEXT *context)
{
    register DWORD64 lr __asm__("x30") = context->u.s.Lr;

    __asm__("hint 0x7" : "+r"(lr));
    context->u.s.Lr = lr;
}

static BOOL arm64_process_unwind_codes(struct cpu_stack_walk *csw, const BYTE *ptr, const BYTE *end, CONTEXT *context, unsigned skip, BOOL *final_pc_from_lr)
{
    unsigned val, length, save_next = 2;
    DWORD64 frame[2];
    CONTEXT saved_context;
    DWORD flags;
    BYTE code;

    while (ptr < end && skip)
    {
        code = *ptr;
        if (!arm64_unwind_code_supported(code)) return FALSE;
        if (code == 0xe4 || code == 0xe5) break;
        length = arm64_unwind_code_length(code);
        if ((SIZE_T)(end - ptr) < length) return FALSE;
        ptr += length;
        if ((code & 0xf8) != 0xe8) skip--;
    }
    if (skip) return FALSE;

    while (ptr < end)
    {
        if (!arm64_unwind_code_supported(*ptr)) return FALSE;
        length = arm64_unwind_code_length(*ptr);
        if ((SIZE_T)(end - ptr) < length) return FALSE;
        val = length > 1 ? ptr[0] * 0x100 + ptr[1] : *ptr;

        if (*ptr < 0x20)
        {
            if (!arm64_add_sp(context, 16 * (val & 0x1f))) return FALSE;
        }
        else if (*ptr < 0x40)
        {
            if (!arm64_restore_regs(csw, context, 19, save_next, -(int)(val & 0x1f))) return FALSE;
        }
        else if (*ptr < 0x80)
        {
            if (!arm64_restore_regs(csw, context, 29, 2, val & 0x3f)) return FALSE;
        }
        else if (*ptr < 0xc0)
        {
            if (!arm64_restore_regs(csw, context, 29, 2, -(int)(val & 0x3f) - 1)) return FALSE;
        }
        else if (*ptr < 0xc8)
        {
            if (!arm64_add_sp(context, 16 * (val & 0x7ff))) return FALSE;
        }
        else if (*ptr < 0xcc)
        {
            if (!arm64_restore_regs(csw, context, 19 + ((val >> 6) & 0xf), save_next, val & 0x3f)) return FALSE;
        }
        else if (*ptr < 0xd0)
        {
            if (!arm64_restore_regs(csw, context, 19 + ((val >> 6) & 0xf), save_next, -(int)(val & 0x3f) - 1)) return FALSE;
        }
        else if (*ptr < 0xd4)
        {
            if (!arm64_restore_regs(csw, context, 19 + ((val >> 6) & 0xf), 1, val & 0x3f)) return FALSE;
        }
        else if (*ptr < 0xd6)
        {
            if (!arm64_restore_regs(csw, context, 19 + ((val >> 5) & 0xf), 1, -(int)(val & 0x1f) - 1)) return FALSE;
        }
        else if (*ptr < 0xd8)
        {
            if (!arm64_restore_regs(csw, context, 19 + 2 * ((val >> 6) & 0x7), 1, val & 0x3f) ||
                !arm64_restore_regs(csw, context, 30, 1, (val & 0x3f) + 1)) return FALSE;
        }
        else if (*ptr < 0xda)
        {
            if (!arm64_restore_fpregs(csw, context, 8 + ((val >> 6) & 0x7), save_next, val & 0x3f)) return FALSE;
        }
        else if (*ptr < 0xdc)
        {
            if (!arm64_restore_fpregs(csw, context, 8 + ((val >> 6) & 0x7), save_next, -(int)(val & 0x3f) - 1)) return FALSE;
        }
        else if (*ptr < 0xde)
        {
            if (!arm64_restore_fpregs(csw, context, 8 + ((val >> 6) & 0x7), 1, val & 0x3f)) return FALSE;
        }
        else if (*ptr == 0xde)
        {
            if (!arm64_restore_fpregs(csw, context, 8 + ((val >> 5) & 0x7), 1, -(int)(val & 0x3f) - 1)) return FALSE;
        }
        else if (*ptr == 0xe0)
        {
            if (!arm64_add_sp(context, 16 * ((ptr[1] << 16) + (ptr[2] << 8) + ptr[3]))) return FALSE;
        }
        else if (*ptr == 0xe1)
        {
            context->Sp = context->u.s.Fp;
        }
        else if (*ptr == 0xe2)
        {
            DWORD64 offset = 8 * (val & 0xff);

            if (context->u.s.Fp < offset) return FALSE;
            context->Sp = context->u.s.Fp - offset;
        }
        else if (*ptr == 0xe3)
        {
            /* nop */
        }
        else if (*ptr == 0xe4)
        {
            break;
        }
        else if (*ptr == 0xe5)
        {
            /* Continue with the parent fragment's phantom prolog. */
        }
        else if (*ptr == 0xe6)
        {
            if (save_next > 29) return FALSE;
            save_next += 2;
            ptr += length;
            continue;
        }
        else if (*ptr == 0xe7)
        {
            if (!arm64_restore_any_reg(csw, context, ptr[1], (ptr[1] & 0x40) ? save_next : 1, ptr[2] >> 6, ptr[2] & 0x3f)) return FALSE;
        }
        else if (*ptr == 0xe9)
        {
            if (!arm64_read_stack(csw, context, 0, frame, sizeof(frame))) return FALSE;
            context->Pc = frame[1];
            context->Sp = frame[0];
            context->ContextFlags &= ~CONTEXT_UNWOUND_TO_CALL;
            *final_pc_from_lr = FALSE;
        }
        else if (*ptr == 0xea)
        {
            flags = context->ContextFlags & ~CONTEXT_UNWOUND_TO_CALL;
            if (!arm64_read_stack(csw, context, 0, &saved_context, sizeof(saved_context))) return FALSE;
            *context = saved_context;
            context->ContextFlags = flags | (saved_context.ContextFlags & CONTEXT_UNWOUND_TO_CALL);
            *final_pc_from_lr = FALSE;
        }
        else if (*ptr == 0xec)
        {
            context->Pc = context->u.s.Lr;
            context->ContextFlags &= ~CONTEXT_UNWOUND_TO_CALL;
            *final_pc_from_lr = FALSE;
        }
        else if (*ptr == 0xfc)
        {
            arm64_strip_pac(context);
        }

        save_next = 2;
        ptr += length;
    }
    return TRUE;
}

static BOOL arm64_unwind_packed(struct cpu_stack_walk *csw, DWORD64 pc, const struct arm64_image_info *image, const struct arm64_runtime_function *function, CONTEXT *context, BOOL *final_pc_from_lr)
{
    BYTE prologue[64], epilogue[64];
    unsigned data = function->unwind_data;
    unsigned flag = data & ARM64_UNWIND_FLAG_MASK;
    unsigned function_length = (data >> ARM64_PACKED_FUNCTION_LENGTH_SHIFT) & ARM64_PACKED_FUNCTION_LENGTH_MASK;
    unsigned reg_f = (data >> ARM64_PACKED_REG_F_SHIFT) & ARM64_PACKED_REG_F_MASK;
    unsigned reg_i = (data >> ARM64_PACKED_REG_I_SHIFT) & ARM64_PACKED_REG_I_MASK;
    unsigned homing = (data >> ARM64_PACKED_H_SHIFT) & 1;
    unsigned cr = (data >> ARM64_PACKED_CR_SHIFT) & ARM64_PACKED_CR_MASK;
    unsigned frame_size = (data >> ARM64_PACKED_FRAME_SIZE_SHIFT) & ARM64_PACKED_FRAME_SIZE_MASK;
    unsigned int_size = reg_i * sizeof(DWORD64), fp_size = reg_f * sizeof(DWORD64);
    unsigned regsave, local_size, int_regs, fp_regs, saved_regs, rva, offset, length;
    unsigned ppos = 0, epos = 0;
    int i;

#define WRITE_ONE(code) do { \
    if (ppos >= ARRAY_SIZE(prologue) || epos >= ARRAY_SIZE(epilogue)) return FALSE; \
    prologue[ppos++] = epilogue[epos++] = (BYTE)(code); \
} while (0)
#define WRITE_TWO(code) do { WRITE_ONE((code) >> 8); WRITE_ONE(code); } while (0)
#define WRITE_ALLOC(size) do { \
    unsigned alloc_size = (size); \
    if (!alloc_size || (alloc_size & 0xf) || alloc_size >= 0x8000) return FALSE; \
    if (alloc_size < 0x200) WRITE_ONE(alloc_size / 16); \
    else WRITE_TWO(0xc000 | (alloc_size / 16)); \
} while (0)

    rva = pc - image->base;
    if ((flag != 1 && flag != 2) || !function_length || reg_i > 10 || rva < function->begin_address) return FALSE;
    offset = (rva - function->begin_address) / sizeof(DWORD);
    if (offset >= function_length) return FALSE;

    if (cr == 1) int_size += sizeof(DWORD64);
    if (reg_f) fp_size += sizeof(DWORD64);
    regsave = (int_size + fp_size + 8 * sizeof(DWORD64) * homing + 0xf) & ~0xf;
    if (frame_size * 16 < regsave) return FALSE;
    local_size = frame_size * 16 - regsave;
    int_regs = int_size / sizeof(DWORD64);
    fp_regs = fp_size / sizeof(DWORD64);
    saved_regs = regsave / sizeof(DWORD64);

    if (homing && !reg_i && !reg_f && cr != 1)
    {
        local_size += regsave;
        homing = 0;
    }

    if (cr == 2 || cr == 3)
    {
        if (local_size < 16) return FALSE;
        WRITE_ONE(0xe1);
        if (local_size <= 0x200)
            WRITE_ONE(0x80 | (local_size / 8 - 1));
        else
            WRITE_ONE(0x40);
    }
    if ((cr <= 1 && local_size) || local_size > 0x200)
    {
        if (local_size > 0xff0)
        {
            WRITE_ALLOC(local_size - 0xff0);
            WRITE_ALLOC(0xff0);
        }
        else
        {
            WRITE_ALLOC(local_size);
        }
    }
    if (homing)
    {
        if (ppos > ARRAY_SIZE(prologue) - 4) return FALSE;
        prologue[ppos++] = 0xe3;
        prologue[ppos++] = 0xe3;
        prologue[ppos++] = 0xe3;
        prologue[ppos++] = 0xe3;
    }
    if (reg_f)
    {
        if (!(reg_f % 2)) WRITE_TWO(0xdc00 | (reg_f << 6) | (int_regs + fp_regs - 1));
        for (i = (reg_f + 1) / 2 - 1; i >= 0; i--)
        {
            if (!i && !int_size)
                WRITE_TWO(0xda00 | (saved_regs - 1));
            else
                WRITE_TWO(0xd800 | (2 * i << 6) | (int_regs + 2 * i));
        }
    }
    if (cr == 1 && !(reg_i % 2))
    {
        if (!reg_i)
            WRITE_TWO(0xd400 | ((30 - 19) << 5) | (saved_regs - 1));
        else
            WRITE_TWO(0xd000 | ((30 - 19) << 6) | (int_regs - 1));
    }
    if (reg_i)
    {
        if (reg_i % 2)
        {
            if (cr == 1)
            {
                WRITE_TWO(0xd600 | ((reg_i - 1) / 2 << 6) | (int_regs - 2));
                if (reg_i == 1) WRITE_ONE(saved_regs / 2);
            }
            else if (reg_i == 1)
            {
                WRITE_TWO(0xd400 | (saved_regs - 1));
            }
            else
            {
                WRITE_TWO(0xd000 | ((int_regs - 1) << 6) | (int_regs - 1));
            }
        }
        for (i = reg_i / 2 - 1; i >= 0; i--)
        {
            if (i)
                WRITE_TWO(0xc800 | (2 * i << 6) | (2 * i));
            else
                WRITE_TWO(0xcc00 | (saved_regs - 1));
        }
    }
    if (cr == 2) WRITE_ONE(0xfc);
    WRITE_ONE(0xe4);

#undef WRITE_ALLOC
#undef WRITE_TWO
#undef WRITE_ONE

    if (flag == 1)
    {
        if (!arm64_sequence_length(prologue, prologue + ppos, &length)) return FALSE;
        if (offset < length)
            return arm64_process_unwind_codes(csw, prologue, prologue + ppos, context, length - offset, final_pc_from_lr);

        if (!arm64_sequence_length(epilogue, epilogue + epos, &length) || function_length <= length) return FALSE;
        if (offset >= function_length - (length + 1))
            return arm64_process_unwind_codes(csw, epilogue, epilogue + epos, context, offset - (function_length - (length + 1)), final_pc_from_lr);
    }
    return arm64_process_unwind_codes(csw, prologue, prologue + ppos, context, 0, final_pc_from_lr);
}

static BOOL arm64_unwind_full(struct cpu_stack_walk *csw, DWORD64 pc, const struct arm64_image_info *image, const struct arm64_runtime_function *function, CONTEXT *context, BOOL *final_pc_from_lr)
{
    BYTE codes[ARM64_XDATA_MAX_CODE_WORDS * sizeof(DWORD)];
    DWORD header, extension, epilog, cursor, epilog_bytes, code_bytes;
    unsigned function_length, epilogs, code_words, rva, offset, length, i, index, epilog_offset;
    BOOL epilog_in_header;
    const BYTE *ptr, *end;

    if (!function->unwind_data || !arm64_read_image(csw, image, function->unwind_data, &header, sizeof(header)) ||
        ((header >> ARM64_XDATA_VERSION_SHIFT) & ARM64_XDATA_VERSION_MASK)) return FALSE;

    function_length = header & ARM64_XDATA_FUNCTION_LENGTH_MASK;
    epilog_in_header = (header >> ARM64_XDATA_EPILOG_IN_HEADER_SHIFT) & 1;
    epilogs = (header >> ARM64_XDATA_EPILOG_COUNT_SHIFT) & ARM64_XDATA_EPILOG_COUNT_MASK;
    code_words = (header >> ARM64_XDATA_CODE_WORDS_SHIFT) & ARM64_XDATA_CODE_WORDS_MASK;
    cursor = function->unwind_data + sizeof(header);
    if (cursor < function->unwind_data) return FALSE;

    if (!code_words && !epilogs)
    {
        if (!arm64_read_image(csw, image, cursor, &extension, sizeof(extension))) return FALSE;
        epilogs = extension & 0xffff;
        code_words = (extension >> 16) & 0xff;
        cursor += sizeof(extension);
    }
    rva = pc - image->base;
    if (!function_length || !code_words || code_words > ARM64_XDATA_MAX_CODE_WORDS || rva < function->begin_address) return FALSE;
    offset = (rva - function->begin_address) / sizeof(DWORD);
    if (offset >= function_length) return FALSE;

    if (!epilog_in_header)
    {
        epilog_bytes = epilogs * sizeof(DWORD);
        if (!arm64_image_range_valid(image, cursor, epilog_bytes)) return FALSE;
        cursor += epilog_bytes;
    }
    code_bytes = code_words * sizeof(DWORD);
    if (!arm64_read_image(csw, image, cursor, codes, code_bytes)) return FALSE;
    end = codes + code_bytes;

    if (offset < code_bytes)
    {
        if (!arm64_sequence_length(codes, end, &length)) return FALSE;
        if (offset < length)
            return arm64_process_unwind_codes(csw, codes, end, context, length - offset, final_pc_from_lr);
    }

    if (!epilog_in_header)
    {
        cursor -= epilog_bytes;
        for (i = 0; i < epilogs; i++)
        {
            if (!arm64_read_image(csw, image, cursor + i * sizeof(epilog), &epilog, sizeof(epilog)) ||
                (epilog & 0x003c0000)) return FALSE;
            epilog_offset = epilog & ARM64_XDATA_EPILOG_OFFSET_MASK;
            index = (epilog >> ARM64_XDATA_EPILOG_INDEX_SHIFT) & ARM64_XDATA_EPILOG_INDEX_MASK;
            if (index >= code_bytes) return FALSE;
            if (offset < epilog_offset) break;
            if (offset - epilog_offset < code_bytes - index)
            {
                ptr = codes + index;
                if (!arm64_sequence_length(ptr, end, &length)) return FALSE;
                if (offset <= epilog_offset + length)
                    return arm64_process_unwind_codes(csw, ptr, end, context, offset - epilog_offset, final_pc_from_lr);
            }
        }
    }
    else
    {
        index = epilogs;
        if (index >= code_bytes) return FALSE;
        if (function_length - offset > code_bytes - index) goto body;
        ptr = codes + index;
        if (!arm64_sequence_length(ptr, end, &length)) return FALSE;
        length++;
        if (function_length >= length && offset >= function_length - length)
            return arm64_process_unwind_codes(csw, ptr, end, context, offset - (function_length - length), final_pc_from_lr);
    }

body:
    return arm64_process_unwind_codes(csw, codes, end, context, 0, final_pc_from_lr);
}

static BOOL arm64_virtual_unwind(struct cpu_stack_walk *csw, DWORD64 pc, const struct arm64_image_info *image, const struct arm64_runtime_function *function, CONTEXT *context)
{
    CONTEXT next = *context;
    BOOL final_pc_from_lr = TRUE, ret;
    DWORD flag = function->unwind_data & ARM64_UNWIND_FLAG_MASK;

    next.ContextFlags |= CONTEXT_UNWOUND_TO_CALL;
    if (!flag)
        ret = arm64_unwind_full(csw, pc, image, function, &next, &final_pc_from_lr);
    else if (flag != 3)
        ret = arm64_unwind_packed(csw, pc, image, function, &next, &final_pc_from_lr);
    else
        return FALSE;
    if (!ret) return FALSE;
    if (final_pc_from_lr) next.Pc = next.u.s.Lr;
    if (!next.Pc || (next.Pc == context->Pc && next.Sp == context->Sp)) return FALSE;
    *context = next;
    return TRUE;
}

#endif /* __REACTOS__ */

/* fetch_next_frame()
 *
 * modify (at least) context.Pc using unwind information
 * either out of debug info (dwarf), or simple Lr trace
 */
static BOOL fetch_next_frame(struct cpu_stack_walk *csw, union ctx *pcontext, DWORD_PTR curr_pc, void **prtf)
{
    DWORD64 xframe;
    CONTEXT *context = &pcontext->ctx;
    DWORD_PTR oldReturn = context->u.s.Lr;

    if (prtf) *prtf = NULL;
#ifdef __REACTOS__
    {
        struct arm64_runtime_function function;
        struct arm64_image_info image;
        void *table_entry;

        if (arm64_lookup_runtime_function(csw, curr_pc, &image, &function, &table_entry))
        {
            if (prtf) *prtf = table_entry;
            return arm64_virtual_unwind(csw, curr_pc, &image, &function, context);
        }
    }
#endif

    if (dwarf2_virtual_unwind(csw, curr_pc, pcontext, &xframe))
    {
        context->Sp = xframe;
        context->Pc = oldReturn;
        return TRUE;
    }

    if (context->Pc == context->u.s.Lr) return FALSE;
    context->Pc = oldReturn;

    return TRUE;
}

static BOOL arm64_stack_walk(struct cpu_stack_walk *csw, STACKFRAME64 *frame, union ctx *context)
{
    unsigned deltapc = curr_count <= 1 ? 0 : 4;

    /* sanity check */
    if (curr_mode >= stm_done) return FALSE;

    TRACE("Enter: PC=%s Frame=%s Return=%s Stack=%s Mode=%s Count=%s\n",
          wine_dbgstr_addr(&frame->AddrPC),
          wine_dbgstr_addr(&frame->AddrFrame),
          wine_dbgstr_addr(&frame->AddrReturn),
          wine_dbgstr_addr(&frame->AddrStack),
          curr_mode == stm_start ? "start" : "ARM64",
          wine_dbgstr_longlong(curr_count));

    if (curr_mode == stm_start)
    {
        /* Init done */
        set_curr_mode(stm_arm64);
        frame->AddrReturn.Mode = frame->AddrStack.Mode = AddrModeFlat;
        frame->FuncTableEntry = NULL;
        /* don't set up AddrStack on first call. Either the caller has set it up, or
         * we will get it in the next frame
         */
        memset(&frame->AddrBStore, 0, sizeof(frame->AddrBStore));
    }
    else
    {
        if (context->ctx.Sp != frame->AddrStack.Offset) FIXME("inconsistent Stack Pointer\n");
        if (context->ctx.Pc != frame->AddrPC.Offset) FIXME("inconsistent Program Counter\n");

        if (frame->AddrReturn.Offset == 0) goto done_err;
        if (!fetch_next_frame(csw, context, frame->AddrPC.Offset - deltapc, &frame->FuncTableEntry))
            goto done_err;
    }

    memset(&frame->Params, 0, sizeof(frame->Params));

    /* set frame information */
    frame->AddrStack.Offset = context->ctx.Sp;
    frame->AddrReturn.Offset = context->ctx.u.s.Lr;
    frame->AddrFrame.Offset = context->ctx.u.s.Fp;
    frame->AddrPC.Offset = context->ctx.Pc;

    frame->Far = TRUE;
    frame->Virtual = TRUE;
    inc_curr_count();

    TRACE("Leave: PC=%s Frame=%s Return=%s Stack=%s Mode=%s Count=%s FuncTable=%p\n",
          wine_dbgstr_addr(&frame->AddrPC),
          wine_dbgstr_addr(&frame->AddrFrame),
          wine_dbgstr_addr(&frame->AddrReturn),
          wine_dbgstr_addr(&frame->AddrStack),
          curr_mode == stm_start ? "start" : "ARM64",
          wine_dbgstr_longlong(curr_count),
          frame->FuncTableEntry);

    return TRUE;
done_err:
    set_curr_mode(stm_done);
    return FALSE;
}
#else
static BOOL arm64_stack_walk(struct cpu_stack_walk* csw, STACKFRAME64 *frame,
    union ctx *ctx)
{
    return FALSE;
}
#endif

static unsigned arm64_map_dwarf_register(unsigned regno, const struct module* module, BOOL eh_frame)
{
    if (regno <= 28) return CV_ARM64_X0 + regno;
    if (regno == 29) return CV_ARM64_FP;
    if (regno == 30) return CV_ARM64_LR;
    if (regno == 31) return CV_ARM64_SP;
    if (regno >= 64 && regno <= 95) return CV_ARM64_Q0 + regno - 64;

    FIXME("Don't know how to map register %d\n", regno);
    return CV_ARM64_NOREG;
}

static void *arm64_fetch_context_reg(union ctx *pctx, unsigned regno, unsigned *size)
{
#ifdef __aarch64__
    CONTEXT *ctx = &pctx->ctx;

    switch (regno)
    {
    case CV_ARM64_X0 +  0:
    case CV_ARM64_X0 +  1:
    case CV_ARM64_X0 +  2:
    case CV_ARM64_X0 +  3:
    case CV_ARM64_X0 +  4:
    case CV_ARM64_X0 +  5:
    case CV_ARM64_X0 +  6:
    case CV_ARM64_X0 +  7:
    case CV_ARM64_X0 +  8:
    case CV_ARM64_X0 +  9:
    case CV_ARM64_X0 + 10:
    case CV_ARM64_X0 + 11:
    case CV_ARM64_X0 + 12:
    case CV_ARM64_X0 + 13:
    case CV_ARM64_X0 + 14:
    case CV_ARM64_X0 + 15:
    case CV_ARM64_X0 + 16:
    case CV_ARM64_X0 + 17:
    case CV_ARM64_X0 + 18:
    case CV_ARM64_X0 + 19:
    case CV_ARM64_X0 + 20:
    case CV_ARM64_X0 + 21:
    case CV_ARM64_X0 + 22:
    case CV_ARM64_X0 + 23:
    case CV_ARM64_X0 + 24:
    case CV_ARM64_X0 + 25:
    case CV_ARM64_X0 + 26:
    case CV_ARM64_X0 + 27:
    case CV_ARM64_X0 + 28: *size = sizeof(ctx->u.X[0]); return &ctx->u.X[regno - CV_ARM64_X0];
    case CV_ARM64_PSTATE:  *size = sizeof(ctx->Cpsr);   return &ctx->Cpsr;
    case CV_ARM64_FP:      *size = sizeof(ctx->u.s.Fp); return &ctx->u.s.Fp;
    case CV_ARM64_LR:      *size = sizeof(ctx->u.s.Lr); return &ctx->u.s.Lr;
    case CV_ARM64_SP:      *size = sizeof(ctx->Sp);     return &ctx->Sp;
    case CV_ARM64_PC:      *size = sizeof(ctx->Pc);     return &ctx->Pc;
    }
#endif
    FIXME("Unknown register %x\n", regno);
    return NULL;
}

static const char* arm64_fetch_regname(unsigned regno)
{
    switch (regno)
    {
    case CV_ARM64_PSTATE:  return "cpsr";
    case CV_ARM64_X0 +  0: return "x0";
    case CV_ARM64_X0 +  1: return "x1";
    case CV_ARM64_X0 +  2: return "x2";
    case CV_ARM64_X0 +  3: return "x3";
    case CV_ARM64_X0 +  4: return "x4";
    case CV_ARM64_X0 +  5: return "x5";
    case CV_ARM64_X0 +  6: return "x6";
    case CV_ARM64_X0 +  7: return "x7";
    case CV_ARM64_X0 +  8: return "x8";
    case CV_ARM64_X0 +  9: return "x9";
    case CV_ARM64_X0 + 10: return "x10";
    case CV_ARM64_X0 + 11: return "x11";
    case CV_ARM64_X0 + 12: return "x12";
    case CV_ARM64_X0 + 13: return "x13";
    case CV_ARM64_X0 + 14: return "x14";
    case CV_ARM64_X0 + 15: return "x15";
    case CV_ARM64_X0 + 16: return "x16";
    case CV_ARM64_X0 + 17: return "x17";
    case CV_ARM64_X0 + 18: return "x18";
    case CV_ARM64_X0 + 19: return "x19";
    case CV_ARM64_X0 + 20: return "x20";
    case CV_ARM64_X0 + 21: return "x21";
    case CV_ARM64_X0 + 22: return "x22";
    case CV_ARM64_X0 + 23: return "x23";
    case CV_ARM64_X0 + 24: return "x24";
    case CV_ARM64_X0 + 25: return "x25";
    case CV_ARM64_X0 + 26: return "x26";
    case CV_ARM64_X0 + 27: return "x27";
    case CV_ARM64_X0 + 28: return "x28";

    case CV_ARM64_FP:     return "fp";
    case CV_ARM64_LR:     return "lr";
    case CV_ARM64_SP:     return "sp";
    case CV_ARM64_PC:     return "pc";
    }
    FIXME("Unknown register %x\n", regno);
    return NULL;
}

static BOOL arm64_fetch_minidump_thread(struct dump_context* dc, unsigned index, unsigned flags, const CONTEXT* ctx)
{
    if (ctx->ContextFlags && (flags & ThreadWriteInstructionWindow))
    {
        /* FIXME: crop values across module boundaries, */
#ifdef __aarch64__
        ULONG base = ctx->Pc <= 0x80 ? 0 : ctx->Pc - 0x80;
        minidump_add_memory_block(dc, base, ctx->Pc + 0x80 - base, 0);
#endif
    }

    return TRUE;
}

static BOOL arm64_fetch_minidump_module(struct dump_context* dc, unsigned index, unsigned flags)
{
    /* FIXME: actually, we should probably take care of FPO data, unless it's stored in
     * function table minidump stream
     */
    return FALSE;
}

DECLSPEC_HIDDEN struct cpu cpu_arm64 = {
    IMAGE_FILE_MACHINE_ARM64,
    8,
    CV_ARM64_FP,
    arm64_get_addr,
    arm64_stack_walk,
    NULL,
    arm64_map_dwarf_register,
    arm64_fetch_context_reg,
    arm64_fetch_regname,
    arm64_fetch_minidump_thread,
    arm64_fetch_minidump_module,
};
