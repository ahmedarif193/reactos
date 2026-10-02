/*
 * PROJECT:     ReactOS system libraries
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Mapping NT 6+ national language support data files
 * COPYRIGHT:   Adapted from Wine dlls/ntdll/locale.c, dlls/ntdll/locale_private.h and dlls/ntdll/unix/env.c
 */

#include <ntdll.h>
#include <winnls.h>

#define NDEBUG
#include <debug.h>

#define NLS_SECTION_SORTKEYS  9
#define NLS_SECTION_CASEMAP   10
#define NLS_SECTION_CODEPAGE  11
#define NLS_SECTION_NORMALIZE 12

#define NLS_NORMALIZATION_C   1
#define NLS_NORMALIZATION_D   2
#define NLS_NORMALIZATION_KC  5
#define NLS_NORMALIZATION_KD  6
#define NLS_NORMALIZATION_IDN 13

static NTSTATUS
NlsBuildCodePageFileName(
    _In_ ULONG CodePage,
    _Out_writes_(FileNameLength) PWCHAR FileName,
    _In_ USHORT FileNameLength)
{
    WCHAR DigitsBuffer[11];
    USHORT DigitsLength;
    UNICODE_STRING Digits;
    UNICODE_STRING Name;
    NTSTATUS Status;

    RtlInitEmptyUnicodeString(&Name, FileName, FileNameLength * sizeof(WCHAR));
    Status = RtlAppendUnicodeToString(&Name, L"c_");
    if (!NT_SUCCESS(Status)) return Status;

    RtlInitEmptyUnicodeString(&Digits, DigitsBuffer, sizeof(DigitsBuffer));
    Status = RtlIntegerToUnicodeString(CodePage, 10, &Digits);
    if (!NT_SUCCESS(Status)) return Status;

    DigitsLength = Digits.Length;
    while (DigitsLength < 3 * sizeof(WCHAR))
    {
        Status = RtlAppendUnicodeToString(&Name, L"0");
        if (!NT_SUCCESS(Status)) return Status;
        DigitsLength += sizeof(WCHAR);
    }

    Status = RtlAppendUnicodeStringToString(&Name, &Digits);
    if (!NT_SUCCESS(Status)) return Status;
    return RtlAppendUnicodeToString(&Name, L".nls");
}

static NTSTATUS
NlsGetFileName(
    _In_ ULONG Type,
    _In_ ULONG Id,
    _Out_writes_(FileNameLength) PWCHAR FileName,
    _In_ USHORT FileNameLength)
{
    PCWSTR Source;
    UNICODE_STRING Name;

    switch (Type)
    {
        case NLS_SECTION_SORTKEYS:
            if (Id) return STATUS_INVALID_PARAMETER_1;
            Source = L"sortdefault.nls";
            break;

        case NLS_SECTION_CASEMAP:
            if (Id) return STATUS_UNSUCCESSFUL;
            Source = L"l_intl.nls";
            break;

        case NLS_SECTION_CODEPAGE:
            return NlsBuildCodePageFileName(Id, FileName, FileNameLength);

        case NLS_SECTION_NORMALIZE:
            switch (Id)
            {
                case NLS_NORMALIZATION_C:
                    Source = L"normnfc.nls";
                    break;
                case NLS_NORMALIZATION_D:
                    Source = L"normnfd.nls";
                    break;
                case NLS_NORMALIZATION_KC:
                    Source = L"normnfkc.nls";
                    break;
                case NLS_NORMALIZATION_KD:
                    Source = L"normnfkd.nls";
                    break;
                case NLS_NORMALIZATION_IDN:
                    Source = L"normidna.nls";
                    break;
                default:
                    return STATUS_OBJECT_NAME_NOT_FOUND;
            }
            break;

        default:
            return STATUS_INVALID_PARAMETER_1;
    }

    RtlInitEmptyUnicodeString(&Name, FileName, FileNameLength * sizeof(WCHAR));
    return RtlAppendUnicodeToString(&Name, Source);
}

