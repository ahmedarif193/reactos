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

static inline void canonical_order_substring( const struct norm_table *info, WCHAR *str, unsigned int len )
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
                swapped = TRUE;
                i += len2 - len1;
            }
        }
    } while (swapped);
}

static inline void canonical_order_string( const struct norm_table *info, WCHAR *str, unsigned int len )
{
    unsigned int ch, i, r, next = 0;

    for (i = 0; i < len; i += r)
    {
        if (!(r = get_utf16( str + i, len - i, &ch ))) return;
        if (i && !get_combining_class( info, ch ))
        {
            if (i > next + 1)
                canonical_order_substring( info, str + next, i - next );
            next = i + r;
        }
    }
    if (i > next + 1) canonical_order_substring( info, str + next, i - next );
}

static inline NTSTATUS decompose_string( const struct norm_table *info, const WCHAR *src, int src_len,
                                         WCHAR *dst, LONG *dst_len )
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
            *dst_len = src_pos;
            return STATUS_NO_UNICODE_TRANSLATION;
        }
        if (dst_pos + decomp_len > *dst_len)
        {
            *dst_len += (src_len - src_pos) * info->len_factor;
            return STATUS_BUFFER_TOO_SMALL;
        }
        memcpy( dst + dst_pos, decomp, decomp_len * sizeof(WCHAR) );
        dst_pos += decomp_len;
    }

    canonical_order_string( info, dst, dst_pos );
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

static inline unsigned int compose_string( const struct norm_table *info, WCHAR *str, unsigned int srclen )
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
    WCHAR *buf = NULL;
    const struct norm_table *info;
    NTSTATUS status = STATUS_SUCCESS;

    if ((status = load_norm_table( form, &info ))) return status;

    if (src_len == -1) src_len = wcslen(src) + 1;

    if (!*dst_len)
    {
        *dst_len = src_len * info->len_factor;
        if (*dst_len > 64) *dst_len = max( 64, src_len + src_len / 8 );
        return STATUS_SUCCESS;
    }
    if (!src_len)
    {
        *dst_len = 0;
        return STATUS_SUCCESS;
    }

    if (!info->comp_size) return decompose_string( info, src, src_len, dst, dst_len );

    buf_len = src_len * 4;
    for (;;)
    {
        buf = RtlAllocateHeap( RtlGetProcessHeap(), 0, buf_len * sizeof(WCHAR) );
        if (!buf) return STATUS_NO_MEMORY;
        status = decompose_string( info, src, src_len, buf, &buf_len );
        if (status != STATUS_BUFFER_TOO_SMALL) break;
        RtlFreeHeap( RtlGetProcessHeap(), 0, buf );
    }
    if (!status)
    {
        buf_len = compose_string( info, buf, buf_len );
        if (*dst_len >= buf_len) memcpy( dst, buf, buf_len * sizeof(WCHAR) );
        else status = STATUS_BUFFER_TOO_SMALL;
    }
    RtlFreeHeap( RtlGetProcessHeap(), 0, buf );
    *dst_len = buf_len;
    return status;
}
