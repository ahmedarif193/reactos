/*
 * PROJECT:         ReactOS dbghelp extension
 * LICENSE:         GPLv2+ - See COPYING in the top level directory
 * PURPOSE:         Parse rsym information for use with dbghelp
 * PROGRAMMER:      Mark Jansen
 */

#include "dbghelp_private.h"
#include <reactos/rossym.h>

#include <wine/debug.h>
WINE_DEFAULT_DEBUG_CHANNEL(dbghelp_rsym);


typedef struct rsym_file_entry_s
{
    const char* File;
    unsigned Source;
} rsym_file_entry_t;

typedef struct rsym_func_entry_s
{
    DWORD64 Address;
    struct symt_function* func;
    struct rsym_func_entry_s* next;
} rsym_func_entry_t;

typedef struct rsym_entry_s
{
    DWORD64 Address;
    ULONG FunctionOffset;
    ULONG FileOffset;
    ULONG SourceLine;
} rsym_entry_t;



/******************************************************************
 *		rsym_finalize_function (copied from stabs_finalize_function)
 *
 * Ends function creation: mainly:
 * - cleans up line number information
 * - tries to set up a debug-start tag (FIXME: heuristic to be enhanced)
 */
static void rsym_finalize_function(struct module* module, struct symt_function* func)
{
    IMAGEHLP_LINE64     il;
    struct location     loc;

    if (!func) return;
    symt_normalize_function(module, func);
    /* To define the debug-start of the function, we use the second line number.
     * Not 100% bullet proof, but better than nothing
     */
    if (symt_fill_func_line_info(module, func, func->address, &il) &&
        symt_get_func_line_next(module, &il))
    {
        loc.kind = loc_absolute;
        loc.offset = il.Address - func->address;
        symt_add_function_point(module, func, SymTagFuncDebugStart,
                                &loc, NULL);
    }
}


static int is_metadata_sym(const char* name)
{
    ULONG len = name ? strlen(name) : 0;
    return len > 3 && name[0] == '_' && name[1] != '_' && name[len-1] == '_' && name[len-2] == '_';
};

static int use_raw_address(const char* name)
{
    if (!name)
        return 0;

    if (!strcmp(name, "__ImageBase"))
        return 1;

    if (!strcmp(name, "__RUNTIME_PSEUDO_RELOC_LIST__"))
        return 1;

    return 0;
}


static void rsym_decode_entry(const BYTE* raw, rsym_entry_t* entry)
{
    ROSSYM_ENTRY raw_entry;

    memcpy(&raw_entry, raw, sizeof(raw_entry));
    entry->Address = raw_entry.Address;
    entry->FunctionOffset = raw_entry.FunctionOffset;
    entry->FileOffset = raw_entry.FileOffset;
    entry->SourceLine = raw_entry.SourceLine;
}

static const char* rsym_get_string(const char* strings, ULONG length, ULONG offset)
{
    if (offset >= length || !memchr(strings + offset, '\0', length - offset))
        return NULL;
    return strings + offset;
}