static NTSTATUS
NlsMapFile(
    _In_ PCWSTR FileName,
    _Out_ PVOID *BaseAddress,
    _Out_ PSIZE_T ViewSize)
{
    WCHAR PathBuffer[MAX_PATH + 32];
    UNICODE_STRING Path;
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    HANDLE FileHandle;
    HANDLE SectionHandle;
    SIZE_T Size = 0;
    NTSTATUS Status;

    RtlInitEmptyUnicodeString(&Path, PathBuffer, sizeof(PathBuffer));
    Status = RtlAppendUnicodeToString(&Path, L"\\??\\");
    if (!NT_SUCCESS(Status)) return Status;
    Status = RtlAppendUnicodeToString(&Path, SharedUserData->NtSystemRoot);
    if (!NT_SUCCESS(Status)) return Status;
#ifdef _M_IX86
    Status = RtlAppendUnicodeToString(&Path, NtCurrentTeb()->WOW32Reserved ? L"\\Sysnative\\" : L"\\System32\\");
#else
    Status = RtlAppendUnicodeToString(&Path, L"\\System32\\");
#endif
    if (!NT_SUCCESS(Status)) return Status;
    Status = RtlAppendUnicodeToString(&Path, FileName);
    if (!NT_SUCCESS(Status)) return Status;

    InitializeObjectAttributes(&ObjectAttributes, &Path, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtOpenFile(&FileHandle, FILE_READ_DATA | SYNCHRONIZE, &ObjectAttributes, &IoStatusBlock, FILE_SHARE_READ, FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT);
    if (!NT_SUCCESS(Status)) return Status;

    Status = NtCreateSection(&SectionHandle, SECTION_MAP_READ, NULL, NULL, PAGE_READONLY, SEC_COMMIT, FileHandle);
    NtClose(FileHandle);
    if (!NT_SUCCESS(Status)) return Status;

    *BaseAddress = NULL;
    Status = NtMapViewOfSection(SectionHandle, NtCurrentProcess(), BaseAddress, 0, 0, NULL, &Size, ViewShare, 0, PAGE_READONLY);
    NtClose(SectionHandle);
    if (!NT_SUCCESS(Status)) return Status;

    *ViewSize = Size;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
NtGetNlsSectionPtr(
    _In_ ULONG Type,
    _In_ ULONG Id,
    _In_opt_ PVOID Unknown,
    _Out_ PVOID *BaseAddress,
    _Out_ PSIZE_T ViewSize)
{
    WCHAR FileName[20];
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Unknown);

    if (!BaseAddress || !ViewSize) return STATUS_INVALID_PARAMETER;
    Status = NlsGetFileName(Type, Id, FileName, RTL_NUMBER_OF(FileName));
    if (!NT_SUCCESS(Status)) return Status;
    return NlsMapFile(FileName, BaseAddress, ViewSize);
}

NTSTATUS
NTAPI
NtInitializeNlsFiles(
    _Out_ PVOID *BaseAddress,
    _Out_ PLCID DefaultLocaleId,
    _Out_opt_ PLARGE_INTEGER DefaultCasingTableSize)
{
    SIZE_T ViewSize;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(DefaultCasingTableSize);

    if (!BaseAddress || !DefaultLocaleId) return STATUS_INVALID_PARAMETER;
    Status = NlsMapFile(L"locale.nls", BaseAddress, &ViewSize);
    if (!NT_SUCCESS(Status)) return Status;
    Status = NtQueryDefaultLocale(FALSE, DefaultLocaleId);
    if (!NT_SUCCESS(Status)) NtUnmapViewOfSection(NtCurrentProcess(), *BaseAddress);
    return Status;
}

NTSTATUS
NTAPI
RtlGetLocaleFileMappingAddress(
    _Out_ PVOID *BaseAddress,
    _Out_ PLCID DefaultLocaleId,
    _Out_opt_ PLARGE_INTEGER DefaultCasingTableSize)
{
    static PVOID CachedAddress;
    static LCID CachedLocaleId;
    PVOID Address;
    NTSTATUS Status;

    if (!BaseAddress || !DefaultLocaleId) return STATUS_INVALID_PARAMETER;
    if (!CachedAddress)
    {
        Status = NtInitializeNlsFiles(&Address, &CachedLocaleId, DefaultCasingTableSize);
        if (!NT_SUCCESS(Status)) return Status;
        if (InterlockedCompareExchangePointer(&CachedAddress, Address, NULL)) NtUnmapViewOfSection(NtCurrentProcess(), Address);
    }

    *BaseAddress = CachedAddress;
    *DefaultLocaleId = CachedLocaleId;
    return STATUS_SUCCESS;
}

struct norm_table
{
    WCHAR   name[13];
    USHORT  checksum[3];
    USHORT  version[4];
    USHORT  form;
    USHORT  len_factor;
    USHORT  unknown1;
    USHORT  decomp_size;
    USHORT  comp_size;
    USHORT  unknown2;
    USHORT  classes;
    USHORT  props_level1;
    USHORT  props_level2;
    USHORT  decomp_hash;
    USHORT  decomp_map;
    USHORT  decomp_seq;
    USHORT  comp_hash;
    USHORT  comp_seq;

};

static struct norm_table *norm_tables[16];

static inline int get_utf16( const WCHAR *src, unsigned int srclen, unsigned int *ch )
{
    if (IS_HIGH_SURROGATE( src[0] ))
    {
        if (srclen <= 1) return 0;
        if (!IS_LOW_SURROGATE( src[1] )) return 0;
        *ch = 0x10000 + ((src[0] & 0x3ff) << 10) + (src[1] & 0x3ff);
        return 2;
    }
    if (IS_LOW_SURROGATE( src[0] )) return 0;
    *ch = src[0];
    return 1;
}

static inline void put_utf16( WCHAR *dst, unsigned int ch )
{
    if (ch >= 0x10000)
    {
        ch -= 0x10000;
        dst[0] = 0xd800 | (ch >> 10);
        dst[1] = 0xdc00 | (ch & 0x3ff);
    }
    else dst[0] = ch;
}

#define HANGUL_SBASE  0xac00
#define HANGUL_LBASE  0x1100
#define HANGUL_VBASE  0x1161
#define HANGUL_TBASE  0x11a7
#define HANGUL_LCOUNT 19
#define HANGUL_VCOUNT 21
#define HANGUL_TCOUNT 28
#define HANGUL_NCOUNT (HANGUL_VCOUNT * HANGUL_TCOUNT)
#define HANGUL_SCOUNT (HANGUL_LCOUNT * HANGUL_NCOUNT)

static inline const WCHAR *get_decomposition( const struct norm_table *info, unsigned int ch,
                                              BYTE props, WCHAR *buffer, unsigned int *ret_len )
{
    const struct pair { WCHAR src; USHORT dst; } *pairs;
    const USHORT *hash_table = (const USHORT *)info + info->decomp_hash;
    const WCHAR *ret;
    unsigned int i, pos, end, len, hash;

    put_utf16( buffer, ch );
    *ret_len = 1 + (ch >= 0x10000);
    if (!props || props == 0x7f) return buffer;

    if (props == 0xff)
    {
        if (ch >= HANGUL_SBASE && ch < HANGUL_SBASE + HANGUL_SCOUNT)
        {
            unsigned short sindex = ch - HANGUL_SBASE;
            unsigned short tindex = sindex % HANGUL_TCOUNT;
            buffer[0] = HANGUL_LBASE + sindex / HANGUL_NCOUNT;
            buffer[1] = HANGUL_VBASE + (sindex % HANGUL_NCOUNT) / HANGUL_TCOUNT;
            if (tindex) buffer[2] = HANGUL_TBASE + tindex;
            *ret_len = 2 + !!tindex;
            return buffer;
        }

        if (ch >= HANGUL_LBASE && ch < HANGUL_LBASE + 0x100) return buffer;
        if (ch >= HANGUL_SBASE && ch < HANGUL_SBASE + 0x2c00) return buffer;
        return NULL;
    }

    hash = ch % info->decomp_size;
    pos = hash_table[hash];
    if (pos >> 13)
    {
        if (props != 0xbf) return buffer;
        ret = (const USHORT *)info + info->decomp_seq + (pos & 0x1fff);
        len = pos >> 13;
    }
    else
    {
        pairs = (const struct pair *)((const USHORT *)info + info->decomp_map);

        for (i = hash + 1; i < info->decomp_size; i++) if (!(hash_table[i] >> 13)) break;
        if (i < info->decomp_size) end = hash_table[i];
        else for (end = pos; pairs[end].src; end++) ;

        for ( ; pos < end; pos++)
        {
            if (pairs[pos].src != (WCHAR)ch) continue;
            ret = (const USHORT *)info + info->decomp_seq + (pairs[pos].dst & 0x1fff);
            len = pairs[pos].dst >> 13;
            break;
        }
        if (pos >= end) return buffer;
    }

    if (len == 7) while (ret[len]) len++;
    if (!ret[0]) len = 0;
    *ret_len = len;
    return ret;
}

static inline BYTE rol( BYTE val, BYTE count )
{
    return (val << count) | (val >> (8 - count));
}

static inline BYTE get_char_props( const struct norm_table *info, unsigned int ch )
{
    const BYTE *level1 = (const BYTE *)((const USHORT *)info + info->props_level1);
    const BYTE *level2 = (const BYTE *)((const USHORT *)info + info->props_level2);
    BYTE off = level1[ch / 128];

    if (!off || off >= 0xfb) return rol( off, 5 );
    return level2[(off - 1) * 128 + ch % 128];
}

static inline BYTE get_combining_class( const struct norm_table *info, unsigned int c )
{
    const BYTE *classes = (const BYTE *)((const USHORT *)info + info->classes);
    BYTE class = get_char_props( info, c ) & 0x3f;

    if (class == 0x3f) return 0;
    return classes[class];
}

static inline BOOL reorderable_pair( const struct norm_table *info, unsigned int c1, unsigned int c2 )
{
    BYTE ccc1, ccc2;

    ccc1 = get_combining_class( info, c1 );
    if (ccc1 < 2) return FALSE;
    ccc2 = get_combining_class( info, c2 );
    return ccc2 && (ccc1 > ccc2);
}

static inline void canonical_order_substring( const struct norm_table *info, WCHAR *str, unsigned int len,
                                              LONG *origins )
{
    unsigned int i, ch1, ch2, len1, len2;
    BOOL swapped;

    do
    {
        swapped = FALSE;
        for (i = 0; i < len - 1; i += len1)
        {
            if (!(len1 = get_utf16( str + i, len - i, &ch1 ))) break;
            if (i + len1 >= len) break;
            if (!(len2 = get_utf16( str + i + len1, len - i - len1, &ch2 ))) break;
            if (reorderable_pair( info, ch1, ch2 ))
            {
                WCHAR tmp[2];
                memcpy( tmp, str + i, len1 * sizeof(WCHAR) );
                memcpy( str + i, str + i + len1, len2 * sizeof(WCHAR) );
                memcpy( str + i + len2, tmp, len1 * sizeof(WCHAR) );
                if (origins)
                {
                    LONG positions[2];
                    memcpy( positions, origins + i, len1 * sizeof(*origins) );
                    memmove( origins + i, origins + i + len1, len2 * sizeof(*origins) );
                    memcpy( origins + i + len2, positions, len1 * sizeof(*origins) );
                }
                swapped = TRUE;
                i += len2 - len1;
            }
        }
    } while (swapped);
}

static inline void canonical_order_string( const struct norm_table *info, WCHAR *str, unsigned int len,
                                           LONG *origins )
{
    unsigned int ch, i, r, next = 0;

    for (i = 0; i < len; i += r)
    {
        if (!(r = get_utf16( str + i, len - i, &ch ))) return;
        if (i && !get_combining_class( info, ch ))
        {
            if (i > next + 1)
                canonical_order_substring( info, str + next, i - next, origins ? origins + next : NULL );
            next = i + r;
        }
    }
    if (i > next + 1) canonical_order_substring( info, str + next, i - next, origins ? origins + next : NULL );
}

static inline LONG estimate_normalized_length( const struct norm_table *info, LONG src_len )
{
    LONG length = (LONG)((ULONG)src_len * info->len_factor);

    if (length > 64) length = max( 64, (LONG)((ULONG)src_len + (ULONG)(src_len / 8)) );
    return length;
}

struct norm_progress
{
    LONG source;
    LONG output;
};

static inline NTSTATUS decompose_string( const struct norm_table *info, const WCHAR *src, int src_len,
                                         WCHAR *dst, LONG *dst_len, struct norm_progress *progress,
                                         LONG *origins )
{
    BYTE props;
    int src_pos, dst_pos;
    unsigned int ch, len, decomp_len;
    WCHAR buffer[3];
    const WCHAR *decomp;

    for (src_pos = dst_pos = 0; src_pos < src_len; src_pos += len)
    {
        if (!(len = get_utf16( src + src_pos, src_len - src_pos, &ch )))
        {
            if (progress)
            {
                progress->source = src_pos;
                progress->output = dst_pos;
            }
            *dst_len = src_pos + IS_HIGH_SURROGATE( src[src_pos] );
            return STATUS_NO_UNICODE_TRANSLATION;
        }
        props = get_char_props( info, ch );
        if (!(decomp = get_decomposition( info, ch, props, buffer, &decomp_len )))
        {

            if (!ch && src_pos == src_len - 1 && dst_pos < *dst_len)
            {
                dst[dst_pos++] = 0;
                break;
            }
            if (progress)
            {
                progress->source = src_pos;
                progress->output = dst_pos;
            }
            *dst_len = src_pos;
            return STATUS_NO_UNICODE_TRANSLATION;
        }
        if (dst_pos + decomp_len > *dst_len)
        {
            LONG remaining = src_len - src_pos;
            LONG estimate;

            if (len == 2 && decomp_len == 2 && decomp[0] == src[src_pos] &&
                decomp[1] == src[src_pos + 1])
                remaining -= *dst_len - dst_pos;
            estimate = estimate_normalized_length( info, remaining );
            *dst_len = (LONG)((ULONG)*dst_len + (ULONG)estimate + (ULONG)(estimate / 8));
            return STATUS_BUFFER_TOO_SMALL;
        }
        memcpy( dst + dst_pos, decomp, decomp_len * sizeof(WCHAR) );
        if (origins)
        {
            unsigned int i;
            BOOL identity = len == decomp_len && !memcmp( decomp, src + src_pos, len * sizeof(WCHAR) );

            for (i = 0; i < decomp_len; ++i)
                origins[dst_pos + i] = src_pos + (identity ? i : 0);
        }
        dst_pos += decomp_len;
    }

    canonical_order_string( info, dst, dst_pos, origins );
    *dst_len = dst_pos;
    return STATUS_SUCCESS;
}

static inline unsigned int compose_hangul( unsigned int ch1, unsigned int ch2 )
{
    if (ch1 >= HANGUL_LBASE && ch1 < HANGUL_LBASE + HANGUL_LCOUNT)
    {
        int lindex = ch1 - HANGUL_LBASE;
        int vindex = ch2 - HANGUL_VBASE;
        if (vindex >= 0 && vindex < HANGUL_VCOUNT)
            return HANGUL_SBASE + (lindex * HANGUL_VCOUNT + vindex) * HANGUL_TCOUNT;
    }
    if (ch1 >= HANGUL_SBASE && ch1 < HANGUL_SBASE + HANGUL_SCOUNT)
    {
        int sindex = ch1 - HANGUL_SBASE;
        if (!(sindex % HANGUL_TCOUNT))
        {
            int tindex = ch2 - HANGUL_TBASE;
            if (tindex > 0 && tindex < HANGUL_TCOUNT) return ch1 + tindex;
        }
    }
    return 0;
}

static inline unsigned int compose_chars( const struct norm_table *info, unsigned int ch1, unsigned int ch2 )
{
    const USHORT *table = (const USHORT *)info + info->comp_hash;
    const WCHAR *chars = (const USHORT *)info + info->comp_seq;
    unsigned int hash, start, end, i, len, ch[3];

    hash = (ch1 + 95 * ch2) % info->comp_size;
    start = table[hash];
    end = table[hash + 1];
    while (start < end)
    {
        for (i = 0; i < 3; i++, start += len) len = get_utf16( chars + start, end - start, ch + i );
        if (ch[0] == ch1 && ch[1] == ch2) return ch[2];
    }
    return 0;
}

static inline unsigned int compose_string( const struct norm_table *info, WCHAR *str, unsigned int srclen,
                                          LONG *origins )
{
    unsigned int i, ch, comp, len, start_ch = 0, last_starter = srclen;
    BYTE class, prev_class = 0;

    for (i = 0; i < srclen; i += len)
    {
        if (!(len = get_utf16( str + i, srclen - i, &ch ))) return 0;
        class = get_combining_class( info, ch );
        if (last_starter == srclen || (prev_class && prev_class >= class) ||
            (!(comp = compose_hangul( start_ch, ch )) &&
             !(comp = compose_chars( info, start_ch, ch ))))
        {
            if (!class)
            {
                last_starter = i;
                start_ch = ch;
            }
            prev_class = class;
        }
        else
        {
            int comp_len = 1 + (comp >= 0x10000);
            int start_len = 1 + (start_ch >= 0x10000);

            if (comp_len != start_len)
                memmove( str + last_starter + comp_len, str + last_starter + start_len,
                         (i - (last_starter + start_len)) * sizeof(WCHAR) );
            memmove( str + i + comp_len - start_len, str + i + len, (srclen - i - len) * sizeof(WCHAR) );
            if (origins)
            {
                LONG positions[2];

                positions[0] = origins[last_starter];
                positions[1] = origins[last_starter + start_len - 1];
                if (comp_len != start_len)
                    memmove( origins + last_starter + comp_len, origins + last_starter + start_len,
                             (i - (last_starter + start_len)) * sizeof(*origins) );
                memmove( origins + i + comp_len - start_len, origins + i + len,
                         (srclen - i - len) * sizeof(*origins) );
                memcpy( origins + last_starter, positions, comp_len * sizeof(*origins) );
            }
            srclen += comp_len - start_len - len;
            start_ch = comp;
            i = last_starter;
            len = comp_len;
            prev_class = 0;
            put_utf16( str + i, comp );
        }
    }
    return srclen;
}

static NTSTATUS composition_prefix_status( const struct norm_table *info, const WCHAR *src,
                                          LONG src_len, WCHAR *buffer, const struct norm_progress *progress,
                                          LONG capacity, LONG *length )
{
    SIZE_T count = progress->output;
    LONG *origins, prefix_len = progress->output;
    NTSTATUS status;

    if (count > ~(SIZE_T)0 / sizeof(*origins)) return STATUS_NO_MEMORY;
    origins = RtlAllocateHeap( RtlGetProcessHeap(), 0, count * sizeof(*origins) );
    if (!origins) return STATUS_NO_MEMORY;
    status = decompose_string( info, src, progress->source, buffer, &prefix_len, NULL, origins );
    if (!status)
    {
        prefix_len = compose_string( info, buffer, prefix_len, origins );
        status = STATUS_NO_UNICODE_TRANSLATION;
        if (prefix_len > capacity)
        {
            LONG estimate = estimate_normalized_length( info, src_len - origins[capacity] );

            *length = (LONG)((ULONG)capacity + (ULONG)estimate + (ULONG)(estimate / 8));
            status = STATUS_BUFFER_TOO_SMALL;
        }
    }
    RtlFreeHeap( RtlGetProcessHeap(), 0, origins );
    return status;
}

static NTSTATUS load_norm_table( ULONG form, const struct norm_table **info )
{
    unsigned int i;
    USHORT *data, *tables;
    SIZE_T size;
    NTSTATUS status;

    if (!form) return STATUS_INVALID_PARAMETER;
    if (form >= RTL_NUMBER_OF(norm_tables)) return STATUS_OBJECT_NAME_NOT_FOUND;

    if (!norm_tables[form])
    {
        if ((status = NtGetNlsSectionPtr( NLS_SECTION_NORMALIZE, form, NULL, (void **)&data, &size )))
            return status;

        if (size <= 0x44) goto invalid;
        if (data[0x14] != form) goto invalid;
        tables = data + 0x1a;
        for (i = 0; i < 8; i++)
        {
            if (tables[i] > size / sizeof(USHORT)) goto invalid;
            if (i && tables[i] < tables[i-1]) goto invalid;
        }

        if (InterlockedCompareExchangePointer( (void **)&norm_tables[form], data, NULL ))
            NtUnmapViewOfSection( NtCurrentProcess(), data );
    }
    *info = norm_tables[form];
    return STATUS_SUCCESS;

invalid:
    NtUnmapViewOfSection( NtCurrentProcess(), data );
    return STATUS_INVALID_PARAMETER;
}

NTSTATUS NTAPI RtlIsNormalizedString( ULONG form, const WCHAR *str, LONG len, BOOLEAN *res )
{
    const struct norm_table *info;
    NTSTATUS status;
    BYTE props, class, last_class = 0;
    unsigned int ch;
    int i, r, result = 1;

    if ((status = load_norm_table( form, &info ))) return status;

    if (len == -1) len = wcslen( str );

    for (i = 0; i < len && result; i += r)
    {
        if (!(r = get_utf16( str + i, len - i, &ch ))) return STATUS_NO_UNICODE_TRANSLATION;
        if (info->comp_size)
        {
            if ((ch >= HANGUL_VBASE && ch < HANGUL_VBASE + HANGUL_VCOUNT) ||
                (ch >= HANGUL_TBASE && ch < HANGUL_TBASE + HANGUL_TCOUNT))
            {
                result = -1;
                continue;
            }
        }
        else if (ch >= HANGUL_SBASE && ch < HANGUL_SBASE + HANGUL_SCOUNT)
        {
            result = 0;
            break;
        }
        props = get_char_props( info, ch );
        class = props & 0x3f;
        if (class == 0x3f)
        {
            last_class = 0;
            if (props == 0xbf) result = 0;
            else if (props == 0xff)
            {

                if (ch >= HANGUL_LBASE && ch < HANGUL_LBASE + 0x100) continue;
                if (ch >= HANGUL_SBASE && ch < HANGUL_SBASE + 0x2c00) continue;

                if (!ch && i == len - 1) continue;
                return STATUS_NO_UNICODE_TRANSLATION;
            }
        }
        else if (props & 0x80)
        {
            if ((props & 0xc0) == 0xc0) result = -1;
            if (class && class < last_class) result = 0;
            last_class = class;
        }
        else last_class = 0;
    }

    if (result == -1)
    {
        LONG dstlen = len * 4;
        NTSTATUS status;
        WCHAR *buffer = RtlAllocateHeap( RtlGetProcessHeap(), 0, dstlen * sizeof(WCHAR) );
        if (!buffer) return STATUS_NO_MEMORY;
        status = RtlNormalizeString( form, str, len, buffer, &dstlen );
        result = !status && (dstlen == len) && !wcsncmp( buffer, str, len );
        RtlFreeHeap( RtlGetProcessHeap(), 0, buffer );
    }
    *res = result;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI RtlNormalizeString( ULONG form, const WCHAR *src, LONG src_len, WCHAR *dst, LONG *dst_len )
{
    LONG buf_len;
    struct norm_progress progress;
    WCHAR *buf = NULL;
    const struct norm_table *info;
    NTSTATUS status = STATUS_SUCCESS;

    if ((status = load_norm_table( form, &info ))) return status;

    if (src_len == -1) src_len = wcslen(src) + 1;

    if (!*dst_len)
    {
        *dst_len = estimate_normalized_length( info, src_len );
        return STATUS_SUCCESS;
    }
    if (!src_len)
    {
        *dst_len = 0;
        return STATUS_SUCCESS;
    }

    if (!info->comp_size) return decompose_string( info, src, src_len, dst, dst_len, NULL, NULL );

    buf_len = (LONG)((ULONG)src_len * 4);
    for (;;)
    {
        LONG capacity = buf_len;

        if (buf_len <= 0 || (SIZE_T)buf_len > ~(SIZE_T)0 / sizeof(WCHAR)) return STATUS_NO_MEMORY;
        buf = RtlAllocateHeap( RtlGetProcessHeap(), 0, buf_len * sizeof(WCHAR) );
        if (!buf) return STATUS_NO_MEMORY;
        status = decompose_string( info, src, src_len, buf, &buf_len, &progress, NULL );
        if (status != STATUS_BUFFER_TOO_SMALL) break;
        RtlFreeHeap( RtlGetProcessHeap(), 0, buf );
        if (buf_len <= capacity) return STATUS_NO_MEMORY;
    }
    if (!status)
    {
        buf_len = compose_string( info, buf, buf_len, NULL );
        if (*dst_len >= buf_len) memcpy( dst, buf, buf_len * sizeof(WCHAR) );
        else status = STATUS_BUFFER_TOO_SMALL;
    }
    else if (status == STATUS_NO_UNICODE_TRANSLATION && *dst_len > 0 && progress.output > *dst_len)
        status = composition_prefix_status( info, src, src_len, buf, &progress, *dst_len, &buf_len );
    RtlFreeHeap( RtlGetProcessHeap(), 0, buf );
    *dst_len = buf_len;
    return status;
}

enum { BASE = 36, TMIN = 1, TMAX = 26, SKEW = 38, DAMP = 700 };

static BOOLEAN check_invalid_chars( const struct norm_table *info, ULONG flags,
                                    const unsigned int *buffer, int len )
{
    int i;

    for (i = 0; i < len; i++)
    {
        switch (buffer[i])
        {
        case 0x200c:
        case 0x200d:
            if (!i || get_combining_class( info, buffer[i - 1] ) != 9) return TRUE;
            break;
        case 0x2260:
        case 0x226e:
        case 0x226f:
            if (flags & IDN_USE_STD3_ASCII_RULES) return TRUE;
            break;
        }
        switch (get_char_props( info, buffer[i] ))
        {
        case 0xbf:
            return TRUE;
        case 0xff:
            if (buffer[i] >= HANGUL_SBASE && buffer[i] < HANGUL_SBASE + 0x2c00) break;
            return TRUE;
        case 0x7f:
            if (!(flags & IDN_ALLOW_UNASSIGNED)) return TRUE;
            break;
        }
    }

    if ((flags & IDN_USE_STD3_ASCII_RULES) && len && (buffer[0] == '-' || buffer[len - 1] == '-'))
        return TRUE;

    return FALSE;
}

NTSTATUS NTAPI RtlIdnToNameprepUnicode( ULONG flags, const WCHAR *src, LONG srclen, WCHAR *dst, LONG *dstlen )
{
    const struct norm_table *info;
    unsigned int ch;
    NTSTATUS status;
    WCHAR buf[256];
    LONG buflen = RTL_NUMBER_OF(buf);
    int i, start, len;

    if (flags & ~(IDN_ALLOW_UNASSIGNED | IDN_USE_STD3_ASCII_RULES)) return STATUS_INVALID_PARAMETER;
    if (!src || srclen < -1) return STATUS_INVALID_PARAMETER;

    if ((status = load_norm_table( NLS_NORMALIZATION_IDN, &info ))) return status;

    if (srclen == -1) srclen = wcslen(src) + 1;

    for (i = 0; i < srclen; i++) if (src[i] < 0x20 || src[i] >= 0x7f) break;

    if (i == srclen || (i == srclen - 1 && !src[i]))
    {
        if (srclen > buflen) return STATUS_INVALID_IDN_NORMALIZATION;
        memcpy( buf, src, srclen * sizeof(WCHAR) );
        buflen = srclen;
    }
    else if ((status = RtlNormalizeString( NLS_NORMALIZATION_IDN, src, srclen, buf, &buflen )))
    {
        if (status == STATUS_NO_UNICODE_TRANSLATION) status = STATUS_INVALID_IDN_NORMALIZATION;
        return status;
    }

    for (i = start = 0; i < buflen; i += len)
    {
        if (!(len = get_utf16( buf + i, buflen - i, &ch ))) break;
        if (!ch) break;
        if (ch == '.')
        {
            if (start == i) return STATUS_INVALID_IDN_NORMALIZATION;
            if (i - start > 63) return STATUS_INVALID_IDN_NORMALIZATION;
            if ((flags & IDN_USE_STD3_ASCII_RULES) && (buf[start] == '-' || buf[i-1] == '-'))
                return STATUS_INVALID_IDN_NORMALIZATION;
            start = i + 1;
            continue;
        }
        if (flags & IDN_USE_STD3_ASCII_RULES)
        {
            if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                (ch >= '0' && ch <= '9') || ch == '-') continue;
            return STATUS_INVALID_IDN_NORMALIZATION;
        }
        if (!(flags & IDN_ALLOW_UNASSIGNED))
        {
            if (get_char_props( info, ch ) == 0x7f) return STATUS_INVALID_IDN_NORMALIZATION;
        }
    }
    if (!i || i - start > 63) return STATUS_INVALID_IDN_NORMALIZATION;
    if ((flags & IDN_USE_STD3_ASCII_RULES) && (buf[start] == '-' || buf[i-1] == '-'))
        return STATUS_INVALID_IDN_NORMALIZATION;

    if (*dstlen)
    {
        if (buflen <= *dstlen) memcpy( dst, buf, buflen * sizeof(WCHAR) );
        else status = STATUS_BUFFER_TOO_SMALL;
    }
    *dstlen = buflen;
    return status;
}

NTSTATUS NTAPI RtlIdnToAscii( ULONG flags, const WCHAR *src, LONG srclen, WCHAR *dst, LONG *dstlen )
{
    static const WCHAR prefixW[] = {'x','n','-','-'};
    const struct norm_table *info;
    NTSTATUS status;
    WCHAR normstr[256], res[256];
    unsigned int ch, buffer[64];
    LONG normlen = RTL_NUMBER_OF(normstr);
    int i, len, start, end, out_label, out = 0;

    if ((status = load_norm_table( NLS_NORMALIZATION_IDN, &info ))) return status;

    if ((status = RtlIdnToNameprepUnicode( flags, src, srclen, normstr, &normlen ))) return status;

    for (start = 0; start < normlen; start = end + 1)
    {
        int n = 0x80, bias = 72, delta = 0, b = 0, h, buflen = 0;

        out_label = out;
        for (i = start; i < normlen; i += len)
        {
            if (!(len = get_utf16( normstr + i, normlen - i, &ch ))) break;
            if (!ch || ch == '.') break;
            if (ch < 0x80) b++;
            buffer[buflen++] = ch;
        }
        end = i;

        if (b == end - start)
        {
            if (end < normlen) b++;
            if (out + b > (int)RTL_NUMBER_OF(res)) return STATUS_INVALID_IDN_NORMALIZATION;
            memcpy( res + out, normstr + start, b * sizeof(WCHAR) );
            out += b;
            continue;
        }

        if (buflen >= 4 && buffer[2] == '-' && buffer[3] == '-') return STATUS_INVALID_IDN_NORMALIZATION;
        if (check_invalid_chars( info, flags, buffer, buflen )) return STATUS_INVALID_IDN_NORMALIZATION;

        if (out + 5 + b > (int)RTL_NUMBER_OF(res)) return STATUS_INVALID_IDN_NORMALIZATION;
        memcpy( res + out, prefixW, sizeof(prefixW) );
        out += RTL_NUMBER_OF(prefixW);
        if (b)
        {
            for (i = start; i < end; i++) if (normstr[i] < 0x80) res[out++] = normstr[i];
            res[out++] = '-';
        }

        for (h = b; h < buflen; delta++, n++)
        {
            int m = 0x10ffff, q, k;

            for (i = 0; i < buflen; i++) if (buffer[i] >= (unsigned int)n && (unsigned int)m > buffer[i]) m = buffer[i];
            delta += (m - n) * (h + 1);
            n = m;

            for (i = 0; i < buflen; i++)
            {
                if (buffer[i] == (unsigned int)n)
                {
                    for (q = delta, k = BASE; ; k += BASE)
                    {
                        int t = k <= bias ? TMIN : k >= bias + TMAX ? TMAX : k - bias;
                        int disp = q < t ? q : t + (q - t) % (BASE - t);
                        if (out + 1 > (int)RTL_NUMBER_OF(res)) return STATUS_INVALID_IDN_NORMALIZATION;
                        res[out++] = disp <= 25 ? 'a' + disp : '0' + disp - 26;
                        if (q < t) break;
                        q = (q - t) / (BASE - t);
                    }
                    delta /= (h == b ? DAMP : 2);
                    delta += delta / (h + 1);
                    for (k = 0; delta > ((BASE - TMIN) * TMAX) / 2; k += BASE) delta /= BASE - TMIN;
                    bias = k + ((BASE - TMIN + 1) * delta) / (delta + SKEW);
                    delta = 0;
                    h++;
                }
                else if (buffer[i] < (unsigned int)n) delta++;
            }
        }

        if (out - out_label > 63) return STATUS_INVALID_IDN_NORMALIZATION;

        if (end < normlen)
        {
            if (out + 1 > (int)RTL_NUMBER_OF(res)) return STATUS_INVALID_IDN_NORMALIZATION;
            res[out++] = normstr[end];
        }
    }

    if (*dstlen)
    {
        if (out <= *dstlen) memcpy( dst, res, out * sizeof(WCHAR) );
        else status = STATUS_BUFFER_TOO_SMALL;
    }
    *dstlen = out;
    return status;
}

NTSTATUS NTAPI RtlIdnToUnicode( ULONG flags, const WCHAR *src, LONG srclen, WCHAR *dst, LONG *dstlen )
{
    const struct norm_table *info;
    int i, buflen, start, end, out_label, out = 0;
    NTSTATUS status;
    unsigned int buffer[64];
    WCHAR ch = 0;

    if (!src || srclen < -1) return STATUS_INVALID_PARAMETER;
    if (srclen == -1) srclen = wcslen( src ) + 1;

    if ((status = load_norm_table( NLS_NORMALIZATION_IDN, &info ))) return status;

    for (start = 0; start < srclen; )
    {
        int n = 0x80, bias = 72, pos = 0, old_pos, w, k, t, delim = 0, digit, delta;

        out_label = out;
        for (i = start; i < srclen; i++)
        {
            ch = src[i];
            if (ch > 0x7f || (i != srclen - 1 && !ch)) return STATUS_INVALID_IDN_NORMALIZATION;
            if (!ch || ch == '.') break;
            if (ch == '-') delim = i;

            if (!(flags & IDN_USE_STD3_ASCII_RULES)) continue;
            if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                (ch >= '0' && ch <= '9') || ch == '-')
                continue;
            return STATUS_INVALID_IDN_NORMALIZATION;
        }
        end = i;

        if (start == end && ch) return STATUS_INVALID_IDN_NORMALIZATION;

        if (end - start < 4 ||
            (src[start] != 'x' && src[start] != 'X') ||
            (src[start + 1] != 'n' && src[start + 1] != 'N') ||
            src[start + 2] != '-' || src[start + 3] != '-')
        {
            if (end - start > 63) return STATUS_INVALID_IDN_NORMALIZATION;

            if ((flags & IDN_USE_STD3_ASCII_RULES) && (src[start] == '-' || src[end - 1] == '-'))
                return STATUS_INVALID_IDN_NORMALIZATION;

            if (end < srclen) end++;
            if (*dstlen)
            {
                if (out + end - start <= *dstlen)
                    memcpy( dst + out, src + start, (end - start) * sizeof(WCHAR));
                else return STATUS_BUFFER_TOO_SMALL;
            }
            out += end - start;
            start = end;
            continue;
        }

        if (delim == start + 3) delim++;
        buflen = 0;
        for (i = start + 4; i < delim && buflen < (int)RTL_NUMBER_OF(buffer); i++) buffer[buflen++] = src[i];
        if (buflen) i++;
        while (i < end)
        {
            old_pos = pos;
            w = 1;
            for (k = BASE; ; k += BASE)
            {
                if (i >= end) return STATUS_INVALID_IDN_NORMALIZATION;
                ch = src[i++];
                if (ch >= 'a' && ch <= 'z') digit = ch - 'a';
                else if (ch >= 'A' && ch <= 'Z') digit = ch - 'A';
                else if (ch >= '0' && ch <= '9') digit = ch - '0' + 26;
                else return STATUS_INVALID_IDN_NORMALIZATION;
                pos += digit * w;
                t = k <= bias ? TMIN : k >= bias + TMAX ? TMAX : k - bias;
                if (digit < t) break;
                w *= BASE - t;
            }

            delta = (pos - old_pos) / (!old_pos ? DAMP : 2);
            delta += delta / (buflen + 1);
            for (k = 0; delta > ((BASE - TMIN) * TMAX) / 2; k += BASE) delta /= BASE - TMIN;
            bias = k + ((BASE - TMIN + 1) * delta) / (delta + SKEW);
            n += pos / (buflen + 1);
            pos %= buflen + 1;

            if (buflen >= (int)RTL_NUMBER_OF(buffer) - 1) return STATUS_INVALID_IDN_NORMALIZATION;
            memmove( buffer + pos + 1, buffer + pos, (buflen - pos) * sizeof(*buffer) );
            buffer[pos++] = n;
            buflen++;
        }

        if (check_invalid_chars( info, flags, buffer, buflen )) return STATUS_INVALID_IDN_NORMALIZATION;

        for (i = 0; i < buflen; i++)
        {
            int len = 1 + (buffer[i] >= 0x10000);
            if (*dstlen)
            {
                if (out + len <= *dstlen) put_utf16( dst + out, buffer[i] );
                else return STATUS_BUFFER_TOO_SMALL;
            }
            out += len;
        }

        if (out - out_label > 63) return STATUS_INVALID_IDN_NORMALIZATION;

        if (end < srclen)
        {
            if (*dstlen)
            {
                if (out + 1 <= *dstlen) dst[out] = src[end];
                else return STATUS_BUFFER_TOO_SMALL;
            }
            out++;
        }
        start = end + 1;
    }
    *dstlen = out;
    return STATUS_SUCCESS;
}
