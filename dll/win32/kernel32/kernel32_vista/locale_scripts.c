#include "k32_vista.h"

#include <string.h>
#include <wchar.h>
#include <wctype.h>

#define NDEBUG
#include <debug.h>

#ifndef GSS_ALLOW_INHERITED_COMMON
#define GSS_ALLOW_INHERITED_COMMON 0x1
#endif

#ifndef VS_ALLOW_LATIN
#define VS_ALLOW_LATIN 0x1
#endif

#define K32VISTA_MAX_SCRIPTS 32

static const WCHAR ScriptLatn[] = L"Latn";
static const WCHAR ScriptGrek[] = L"Grek";
static const WCHAR ScriptCyrl[] = L"Cyrl";
static const WCHAR ScriptArmn[] = L"Armn";
static const WCHAR ScriptHebr[] = L"Hebr";
static const WCHAR ScriptArab[] = L"Arab";
static const WCHAR ScriptSyrc[] = L"Syrc";
static const WCHAR ScriptThaa[] = L"Thaa";
static const WCHAR ScriptDeva[] = L"Deva";
static const WCHAR ScriptBeng[] = L"Beng";
static const WCHAR ScriptGuru[] = L"Guru";
static const WCHAR ScriptGujr[] = L"Gujr";
static const WCHAR ScriptOrya[] = L"Orya";
static const WCHAR ScriptTaml[] = L"Taml";
static const WCHAR ScriptTelu[] = L"Telu";
static const WCHAR ScriptKnda[] = L"Knda";
static const WCHAR ScriptMlym[] = L"Mlym";
static const WCHAR ScriptSinh[] = L"Sinh";
static const WCHAR ScriptThai[] = L"Thai";
static const WCHAR ScriptLaoo[] = L"Laoo";
static const WCHAR ScriptTibt[] = L"Tibt";
static const WCHAR ScriptMymr[] = L"Mymr";
static const WCHAR ScriptGeor[] = L"Geor";
static const WCHAR ScriptCher[] = L"Cher";
static const WCHAR ScriptEthi[] = L"Ethi";
static const WCHAR ScriptMong[] = L"Mong";
static const WCHAR ScriptKhmr[] = L"Khmr";
static const WCHAR ScriptHira[] = L"Hira";
static const WCHAR ScriptKana[] = L"Kana";
static const WCHAR ScriptHans[] = L"Hans";
static const WCHAR ScriptHang[] = L"Hang";
static const WCHAR ScriptBopo[] = L"Bopo";
static const WCHAR ScriptCans[] = L"Cans";
static const WCHAR ScriptOgam[] = L"Ogam";
static const WCHAR ScriptRunr[] = L"Runr";
static const WCHAR ScriptCommon[] = L"Zyyy";
static const WCHAR ScriptInherited[] = L"Zinh";

typedef struct _K32VISTA_SCRIPT_RANGE
{
    WCHAR Start;
    WCHAR End;
    const WCHAR *Code;
} K32VISTA_SCRIPT_RANGE, *PK32VISTA_SCRIPT_RANGE;

static const K32VISTA_SCRIPT_RANGE K32VistaScriptRanges[] =
{
    {0x0041, 0x024F, ScriptLatn},
    {0x0250, 0x02AF, ScriptLatn},
    {0x1E00, 0x1EFF, ScriptLatn},
    {0x0370, 0x03FF, ScriptGrek},
    {0x1F00, 0x1FFF, ScriptGrek},
    {0x0400, 0x052F, ScriptCyrl},
    {0x0530, 0x058F, ScriptArmn},
    {0x0590, 0x05FF, ScriptHebr},
    {0x0600, 0x06FF, ScriptArab},
    {0x0750, 0x077F, ScriptArab},
    {0x0700, 0x074F, ScriptSyrc},
    {0x0780, 0x07BF, ScriptThaa},
    {0x0900, 0x097F, ScriptDeva},
    {0x0980, 0x09FF, ScriptBeng},
    {0x0A00, 0x0A7F, ScriptGuru},
    {0x0A80, 0x0AFF, ScriptGujr},
    {0x0B00, 0x0B7F, ScriptOrya},
    {0x0B80, 0x0BFF, ScriptTaml},
    {0x0C00, 0x0C7F, ScriptTelu},
    {0x0C80, 0x0CFF, ScriptKnda},
    {0x0D00, 0x0D7F, ScriptMlym},
    {0x0D80, 0x0DFF, ScriptSinh},
    {0x0E00, 0x0E7F, ScriptThai},
    {0x0E80, 0x0EFF, ScriptLaoo},
    {0x0F00, 0x0FFF, ScriptTibt},
    {0x1000, 0x109F, ScriptMymr},
    {0x10A0, 0x10FF, ScriptGeor},
    {0x2D00, 0x2D2F, ScriptGeor},
    {0x1200, 0x137F, ScriptEthi},
    {0x13A0, 0x13FF, ScriptCher},
    {0xAB70, 0xABBF, ScriptCher},
    {0x1400, 0x167F, ScriptCans},
    {0x1680, 0x169F, ScriptOgam},
    {0x16A0, 0x16FF, ScriptRunr},
    {0x1780, 0x17FF, ScriptKhmr},
    {0x1800, 0x18AF, ScriptMong},
    {0x3040, 0x309F, ScriptHira},
    {0x30A0, 0x30FF, ScriptKana},
    {0x31F0, 0x31FF, ScriptKana},
    {0xAC00, 0xD7A3, ScriptHang},
    {0x1100, 0x11FF, ScriptHang},
    {0xA960, 0xA97F, ScriptHang},
    {0xD7B0, 0xD7FF, ScriptHang},
    {0x3100, 0x312F, ScriptBopo},
    {0x3400, 0x4DBF, ScriptHans},
    {0x4E00, 0x9FFF, ScriptHans},
    {0xF900, 0xFAFF, ScriptHans}
};