BOOL rsym_parse(struct module* module, DWORD64 load_offset,
                const void* rsym_ptr, unsigned rsymlen)
{
    ROSSYM_HEADER RosSymHeader;
    const BYTE* First;
    const CHAR* Strings;
    ULONG SymbolsEnd;
    unsigned EntrySize, EntryCount, Index;
    DWORD64 PreviousAddress = 0;

    struct pool pool;
    struct sparse_array file_table, func_table;
    rsym_func_entry_t* first_func = NULL;

    if (!rsym_ptr || rsymlen < sizeof(ROSSYM_HEADER))
    {
        WARN("Invalid ROSSYM_HEADER\n");
        return FALSE;
    }

    memcpy(&RosSymHeader, rsym_ptr, sizeof(RosSymHeader));
    EntrySize = sizeof(ROSSYM_ENTRY);
    if (RosSymHeader.SymbolsOffset < sizeof(ROSSYM_HEADER) ||
        RosSymHeader.SymbolsOffset > rsymlen ||
        RosSymHeader.SymbolsLength > rsymlen - RosSymHeader.SymbolsOffset)
    {
        WARN("Invalid ROSSYM_HEADER\n");
        return FALSE;
    }

    SymbolsEnd = RosSymHeader.SymbolsOffset + RosSymHeader.SymbolsLength;
    if (RosSymHeader.StringsOffset < SymbolsEnd ||
        RosSymHeader.StringsOffset > rsymlen ||
        RosSymHeader.StringsLength > rsymlen - RosSymHeader.StringsOffset ||
        RosSymHeader.SymbolsLength % EntrySize != 0 ||
        (RosSymHeader.SymbolsLength != 0 && RosSymHeader.StringsLength == 0))
    {
        WARN("Invalid ROSSYM_HEADER\n");
        return FALSE;
    }

    First = (const BYTE*)rsym_ptr + RosSymHeader.SymbolsOffset;
    Strings = (const CHAR*)rsym_ptr + RosSymHeader.StringsOffset;
    EntryCount = RosSymHeader.SymbolsLength / EntrySize;

    for (Index = 0; Index < EntryCount; Index++)
    {
        rsym_entry_t Entry;

        rsym_decode_entry(First + Index * EntrySize, &Entry);
        if ((Index != 0 && Entry.Address < PreviousAddress) ||
            !rsym_get_string(Strings, RosSymHeader.StringsLength, Entry.FunctionOffset) ||
            !rsym_get_string(Strings, RosSymHeader.StringsLength, Entry.FileOffset) ||
            Entry.Address > ~(DWORD64)0 - load_offset)
        {
            WARN("Invalid ROSSYM entry\n");
            return FALSE;
        }
        PreviousAddress = Entry.Address;
    }

    pool_init(&pool, 65536);
    sparse_array_init(&file_table, sizeof(rsym_file_entry_t), 64);
    sparse_array_init(&func_table, sizeof(rsym_func_entry_t), 128);

    for (Index = 0; Index < EntryCount; Index++)
    {
        rsym_entry_t Entry;
        DWORD64 Address;

        rsym_decode_entry(First + Index * EntrySize, &Entry);
        Address = load_offset + Entry.Address;
        if (!Entry.FileOffset)
        {
            rsym_func_entry_t* func = sparse_array_find(&func_table, Entry.FunctionOffset);

            /* We do not want to define a data point where there is already a function! */
            if (!func || func->Address != Address)
            {
                const char* SymbolName = Strings + Entry.FunctionOffset;
                if (!is_metadata_sym(SymbolName))
                {
                    /* TODO: How should we determine the size? */
                    ULONG Size = sizeof(ULONG);
                    if (use_raw_address(SymbolName))
                        Address = Entry.Address;

                    symt_new_public(module, NULL, SymbolName, FALSE, Address, Size);
                }
                else
                {
                    /* Maybe use it to fill some metadata? */
                }
            }
        }
        else
        {
            rsym_file_entry_t* file = sparse_array_find(&file_table, Entry.FileOffset);
            rsym_func_entry_t* func = sparse_array_find(&func_table, Entry.FunctionOffset);

            if (!file)
            {
                file = sparse_array_add(&file_table, Entry.FileOffset, &pool);
                file->File = Strings + Entry.FileOffset;
                file->Source = source_new(module, NULL, Strings + Entry.FileOffset);
            }

            if (!func)
            {
                func = sparse_array_add(&func_table, Entry.FunctionOffset, &pool);
                func->func = symt_new_function(module, NULL, Strings + Entry.FunctionOffset,
                    Address, 0, NULL);
                func->Address = Address;
                func->next = first_func;
                first_func = func;
            }

            /* TODO: What if we have multiple chunks scattered around? */
            symt_add_func_line(module, func->func, file->Source, Entry.SourceLine, Address - func->Address);
        }
    }

    while (first_func)
    {
        /* TODO: Size of function? */
        rsym_finalize_function(module, first_func->func);
        first_func = first_func->next;
    }

    module->module.SymType = SymDia;
    module->module.CVSig = 'R' | ('S' << 8) | ('Y' << 16) | ('M' << 24);
    module->module.LineNumbers = TRUE;
    module->module.GlobalSymbols = TRUE;
    module->module.TypeInfo = FALSE;
    module->module.SourceIndexed = TRUE;
    module->module.Publics = TRUE;

    pool_destroy(&pool);

    return TRUE;
}