static const WCHAR *K32VistaKnownScripts[] =
{
    ScriptLatn, ScriptGrek, ScriptCyrl, ScriptArmn, ScriptHebr, ScriptArab,
    ScriptSyrc, ScriptThaa, ScriptDeva, ScriptBeng, ScriptGuru, ScriptGujr,
    ScriptOrya, ScriptTaml, ScriptTelu, ScriptKnda, ScriptMlym, ScriptSinh,
    ScriptThai, ScriptLaoo, ScriptTibt, ScriptMymr, ScriptGeor, ScriptCher,
    ScriptEthi, ScriptMong, ScriptKhmr, ScriptHira, ScriptKana, ScriptHans,
    ScriptHang, ScriptBopo, ScriptCans, ScriptOgam, ScriptRunr, ScriptCommon,
    ScriptInherited
};

static WCHAR
K32VistaToUpper(WCHAR Ch)
{
    if (Ch >= L'a' && Ch <= L'z')
        return Ch - L'a' + L'A';
    return towupper(Ch);
}

static WCHAR
K32VistaToLower(WCHAR Ch)
{
    if (Ch >= L'A' && Ch <= L'Z')
        return Ch - L'A' + L'a';
    return towlower(Ch);
}

static const WCHAR *
K32VistaFindKnownScript(const WCHAR Tag[5])
{
    SIZE_T Index;

    for (Index = 0; Index < RTL_NUMBER_OF(K32VistaKnownScripts); ++Index)
    {
        if (wcscmp(K32VistaKnownScripts[Index], Tag) == 0)
            return K32VistaKnownScripts[Index];
    }

    return NULL;
}

static BOOL
K32VistaNormalizeScriptTag(const WCHAR *Start,
                           INT Length,
                           WCHAR Tag[5])
{
    INT Begin = 0;
    INT End = Length;

    while (Begin < End && iswspace(Start[Begin]))
        Begin++;

    while (End > Begin && iswspace(Start[End - 1]))
        End--;

    if (End - Begin != 4)
        return FALSE;

    Tag[0] = K32VistaToUpper(Start[Begin]);
    Tag[1] = K32VistaToLower(Start[Begin + 1]);
    Tag[2] = K32VistaToLower(Start[Begin + 2]);
    Tag[3] = K32VistaToLower(Start[Begin + 3]);
    Tag[4] = L'\0';
    return TRUE;
}

static const WCHAR *
K32VistaLookupScriptFromSegment(const WCHAR *Start,
                                INT Length)
{
    WCHAR Tag[5];

    if (!K32VistaNormalizeScriptTag(Start, Length, Tag))
        return NULL;

    return K32VistaFindKnownScript(Tag);
}

static BOOL
K32VistaAddScript(const WCHAR *Script,
                  const WCHAR **List,
                  SIZE_T *Count)
{
    SIZE_T Index;

    for (Index = 0; Index < *Count; ++Index)
    {
        if (wcscmp(List[Index], Script) == 0)
            return TRUE;
    }

    if (*Count >= K32VISTA_MAX_SCRIPTS)
    {
        DPRINT1("GetStringScripts: reached script tracking capacity\n");
        return FALSE;
    }

    List[*Count] = Script;
    (*Count)++;
    return TRUE;
}

static const WCHAR *
K32VistaGetScriptForCharacter(WCHAR Ch)
{
    SIZE_T Index;

    if ((Ch >= 0x0300 && Ch <= 0x036F) ||
        (Ch >= 0x1AB0 && Ch <= 0x1AFF) ||
        (Ch >= 0x1DC0 && Ch <= 0x1DFF) ||
        (Ch >= 0xFE20 && Ch <= 0xFE2F))
    {
        return ScriptInherited;
    }

    for (Index = 0; Index < RTL_NUMBER_OF(K32VistaScriptRanges); ++Index)
    {
        if (Ch >= K32VistaScriptRanges[Index].Start &&
            Ch <= K32VistaScriptRanges[Index].End)
        {
            return K32VistaScriptRanges[Index].Code;
        }
    }

    return ScriptCommon;
}

static INT
K32VistaWriteScriptBuffer(const WCHAR **Scripts,
                          SIZE_T ScriptCount,
                          LPWSTR Output,
                          INT OutputCch)
{
    INT Required = 1; /* null terminator */
    SIZE_T Index;

    if (ScriptCount > 0)
        Required += (INT)(ScriptCount * 4 + (ScriptCount - 1));

    if (!Output || OutputCch == 0)
        return Required;

    if (OutputCch < Required)
    {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return Required;
    }

    if (ScriptCount == 0)
    {
        Output[0] = L'\0';
        return 1;
    }

    INT Position = 0;
    for (Index = 0; Index < ScriptCount; ++Index)
    {
        if (Index > 0)
            Output[Position++] = L';';

        memcpy(&Output[Position], Scripts[Index], 4 * sizeof(WCHAR));
        Position += 4;
    }

    Output[Position] = L'\0';
    return Required;
}

static BOOL
K32VistaIsScriptAllowed(const WCHAR *Script,
                        const WCHAR **AllowedScripts,
                        SIZE_T AllowedCount,
                        DWORD Flags)
{
    SIZE_T Index;

    if (Script == ScriptCommon || Script == ScriptInherited)
    {
        if (!(Flags & GSS_ALLOW_INHERITED_COMMON))
            return TRUE;
    }

    if ((Flags & VS_ALLOW_LATIN) && Script == ScriptLatn)
        return TRUE;

    for (Index = 0; Index < AllowedCount; ++Index)
    {
        if (wcscmp(AllowedScripts[Index], Script) == 0)
            return TRUE;
    }

    return FALSE;
}

INT
WINAPI
GetStringScripts(DWORD dwFlags,
                 LPCWSTR lpString,
                 INT cchString,
                 LPWSTR lpScripts,
                 INT cchScripts)
{
    const WCHAR *Scripts[K32VISTA_MAX_SCRIPTS];
    SIZE_T ScriptCount = 0;
    INT Index;
    WCHAR Current;

    if (!lpString || cchString < -1)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }

    if (cchString < 0)
        cchString = wcslen(lpString);

    for (Index = 0; Index < cchString; ++Index)
    {
        Current = lpString[Index];
        if (Current == L'\0')
            break;

        const WCHAR *Script = K32VistaGetScriptForCharacter(Current);
        if (!(dwFlags & GSS_ALLOW_INHERITED_COMMON) &&
            (Script == ScriptCommon || Script == ScriptInherited))
        {
            continue;
        }

        if (!K32VistaAddScript(Script, Scripts, &ScriptCount))
        {
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            return 0;
        }
    }

    return K32VistaWriteScriptBuffer(Scripts, ScriptCount, lpScripts, cchScripts);
}

static BOOL
K32VistaParseScriptList(LPCWSTR lpScripts,
                        INT cchScripts,
                        const WCHAR **Scripts,
                        SIZE_T *ScriptCount)
{
    INT Index = 0;
    INT SegmentStart = 0;
    INT EffectiveLength;
    const WCHAR *Script;

    if (cchScripts < 0)
        cchScripts = wcslen(lpScripts);

    while (Index <= cchScripts)
    {
        if (Index == cchScripts || lpScripts[Index] == L';' || lpScripts[Index] == L'\0')
        {
            EffectiveLength = Index - SegmentStart;
            if (EffectiveLength > 0)
            {
                Script = K32VistaLookupScriptFromSegment(lpScripts + SegmentStart, EffectiveLength);
                if (!Script)
                {
                    return FALSE;
                }

                if (!K32VistaAddScript(Script, Scripts, ScriptCount))
                    return FALSE;
            }

            SegmentStart = Index + 1;
        }

        Index++;
    }

    return TRUE;
}

BOOL
WINAPI
VerifyScripts(DWORD dwFlags,
              LPCWSTR lpString,
              INT cchString,
              LPCWSTR lpScripts,
              INT cchScripts)
{
    const WCHAR *Allowed[K32VISTA_MAX_SCRIPTS];
    SIZE_T AllowedCount = 0;
    INT Index;
    const WCHAR *Script;

    if (!lpString || !lpScripts || cchString < -1 || cchScripts < -1)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if (!K32VistaParseScriptList(lpScripts, cchScripts, Allowed, &AllowedCount))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if (cchString < 0)
        cchString = wcslen(lpString);

    for (Index = 0; Index < cchString; ++Index)
    {
        WCHAR Ch = lpString[Index];
        if (Ch == L'\0')
            break;

        Script = K32VistaGetScriptForCharacter(Ch);
        if (!K32VistaIsScriptAllowed(Script, Allowed, AllowedCount, dwFlags))
        {
            SetLastError(ERROR_INVALID_NAME);
            return FALSE;
        }
    }

    return TRUE;
}
