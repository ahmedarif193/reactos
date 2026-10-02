/*
 * PROJECT:     LiberNT Runtime Library
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Side-by-side manifest validation
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <rtl.h>

#define NDEBUG
#include <debug.h>

typedef struct _SXM_STRING
{
    PCWSTR Buffer;
    ULONG Length;
} SXM_STRING;

typedef struct _SXM_ATTRIBUTE
{
    SXM_STRING Prefix;
    SXM_STRING Name;
    SXM_STRING Value;
} SXM_ATTRIBUTE;

typedef struct _SXM_NAMESPACE
{
    SXM_STRING Prefix;
    SXM_STRING Uri;
} SXM_NAMESPACE;

typedef enum _SXM_TOKEN
{
    SxmTokenAttribute,
    SxmTokenOpen,
    SxmTokenEmpty,
    SxmTokenError
} SXM_TOKEN;

typedef enum _SXM_SET
{
    SxmSetNone,
    SxmSetFile,
    SxmSetClass,
    SxmSetProgId,
    SxmSetWindowClass,
    SxmSetTypeLibrary,
    SxmSetInterface,
    SxmSetSurrogate,
    SxmSetActivatableClass,
    SxmSetSetting,
    SxmSetDependency
} SXM_SET;

typedef enum _SXM_VALUE
{
    SxmValueAny,
    SxmValueNonEmpty,
    SxmValueManifestVersion,
    SxmValueVersion,
    SxmValueGuid,
    SxmValueDecimal,
    SxmValueBoolean,
    SxmValueTrueFalse,
    SxmValueArchitecture,
    SxmValueLanguage,
    SxmValueLanguageReference,
    SxmValueTypeWin32,
    SxmValueHashAlgorithm,
    SxmValueHash,
    SxmValueFileName,
    SxmValueThreadingModel,
    SxmValueWinRtThreadingModel,
    SxmValueMiscStatus,
    SxmValueTypeLibraryFlags,
    SxmValueTypeLibraryVersion,
    SxmValueExecutionLevel
} SXM_VALUE;

typedef enum _SXM_KIND
{
    SxmKindReject,
    SxmKindDocument,
    SxmKindSkip,
    SxmKindAssembly,
    SxmKindIdentity,
    SxmKindDescription,
    SxmKindNoInherit,
    SxmKindNoInheritable,
    SxmKindFile,
    SxmKindComClass,
    SxmKindProgId,
    SxmKindTypeLibrary,
    SxmKindProxyStub,
    SxmKindWindowClass,
    SxmKindHash,
    SxmKindActivatableClass,
    SxmKindExternalProxyStub,
    SxmKindClrClass,
    SxmKindClrSurrogate,
    SxmKindDependency,
    SxmKindDependentAssembly,
    SxmKindDependentIdentity,
    SxmKindTrustInfo,
    SxmKindSecurity,
    SxmKindRequestedPrivileges,
    SxmKindExecutionLevel,
    SxmKindApplication,
    SxmKindWindowsSettings,
    SxmKindSetting,
    SxmKindCompatibility,
    SxmKindCompatibilityApplication,
    SxmKindSupportedOs,
    SxmKindMaxVersionTested
} SXM_KIND;

typedef enum _SXM_NAMESPACE_CLASS
{
    SxmNamespaceOther,
    SxmNamespaceAssembly,
    SxmNamespaceCompatibility,
    SxmNamespaceWinRt,
    SxmNamespaceSettings
} SXM_NAMESPACE_CLASS;

typedef struct _SXM_RULE
{
    PCWSTR Name;
    SXM_VALUE Value;
    BOOLEAN Required;
    SXM_SET Unique;
} SXM_RULE;

typedef struct _SXM_CHILD
{
    PCWSTR Name;
    SXM_KIND Kind;
} SXM_CHILD;

typedef struct _SXM_SEEN
{
    SXM_SET Set;
    SXM_STRING Key;
    SXM_STRING Scope;
    USHORT Version[4];
    BOOLEAN Implicit;
} SXM_SEEN;

typedef struct _SXM_FRAME
{
    SXM_KIND Kind;
    SXM_STRING QualifiedName;
    SXM_STRING Content;
    ULONG SavedNamespaces;
    BOOLEAN ContentSeen;
    BOOLEAN IdentitySeen;
    BOOLEAN NoInheritSeen;
    BOOLEAN NoInheritableSeen;
    BOOLEAN ChildSeen;
} SXM_FRAME;

typedef struct _SXM_STATE
{
    PCWSTR Current;
    PCWSTR End;
    SXM_NAMESPACE *Namespaces;
    ULONG NamespaceCount;
    ULONG NamespaceCapacity;
    SXM_SEEN *Seen;
    ULONG SeenCount;
    ULONG SeenCapacity;
    SXM_FRAME *Frames;
    ULONG FrameCount;
    ULONG FrameCapacity;
    BOOLEAN ApplicationManifest;
    BOOLEAN ExecutionLevelSeen;
    NTSTATUS Status;
} SXM_STATE;

static const SXM_RULE SxmAssemblyRules[] =
{
    { L"manifestVersion", SxmValueManifestVersion, TRUE, SxmSetNone },
    { NULL }
};

static const SXM_RULE SxmIdentityRules[] =
{
    { L"name", SxmValueNonEmpty, TRUE, SxmSetNone },
    { L"version", SxmValueVersion, TRUE, SxmSetNone },
    { L"type", SxmValueAny, FALSE, SxmSetNone },
    { L"processorArchitecture", SxmValueArchitecture, FALSE, SxmSetNone },
    { L"publicKeyToken", SxmValueAny, FALSE, SxmSetNone },
    { L"language", SxmValueLanguage, FALSE, SxmSetNone },
    { L"publicKey", SxmValueAny, FALSE, SxmSetNone },
    { NULL }
};

static const SXM_RULE SxmDependentIdentityRules[] =
{
    { L"name", SxmValueNonEmpty, TRUE, SxmSetNone },
    { L"version", SxmValueVersion, TRUE, SxmSetNone },
    { L"type", SxmValueTypeWin32, TRUE, SxmSetNone },
    { L"processorArchitecture", SxmValueArchitecture, FALSE, SxmSetNone },
    { L"publicKeyToken", SxmValueAny, FALSE, SxmSetNone },
    { L"language", SxmValueLanguageReference, FALSE, SxmSetNone },
    { L"publicKey", SxmValueAny, FALSE, SxmSetNone },
    { NULL }
};

static const SXM_RULE SxmFileRules[] =
{
    { L"name", SxmValueFileName, TRUE, SxmSetFile },
    { L"hash", SxmValueHash, FALSE, SxmSetNone },
    { L"hashalg", SxmValueHashAlgorithm, FALSE, SxmSetNone },
    { L"loadFrom", SxmValueAny, FALSE, SxmSetNone },
    { L"size", SxmValueDecimal, FALSE, SxmSetNone },
    { NULL }
};

static const SXM_RULE SxmComClassRules[] =
{
    { L"clsid", SxmValueGuid, TRUE, SxmSetClass },
    { L"threadingModel", SxmValueThreadingModel, FALSE, SxmSetNone },
    { L"progid", SxmValueAny, FALSE, SxmSetProgId },
    { L"tlbid", SxmValueGuid, FALSE, SxmSetNone },
    { L"description", SxmValueAny, FALSE, SxmSetNone },
    { L"miscStatus", SxmValueMiscStatus, FALSE, SxmSetNone },
    { L"miscStatusIcon", SxmValueMiscStatus, FALSE, SxmSetNone },
    { L"miscStatusContent", SxmValueMiscStatus, FALSE, SxmSetNone },
    { L"miscStatusThumbnail", SxmValueMiscStatus, FALSE, SxmSetNone },
    { L"miscStatusDocPrint", SxmValueMiscStatus, FALSE, SxmSetNone },
    { NULL }
};

static const SXM_RULE SxmTypeLibraryRules[] =
{
    { L"tlbid", SxmValueGuid, TRUE, SxmSetTypeLibrary },
    { L"version", SxmValueTypeLibraryVersion, TRUE, SxmSetNone },
    { L"helpdir", SxmValueAny, TRUE, SxmSetNone },
    { L"resourceid", SxmValueDecimal, FALSE, SxmSetNone },
    { L"flags", SxmValueTypeLibraryFlags, FALSE, SxmSetNone },
    { NULL }
};

static const SXM_RULE SxmProxyStubRules[] =
{
    { L"iid", SxmValueGuid, TRUE, SxmSetInterface },
    { L"name", SxmValueAny, FALSE, SxmSetNone },
    { L"tlbid", SxmValueGuid, FALSE, SxmSetNone },
    { L"baseInterface", SxmValueGuid, FALSE, SxmSetNone },
    { L"numMethods", SxmValueDecimal, FALSE, SxmSetNone },
    { L"proxyStubClsid32", SxmValueGuid, FALSE, SxmSetNone },
    { L"threadingModel", SxmValueAny, FALSE, SxmSetNone },
    { NULL }
};

static const SXM_RULE SxmExternalProxyStubRules[] =
{
    { L"iid", SxmValueGuid, TRUE, SxmSetInterface },
    { L"name", SxmValueAny, TRUE, SxmSetNone },
    { L"tlbid", SxmValueGuid, FALSE, SxmSetNone },
    { L"baseInterface", SxmValueGuid, FALSE, SxmSetNone },
    { L"numMethods", SxmValueDecimal, FALSE, SxmSetNone },
    { L"proxyStubClsid32", SxmValueGuid, FALSE, SxmSetNone },
    { NULL }
};

static const SXM_RULE SxmWindowClassRules[] =
{
    { L"versioned", SxmValueBoolean, FALSE, SxmSetNone },
    { NULL }
};

static const SXM_RULE SxmClrClassRules[] =
{
    { L"clsid", SxmValueGuid, TRUE, SxmSetClass },
    { L"name", SxmValueAny, TRUE, SxmSetNone },
    { L"progid", SxmValueAny, FALSE, SxmSetProgId },
    { L"tlbid", SxmValueGuid, FALSE, SxmSetNone },
    { L"description", SxmValueAny, FALSE, SxmSetNone },
    { L"runtimeVersion", SxmValueAny, FALSE, SxmSetNone },
    { L"threadingModel", SxmValueThreadingModel, FALSE, SxmSetNone },
    { NULL }
};

static const SXM_RULE SxmClrSurrogateRules[] =
{
    { L"clsid", SxmValueGuid, TRUE, SxmSetSurrogate },
    { L"name", SxmValueAny, TRUE, SxmSetNone },
    { L"runtimeVersion", SxmValueAny, FALSE, SxmSetNone },
    { NULL }
};

static const SXM_RULE SxmDependencyRules[] =
{
    { L"optional", SxmValueBoolean, FALSE, SxmSetNone },
    { NULL }
};

static const SXM_RULE SxmDependentAssemblyRules[] =
{
    { L"allowDelayedBinding", SxmValueAny, FALSE, SxmSetNone },
    { L"dependencyType", SxmValueAny, FALSE, SxmSetNone },
    { L"codebase", SxmValueAny, FALSE, SxmSetNone },
    { NULL }
};

static const SXM_RULE SxmActivatableClassRules[] =
{
    { L"name", SxmValueNonEmpty, TRUE, SxmSetActivatableClass },
    { L"threadingModel", SxmValueWinRtThreadingModel, TRUE, SxmSetNone },
    { NULL }
};

static const SXM_RULE SxmExecutionLevelRules[] =
{
    { L"level", SxmValueExecutionLevel, TRUE, SxmSetNone },
    { L"uiAccess", SxmValueTrueFalse, FALSE, SxmSetNone },
    { NULL }
};

static const SXM_RULE SxmSupportedOsRules[] =
{
    { L"Id", SxmValueGuid, TRUE, SxmSetNone },
    { NULL }
};

static const SXM_RULE SxmMaxVersionTestedRules[] =
{
    { L"Id", SxmValueAny, TRUE, SxmSetNone },
    { NULL }
};

static const SXM_RULE SxmNoRules[] =
{
    { NULL }
};

static const SXM_CHILD SxmAssemblyChildren[] =
{
    { L"assemblyIdentity", SxmKindIdentity },
    { L"description", SxmKindDescription },
    { L"noInherit", SxmKindNoInherit },
    { L"noInheritable", SxmKindNoInheritable },
    { L"file", SxmKindFile },
    { L"dependency", SxmKindDependency },
    { L"comInterfaceExternalProxyStub", SxmKindExternalProxyStub },
    { L"clrClass", SxmKindClrClass },
    { L"clrSurrogate", SxmKindClrSurrogate },
    { L"trustInfo", SxmKindTrustInfo },
    { L"application", SxmKindApplication },
    { L"compatibility", SxmKindCompatibility },
    { L"configuration", SxmKindSkip },
    { L"memberships", SxmKindSkip },
    { L"migration", SxmKindSkip },
    { L"registryKeys", SxmKindSkip },
    { L"localization", SxmKindSkip },
    { L"deployment", SxmKindSkip },
    { L"entryPoint", SxmKindSkip },
    { L"signature", SxmKindSkip },
    { NULL }
};

static const SXM_CHILD SxmFileChildren[] =
{
    { L"comClass", SxmKindComClass },
    { L"typelib", SxmKindTypeLibrary },
    { L"comInterfaceProxyStub", SxmKindProxyStub },
    { L"windowClass", SxmKindWindowClass },
    { L"hash", SxmKindHash },
    { NULL }
};

static const SXM_CHILD SxmClassChildren[] =
{
    { L"progid", SxmKindProgId },
    { NULL }
};

static const SXM_CHILD SxmDependencyChildren[] =
{
    { L"dependentAssembly", SxmKindDependentAssembly },
    { L"dependentOS", SxmKindSkip },
    { NULL }
};

static const SXM_CHILD SxmDependentAssemblyChildren[] =
{
    { L"assemblyIdentity", SxmKindDependentIdentity },
    { NULL }
};

static const SXM_CHILD SxmTrustInfoChildren[] =
{
    { L"security", SxmKindSecurity },
    { NULL }
};

static const SXM_CHILD SxmSecurityChildren[] =
{
    { L"requestedPrivileges", SxmKindRequestedPrivileges },
    { L"applicationRequestMinimum", SxmKindSkip },
    { NULL }
};

static const SXM_CHILD SxmRequestedPrivilegesChildren[] =
{
    { L"requestedExecutionLevel", SxmKindExecutionLevel },
    { NULL }
};

static const SXM_CHILD SxmApplicationChildren[] =
{
    { L"windowsSettings", SxmKindWindowsSettings },
    { NULL }
};

static const SXM_CHILD SxmNoChildren[] =
{
    { NULL }
};

static const struct
{
    PCWSTR Year;
    PCWSTR Name;
} SxmSettings[] =
{
    { L"2005", L"dpiAware" },
    { L"2005", L"autoElevate" },
    { L"2005", L"disableTheming" },
    { L"2011", L"disableWindowFiltering" },
    { L"2011", L"printerDriverIsolation" },
    { L"2013", L"highResolutionScrollingAware" },
    { L"2013", L"ultraHighResolutionScrollingAware" },
    { L"2016", L"dpiAwareness" },
    { L"2016", L"longPathAware" },
    { L"2017", L"gdiScaling" },
    { L"2019", L"activeCodePage" },
    { L"2020", L"heapType" },
    { L"2024", L"supportedArchitectures" }
};

static PCWSTR const SxmArchitectures[] =
{
    L"x86", L"amd64", L"arm", L"arm64", L"ia64", L"msil", L"wow64", L"data", L"shx", L"riscv64", L"*", NULL
};

static PCWSTR const SxmHashAlgorithms[] =
{
    L"SHA1", L"SHA", L"MD5", L"MD2", L"MD4", L"MAC", L"HMAC", NULL
};

static PCWSTR const SxmThreadingModels[] =
{
    L"Apartment", L"Free", L"Both", L"Neutral", L"Single", NULL
};

static PCWSTR const SxmExecutionLevels[] =
{
    L"asInvoker", L"highestAvailable", L"requireAdministrator", NULL
};

static PCWSTR const SxmBooleans[] =
{
    L"yes", L"no", L"true", L"false", NULL
};

static PCWSTR const SxmTrueFalse[] =
{
    L"true", L"false", NULL
};

static PCWSTR const SxmWinRtThreadingModels[] =
{
    L"both", L"sta", L"mta", NULL
};

static PCWSTR const SxmTypeLibraryFlags[] =
{
    L"RESTRICTED", L"CONTROL", L"HIDDEN", L"HASDISKIMAGE", NULL
};

static PCWSTR const SxmMiscStatusFlags[] =
{
    L"activatewhenvisible", L"actslikebutton", L"actslikelabel", L"alignable", L"alwaysrun", L"canlinkbyole1",
    L"cantlinkinside", L"ignoreactivatewhenvisible", L"imemode", L"insertnotreplace", L"insideout",
    L"invisibleatruntime", L"islinkobject", L"nouiactivate", L"onlyiconic", L"recomposeonresize",
    L"renderingisdeviceindependent", L"setclientsitefirst", L"simpleframe", L"static", L"supportsmultilevelundo",
    L"wantstomenumerge", NULL
};

static
BOOLEAN
SxmFail(_Inout_ SXM_STATE *State)
{
    if (NT_SUCCESS(State->Status))
        State->Status = STATUS_SXS_CANT_GEN_ACTCTX;
    return FALSE;
}

static
BOOLEAN
SxmIsSpace(_In_ WCHAR Char)
{
    return Char == L' ' || Char == L'\t' || Char == L'\r' || Char == L'\n';
}

static
BOOLEAN
SxmIsDigit(_In_ WCHAR Char)
{
    return Char >= L'0' && Char <= L'9';
}

static
BOOLEAN
SxmIsHexDigit(_In_ WCHAR Char)
{
    return SxmIsDigit(Char) || (Char >= L'a' && Char <= L'f') || (Char >= L'A' && Char <= L'F');
}

static
BOOLEAN
SxmIsLetter(_In_ WCHAR Char)
{
    return (Char >= L'a' && Char <= L'z') || (Char >= L'A' && Char <= L'Z');
}

static
WCHAR
SxmLower(_In_ WCHAR Char)
{
    return (Char >= L'A' && Char <= L'Z') ? (WCHAR)(Char + (L'a' - L'A')) : Char;
}

static
BOOLEAN
SxmEqualRange(
    _In_reads_(Length) PCWSTR Left,
    _In_reads_(Length) PCWSTR Right,
    _In_ ULONG Length,
    _In_ BOOLEAN IgnoreCase)
{
    ULONG i;

    for (i = 0; i < Length; i++)
    {
        if (IgnoreCase ? SxmLower(Left[i]) != SxmLower(Right[i]) : Left[i] != Right[i])
            return FALSE;
    }
    return TRUE;
}

static
BOOLEAN
SxmEqualStrings(
    _In_ const SXM_STRING *Left,
    _In_ const SXM_STRING *Right,
    _In_ BOOLEAN IgnoreCase)
{
    return Left->Length == Right->Length && SxmEqualRange(Left->Buffer, Right->Buffer, Left->Length, IgnoreCase);
}

static
BOOLEAN
SxmEqualLiteral(
    _In_ const SXM_STRING *String,
    _In_z_ PCWSTR Literal,
    _In_ BOOLEAN IgnoreCase)
{
    ULONG Length = 0;

    while (Literal[Length]) Length++;
    return String->Length == Length && SxmEqualRange(String->Buffer, Literal, Length, IgnoreCase);
}

static
BOOLEAN
SxmInList(
    _In_ const SXM_STRING *String,
    _In_ PCWSTR const *List,
    _In_ BOOLEAN IgnoreCase)
{
    for (; *List; List++)
    {
        if (SxmEqualLiteral(String, *List, IgnoreCase))
            return TRUE;
    }
    return FALSE;
}

static
BOOLEAN
SxmInTokenList(
    _In_ const SXM_STRING *String,
    _In_ PCWSTR const *List)
{
    SXM_STRING Token, Other;
    ULONG i, j, Start = 0, OtherStart;

    if (!String->Length)
        return TRUE;

    for (i = 0; i <= String->Length; i++)
    {
        if (i != String->Length && String->Buffer[i] != L',')
            continue;
        Token.Buffer = String->Buffer + Start;
        Token.Length = i - Start;
        if (!SxmInList(&Token, List, TRUE))
            return FALSE;

        OtherStart = 0;
        for (j = 0; j < Start; j++)
        {
            if (String->Buffer[j] != L',')
                continue;
            Other.Buffer = String->Buffer + OtherStart;
            Other.Length = j - OtherStart;
            if (SxmEqualStrings(&Other, &Token, TRUE))
                return FALSE;
            OtherStart = j + 1;
        }
        Start = i + 1;
    }
    return TRUE;
}

static
BOOLEAN
SxmParseVersion(
    _In_ const SXM_STRING *String,
    _Out_writes_(4) USHORT *Version)
{
    ULONG i, Part = 0, Value = 0, Digits = 0;

    for (i = 0; i <= String->Length; i++)
    {
        if (i != String->Length && SxmIsDigit(String->Buffer[i]))
        {
            Value = Value * 10 + (String->Buffer[i] - L'0');
            if (Value > 0xFFFF)
                return FALSE;
            Digits++;
            continue;
        }
        if (i != String->Length && String->Buffer[i] != L'.')
            return FALSE;
        if (!Digits || Part >= 4)
            return FALSE;
        Version[Part++] = (USHORT)Value;
        Value = 0;
        Digits = 0;
    }
    return Part == 4;
}

static
BOOLEAN
SxmIsGuid(_In_ const SXM_STRING *String)
{
    ULONG i;

    if (String->Length != 38 || String->Buffer[0] != L'{' || String->Buffer[37] != L'}')
        return FALSE;

    for (i = 1; i < 37; i++)
    {
        if (i == 9 || i == 14 || i == 19 || i == 24)
        {
            if (String->Buffer[i] != L'-')
                return FALSE;
        }
        else if (!SxmIsHexDigit(String->Buffer[i]))
        {
            return FALSE;
        }
    }
    return TRUE;
}

static
BOOLEAN
SxmIsNumber(
    _In_ const SXM_STRING *String,
    _In_ BOOLEAN Hex,
    _Out_opt_ ULONG *Result)
{
    ULONGLONG Value = 0;
    ULONG i, Digit;

    if (!String->Length)
        return FALSE;

    for (i = 0; i < String->Length; i++)
    {
        WCHAR Char = String->Buffer[i];

        if (SxmIsDigit(Char))
            Digit = Char - L'0';
        else if (Hex && SxmIsHexDigit(Char))
            Digit = SxmLower(Char) - L'a' + 10;
        else
            return FALSE;

        Value = Value * (Hex ? 16 : 10) + Digit;
        if (Value > 0xFFFFFFFFULL)
            return FALSE;
    }

    if (Result)
        *Result = (ULONG)Value;
    return TRUE;
}

static
BOOLEAN
SxmIsLanguage(_In_ const SXM_STRING *String)
{
    ULONG i;

    for (i = 0; i < String->Length; i++)
    {
        if (!SxmIsLetter(String->Buffer[i]) && String->Buffer[i] != L'-' && String->Buffer[i] != L'_')
            return FALSE;
    }
    return TRUE;
}

static
BOOLEAN
SxmIsTypeLibraryVersion(_In_ const SXM_STRING *String)
{
    ULONG i, Dots = 0;

    for (i = 0; i < String->Length; i++)
    {
        if (SxmIsDigit(String->Buffer[i]))
            continue;
        if (String->Buffer[i] != L'.' || ++Dots > 1)
            return FALSE;
    }
    return Dots == 1;
}

static
BOOLEAN
SxmIsSeparator(_In_ WCHAR Char)
{
    return Char == L'\\' || Char == L'/';
}

static
BOOLEAN
SxmIsFileName(_In_ const SXM_STRING *String)
{
    ULONG i;

    if (!String->Length || SxmIsSeparator(String->Buffer[0]))
        return FALSE;

    for (i = 0; i < String->Length; i++)
    {
        if (String->Buffer[i] == L':')
            return FALSE;
        if (String->Buffer[i] == L'.' && i + 2 < String->Length &&
            String->Buffer[i + 1] == L'.' && SxmIsSeparator(String->Buffer[i + 2]) &&
            (i == 0 || SxmIsSeparator(String->Buffer[i - 1])))
        {
            return FALSE;
        }
    }
    return TRUE;
}

static
BOOLEAN
SxmIsValidValue(
    _In_ SXM_VALUE Kind,
    _In_ const SXM_STRING *Value)
{
    USHORT Version[4];

    switch (Kind)
    {
        case SxmValueAny:
            return TRUE;
        case SxmValueNonEmpty:
            return Value->Length != 0;
        case SxmValueManifestVersion:
            return SxmEqualLiteral(Value, L"1.0", FALSE);
        case SxmValueVersion:
            return SxmParseVersion(Value, Version);
        case SxmValueGuid:
            return SxmIsGuid(Value);
        case SxmValueDecimal:
            return SxmIsNumber(Value, FALSE, NULL);
        case SxmValueBoolean:
            return SxmInList(Value, SxmBooleans, TRUE);
        case SxmValueTrueFalse:
            return SxmInList(Value, SxmTrueFalse, TRUE);
        case SxmValueArchitecture:
            return SxmInList(Value, SxmArchitectures, TRUE);
        case SxmValueLanguage:
            return SxmIsLanguage(Value);
        case SxmValueLanguageReference:
            return SxmEqualLiteral(Value, L"*", FALSE) || SxmIsLanguage(Value);
        case SxmValueTypeWin32:
            return SxmEqualLiteral(Value, L"win32", TRUE);
        case SxmValueHashAlgorithm:
            return SxmInList(Value, SxmHashAlgorithms, FALSE);
        case SxmValueHash:
            return (Value->Length & 1) == 0;
        case SxmValueFileName:
            return SxmIsFileName(Value);
        case SxmValueThreadingModel:
            return SxmInList(Value, SxmThreadingModels, TRUE);
        case SxmValueWinRtThreadingModel:
            return SxmInList(Value, SxmWinRtThreadingModels, TRUE);
        case SxmValueMiscStatus:
            return SxmInTokenList(Value, SxmMiscStatusFlags);
        case SxmValueTypeLibraryFlags:
            return SxmInTokenList(Value, SxmTypeLibraryFlags);
        case SxmValueTypeLibraryVersion:
            return SxmIsTypeLibraryVersion(Value);
        case SxmValueExecutionLevel:
            return SxmInList(Value, SxmExecutionLevels, TRUE);
    }
    return FALSE;
}

static
BOOLEAN
SxmIsXmlChar(_In_ ULONG Char)
{
    return Char == 0x9 || Char == 0xA || Char == 0xD || (Char >= 0x20 && Char <= 0xD7FF) ||
           (Char >= 0xE000 && Char <= 0xFFFD) || (Char >= 0x10000 && Char <= 0x10FFFF);
}

static
BOOLEAN
SxmValidReferences(
    _In_ PCWSTR Start,
    _In_ PCWSTR End)
{
    static PCWSTR const Entities[] = { L"amp", L"lt", L"gt", L"quot", L"apos", NULL };
    SXM_STRING Name;
    PCWSTR p, Semicolon;
    ULONG Value;

    for (p = Start; p < End; p++)
    {
        if (*p != L'&')
            continue;

        for (Semicolon = p + 1; Semicolon < End && *Semicolon != L';'; Semicolon++) ;
        if (Semicolon == End)
            return FALSE;

        Name.Buffer = p + 1;
        Name.Length = (ULONG)(Semicolon - (p + 1));
        if (Name.Length >= 2 && Name.Buffer[0] == L'#')
        {
            BOOLEAN Hex = Name.Buffer[1] == L'x';

            Name.Buffer += Hex ? 2 : 1;
            Name.Length -= Hex ? 2 : 1;
            if (!SxmIsNumber(&Name, Hex, &Value) || !SxmIsXmlChar(Value))
                return FALSE;
        }
        else if (!SxmInList(&Name, Entities, FALSE))
        {
            return FALSE;
        }
        p = Semicolon;
    }
    return TRUE;
}

static
BOOLEAN
SxmStartsWith(
    _In_ const SXM_STATE *State,
    _In_ PCWSTR Position,
    _In_z_ PCWSTR Literal)
{
    for (; *Literal; Literal++, Position++)
    {
        if (Position >= State->End || *Position != *Literal)
            return FALSE;
    }
    return TRUE;
}

static
PCWSTR
SxmFind(
    _In_ const SXM_STATE *State,
    _In_ PCWSTR Position,
    _In_z_ PCWSTR Literal)
{
    for (; Position < State->End; Position++)
    {
        if (SxmStartsWith(State, Position, Literal))
            return Position;
    }
    return NULL;
}

static
SXM_TOKEN
SxmNextAttribute(
    _In_ const SXM_STATE *State,
    _Inout_ PCWSTR *Cursor,
    _Out_ SXM_ATTRIBUTE *Attribute)
{
    PCWSTR p = *Cursor, Start, Colon = NULL, NameEnd;
    WCHAR Quote;

    while (p < State->End && SxmIsSpace(*p)) p++;
    if (p == State->End)
        return SxmTokenError;

    if (*p == L'>')
    {
        *Cursor = p + 1;
        return SxmTokenOpen;
    }

    if (*p == L'/')
    {
        if (p + 1 == State->End || p[1] != L'>')
            return SxmTokenError;
        *Cursor = p + 2;
        return SxmTokenEmpty;
    }

    if (p == *Cursor)
        return SxmTokenError;

    Start = p;
    while (p < State->End && *p != L'=' && *p != L'>' && *p != L'/' && *p != L'<' && !SxmIsSpace(*p))
    {
        if (*p == L':' && !Colon)
            Colon = p;
        p++;
    }
    NameEnd = p;
    if (NameEnd == Start || Colon == Start || Colon + 1 == NameEnd)
        return SxmTokenError;

    while (p < State->End && SxmIsSpace(*p)) p++;
    if (p == State->End || *p != L'=')
        return SxmTokenError;
    p++;
    while (p < State->End && SxmIsSpace(*p)) p++;
    if (p == State->End || (*p != L'"' && *p != L'\''))
        return SxmTokenError;

    Quote = *p++;
    Attribute->Value.Buffer = p;
    while (p < State->End && *p != Quote)
    {
        if (*p == L'<')
            return SxmTokenError;
        p++;
    }
    if (p == State->End)
        return SxmTokenError;
    Attribute->Value.Length = (ULONG)(p - Attribute->Value.Buffer);
    if (!SxmValidReferences(Attribute->Value.Buffer, p))
        return SxmTokenError;

    if (Colon)
    {
        Attribute->Prefix.Buffer = Start;
        Attribute->Prefix.Length = (ULONG)(Colon - Start);
        Attribute->Name.Buffer = Colon + 1;
        Attribute->Name.Length = (ULONG)(NameEnd - (Colon + 1));
    }
    else
    {
        Attribute->Prefix.Buffer = Start;
        Attribute->Prefix.Length = 0;
        Attribute->Name.Buffer = Start;
        Attribute->Name.Length = (ULONG)(NameEnd - Start);
    }

    *Cursor = p + 1;
    return SxmTokenAttribute;
}

static
BOOLEAN
SxmIsNamespaceDeclaration(_In_ const SXM_ATTRIBUTE *Attribute)
{
    if (Attribute->Prefix.Length)
        return SxmEqualLiteral(&Attribute->Prefix, L"xmlns", FALSE);
    return SxmEqualLiteral(&Attribute->Name, L"xmlns", FALSE);
}

static
PVOID
SxmGrow(
    _Inout_ SXM_STATE *State,
    _In_opt_ PVOID Table,
    _In_ ULONG Count,
    _Inout_ ULONG *Capacity,
    _In_ ULONG EntrySize)
{
    ULONG NewCapacity;
    PVOID NewTable;

    if (Count < *Capacity)
        return Table;

    NewCapacity = *Capacity ? *Capacity * 2 : 16;
    NewTable = RtlAllocateHeap(RtlGetProcessHeap(), 0, (SIZE_T)NewCapacity * EntrySize);
    if (!NewTable)
    {
        State->Status = STATUS_NO_MEMORY;
        return NULL;
    }
    if (Table)
    {
        RtlCopyMemory(NewTable, Table, (SIZE_T)Count * EntrySize);
        RtlFreeHeap(RtlGetProcessHeap(), 0, Table);
    }
    *Capacity = NewCapacity;
    return NewTable;
}

static
BOOLEAN
SxmPushNamespace(
    _Inout_ SXM_STATE *State,
    _In_ const SXM_ATTRIBUTE *Attribute)
{
    SXM_NAMESPACE *Table, *Entry;

    if (Attribute->Prefix.Length && !Attribute->Value.Length)
        return SxmFail(State);

    Table = SxmGrow(State, State->Namespaces, State->NamespaceCount, &State->NamespaceCapacity, sizeof(*Table));
    if (!Table)
        return FALSE;
    State->Namespaces = Table;

    Entry = &State->Namespaces[State->NamespaceCount++];
    Entry->Uri = Attribute->Value;
    if (Attribute->Prefix.Length)
    {
        Entry->Prefix = Attribute->Name;
    }
    else
    {
        Entry->Prefix.Buffer = Attribute->Name.Buffer;
        Entry->Prefix.Length = 0;
    }
    return TRUE;
}

static
BOOLEAN
SxmFindNamespace(
    _In_ const SXM_STATE *State,
    _In_ const SXM_STRING *Prefix,
    _Out_ SXM_STRING *Uri)
{
    ULONG i;

    for (i = State->NamespaceCount; i > 0; i--)
    {
        if (SxmEqualStrings(&State->Namespaces[i - 1].Prefix, Prefix, FALSE))
        {
            *Uri = State->Namespaces[i - 1].Uri;
            return TRUE;
        }
    }
    Uri->Buffer = Prefix->Buffer;
    Uri->Length = 0;
    return FALSE;
}

static
SXM_NAMESPACE_CLASS
SxmClassifyNamespace(
    _In_ const SXM_STRING *Uri,
    _Out_ SXM_STRING *Year)
{
    static const WCHAR SettingsPrefix[] = L"http://schemas.microsoft.com/SMI/";
    static const WCHAR SettingsSuffix[] = L"/WindowsSettings";
    const ULONG PrefixLength = RTL_NUMBER_OF(SettingsPrefix) - 1;
    const ULONG SuffixLength = RTL_NUMBER_OF(SettingsSuffix) - 1;

    Year->Buffer = Uri->Buffer;
    Year->Length = 0;

    if (SxmEqualLiteral(Uri, L"urn:schemas-microsoft-com:asm.v1", FALSE) ||
        SxmEqualLiteral(Uri, L"urn:schemas-microsoft-com:asm.v2", FALSE) ||
        SxmEqualLiteral(Uri, L"urn:schemas-microsoft-com:asm.v3", FALSE))
    {
        return SxmNamespaceAssembly;
    }

    if (SxmEqualLiteral(Uri, L"urn:schemas-microsoft-com:compatibility.v1", FALSE))
        return SxmNamespaceCompatibility;

    if (SxmEqualLiteral(Uri, L"urn:schemas-microsoft-com:winrt.v1", FALSE))
        return SxmNamespaceWinRt;

    if (Uri->Length == PrefixLength + 4 + SuffixLength &&
        SxmEqualRange(Uri->Buffer, SettingsPrefix, PrefixLength, FALSE) &&
        SxmEqualRange(Uri->Buffer + PrefixLength + 4, SettingsSuffix, SuffixLength, FALSE))
    {
        Year->Buffer = Uri->Buffer + PrefixLength;
        Year->Length = 4;
        return SxmNamespaceSettings;
    }

    return SxmNamespaceOther;
}

static
const SXM_CHILD *
SxmChildrenOf(_In_ SXM_KIND Kind)
{
    switch (Kind)
    {
        case SxmKindAssembly: return SxmAssemblyChildren;
        case SxmKindFile: return SxmFileChildren;
        case SxmKindComClass:
        case SxmKindClrClass: return SxmClassChildren;
        case SxmKindDependency: return SxmDependencyChildren;
        case SxmKindDependentAssembly: return SxmDependentAssemblyChildren;
        case SxmKindTrustInfo: return SxmTrustInfoChildren;
        case SxmKindSecurity: return SxmSecurityChildren;
        case SxmKindRequestedPrivileges: return SxmRequestedPrivilegesChildren;
        case SxmKindApplication: return SxmApplicationChildren;
        default: return SxmNoChildren;
    }
}

static
const SXM_RULE *
SxmRulesOf(_In_ SXM_KIND Kind)
{
    switch (Kind)
    {
        case SxmKindAssembly: return SxmAssemblyRules;
        case SxmKindIdentity: return SxmIdentityRules;
        case SxmKindDependentIdentity: return SxmDependentIdentityRules;
        case SxmKindFile: return SxmFileRules;
        case SxmKindComClass: return SxmComClassRules;
        case SxmKindTypeLibrary: return SxmTypeLibraryRules;
        case SxmKindProxyStub: return SxmProxyStubRules;
        case SxmKindExternalProxyStub: return SxmExternalProxyStubRules;
        case SxmKindWindowClass: return SxmWindowClassRules;
        case SxmKindActivatableClass: return SxmActivatableClassRules;
        case SxmKindClrClass: return SxmClrClassRules;
        case SxmKindClrSurrogate: return SxmClrSurrogateRules;
        case SxmKindDependency: return SxmDependencyRules;
        case SxmKindDependentAssembly: return SxmDependentAssemblyRules;
        case SxmKindExecutionLevel: return SxmExecutionLevelRules;
        case SxmKindSupportedOs: return SxmSupportedOsRules;
        case SxmKindMaxVersionTested: return SxmMaxVersionTestedRules;
        default: return SxmNoRules;
    }
}

static
BOOLEAN
SxmAllowsUnknownAttributes(_In_ SXM_KIND Kind)
{
    switch (Kind)
    {
        case SxmKindSkip:
        case SxmKindSetting:
        case SxmKindHash:
        case SxmKindCompatibility:
        case SxmKindCompatibilityApplication:
        case SxmKindSupportedOs:
        case SxmKindMaxVersionTested:
            return TRUE;
        default:
            return FALSE;
    }
}

static
SXM_KIND
SxmClassifyElement(
    _In_ const SXM_STATE *State,
    _In_ SXM_KIND Parent,
    _In_ BOOLEAN HasNamespace,
    _In_ const SXM_STRING *Uri,
    _In_ const SXM_STRING *Name)
{
    SXM_NAMESPACE_CLASS Class = SxmNamespaceOther;
    const SXM_CHILD *Child;
    SXM_STRING Year;
    BOOLEAN KnownYear = FALSE;
    ULONG i;

    if (Parent == SxmKindSkip || Parent == SxmKindSetting || Parent == SxmKindSupportedOs || Parent == SxmKindMaxVersionTested)
        return SxmKindSkip;

    if (HasNamespace)
        Class = SxmClassifyNamespace(Uri, &Year);

    if (Parent == SxmKindDocument)
        return (Class == SxmNamespaceAssembly && SxmEqualLiteral(Name, L"assembly", FALSE)) ? SxmKindAssembly : SxmKindReject;

    if (Parent == SxmKindNoInherit || Parent == SxmKindNoInheritable)
        return SxmKindReject;

    if (Class == SxmNamespaceWinRt)
        return (Parent == SxmKindFile && SxmEqualLiteral(Name, L"activatableClass", FALSE)) ? SxmKindActivatableClass : SxmKindReject;

    if (Parent == SxmKindCompatibility)
    {
        if ((Class == SxmNamespaceCompatibility || Class == SxmNamespaceAssembly) && SxmEqualLiteral(Name, L"application", FALSE))
            return SxmKindCompatibilityApplication;
        return SxmKindSkip;
    }

    if (Parent == SxmKindCompatibilityApplication)
    {
        if (Class == SxmNamespaceCompatibility && SxmEqualLiteral(Name, L"supportedOS", FALSE))
            return SxmKindSupportedOs;
        if (Class == SxmNamespaceCompatibility && SxmEqualLiteral(Name, L"maxversiontested", FALSE))
            return SxmKindMaxVersionTested;
        return SxmKindSkip;
    }

    if (Parent == SxmKindWindowsSettings)
    {
        if (Class == SxmNamespaceAssembly)
            return SxmKindReject;
        if (Class != SxmNamespaceSettings)
            return SxmKindSkip;
        for (i = 0; i < RTL_NUMBER_OF(SxmSettings); i++)
        {
            if (!SxmEqualLiteral(&Year, SxmSettings[i].Year, FALSE))
                continue;
            KnownYear = TRUE;
            if (SxmEqualLiteral(Name, SxmSettings[i].Name, TRUE))
                return SxmKindSetting;
        }
        return KnownYear ? SxmKindReject : SxmKindSkip;
    }

    if (Class == SxmNamespaceCompatibility && Parent == SxmKindAssembly && SxmEqualLiteral(Name, L"compatibility", FALSE))
        return SxmKindCompatibility;

    if (Class != SxmNamespaceAssembly)
        return SxmKindSkip;

    for (Child = SxmChildrenOf(Parent); Child->Name; Child++)
    {
        if (!SxmEqualLiteral(Name, Child->Name, FALSE))
            continue;
        if (!State->ApplicationManifest &&
            (Child->Kind == SxmKindNoInherit || Child->Kind == SxmKindTrustInfo || Child->Kind == SxmKindApplication))
        {
            return SxmKindReject;
        }
        return Child->Kind;
    }
    return SxmKindReject;
}

static
SXM_SEEN *
SxmFindSeen(
    _In_ const SXM_STATE *State,
    _In_ SXM_SET Set,
    _In_ const SXM_STRING *Key,
    _In_opt_ const SXM_STRING *Scope)
{
    ULONG i;

    for (i = 0; i < State->SeenCount; i++)
    {
        SXM_SEEN *Entry = &State->Seen[i];

        if (Entry->Set != Set || !SxmEqualStrings(&Entry->Key, Key, TRUE))
            continue;
        if (Scope && !SxmEqualStrings(&Entry->Scope, Scope, FALSE))
            continue;
        return Entry;
    }
    return NULL;
}

static
SXM_SEEN *
SxmAddSeen(
    _Inout_ SXM_STATE *State,
    _In_ SXM_SET Set,
    _In_ const SXM_STRING *Key,
    _In_opt_ const SXM_STRING *Scope)
{
    SXM_SEEN *Table, *Entry;

    Table = SxmGrow(State, State->Seen, State->SeenCount, &State->SeenCapacity, sizeof(*Table));
    if (!Table)
        return NULL;
    State->Seen = Table;

    Entry = &State->Seen[State->SeenCount++];
    RtlZeroMemory(Entry, sizeof(*Entry));
    Entry->Set = Set;
    Entry->Key = *Key;
    if (Scope)
        Entry->Scope = *Scope;
    return Entry;
}

static
BOOLEAN
SxmAddUnique(
    _Inout_ SXM_STATE *State,
    _In_ SXM_SET Set,
    _In_ const SXM_STRING *Key,
    _In_opt_ const SXM_STRING *Scope,
    _In_ BOOLEAN Implicit)
{
    SXM_SEEN *Entry = SxmFindSeen(State, Set, Key, Scope);

    if (Entry)
    {
        if (Entry->Implicit && Implicit)
            return TRUE;
        return SxmFail(State);
    }

    Entry = SxmAddSeen(State, Set, Key, Scope);
    if (!Entry)
        return FALSE;
    Entry->Implicit = Implicit;
    return TRUE;
}

static
BOOLEAN
SxmCheckAttributes(
    _Inout_ SXM_STATE *State,
    _In_ SXM_KIND Kind,
    _In_ PCWSTR TagStart)
{
    const SXM_RULE *Rules = SxmRulesOf(Kind), *Rule;
    SXM_ATTRIBUTE Attribute;
    SXM_STRING Uri, Name, Interface, ProxyStubClass;
    PCWSTR Cursor = TagStart;
    ULONG Present = 0, Index;
    USHORT Version[4] = { 0, 0, 0, 0 };
    BOOLEAN HaveName = FALSE, HaveVersion = FALSE;
    SXM_SEEN *Seen;

    Name.Buffer = Interface.Buffer = ProxyStubClass.Buffer = TagStart;
    Name.Length = Interface.Length = ProxyStubClass.Length = 0;

    while (SxmNextAttribute(State, &Cursor, &Attribute) == SxmTokenAttribute)
    {
        if (SxmIsNamespaceDeclaration(&Attribute))
        {
            if (Kind == SxmKindIdentity || Kind == SxmKindDependentIdentity)
                return SxmFail(State);
            continue;
        }

        if (Attribute.Prefix.Length)
        {
            if (!SxmFindNamespace(State, &Attribute.Prefix, &Uri))
                return SxmFail(State);
            if (Kind == SxmKindNoInherit || Kind == SxmKindNoInheritable || Kind == SxmKindDependentIdentity)
                return SxmFail(State);
            continue;
        }

        for (Rule = Rules, Index = 0; Rule->Name; Rule++, Index++)
        {
            if (SxmEqualLiteral(&Attribute.Name, Rule->Name, FALSE))
                break;
        }

        if (!Rule->Name)
        {
            if (SxmAllowsUnknownAttributes(Kind))
                continue;
            return SxmFail(State);
        }

        if (!SxmIsValidValue(Rule->Value, &Attribute.Value))
            return SxmFail(State);

        Present |= 1UL << Index;

        if (Rule->Unique != SxmSetNone && Attribute.Value.Length &&
            !SxmAddUnique(State, Rule->Unique, &Attribute.Value, NULL, FALSE))
        {
            return FALSE;
        }

        if (Kind == SxmKindDependentIdentity && Rule->Value == SxmValueNonEmpty)
        {
            Name = Attribute.Value;
            HaveName = TRUE;
        }
        else if (Kind == SxmKindDependentIdentity && Rule->Value == SxmValueVersion)
        {
            HaveVersion = SxmParseVersion(&Attribute.Value, Version);
        }
        else if (Kind == SxmKindProxyStub && Rule->Unique == SxmSetInterface)
        {
            Interface = Attribute.Value;
        }
        else if (Kind == SxmKindProxyStub && SxmEqualLiteral(&Attribute.Name, L"proxyStubClsid32", FALSE))
        {
            ProxyStubClass = Attribute.Value;
        }
    }

    for (Rule = Rules, Index = 0; Rule->Name; Rule++, Index++)
    {
        if (Rule->Required && !(Present & (1UL << Index)))
            return SxmFail(State);
    }

    if (Kind == SxmKindProxyStub &&
        !SxmAddUnique(State, SxmSetClass, ProxyStubClass.Length ? &ProxyStubClass : &Interface, NULL, TRUE))
    {
        return FALSE;
    }

    if (Kind == SxmKindDependentIdentity && HaveName && HaveVersion)
    {
        Seen = SxmFindSeen(State, SxmSetDependency, &Name, NULL);
        if (Seen)
        {
            if (Seen->Version[0] != Version[0] || Seen->Version[1] != Version[1] ||
                Seen->Version[2] != Version[2] || Seen->Version[3] != Version[3])
            {
                return SxmFail(State);
            }
        }
        else
        {
            Seen = SxmAddSeen(State, SxmSetDependency, &Name, NULL);
            if (!Seen)
                return FALSE;
            RtlCopyMemory(Seen->Version, Version, sizeof(Version));
        }
    }

    return TRUE;
}

static
BOOLEAN
SxmCheckPlacement(
    _Inout_ SXM_STATE *State,
    _Inout_ SXM_FRAME *Parent,
    _In_ SXM_KIND Kind)
{
    switch (Kind)
    {
        case SxmKindIdentity:
            if (Parent->IdentitySeen)
                return SxmFail(State);
            Parent->IdentitySeen = TRUE;
            break;

        case SxmKindNoInherit:
            if (Parent->IdentitySeen || Parent->NoInheritSeen)
                return SxmFail(State);
            Parent->NoInheritSeen = TRUE;
            break;

        case SxmKindNoInheritable:
            if (Parent->IdentitySeen || Parent->NoInheritableSeen)
                return SxmFail(State);
            Parent->NoInheritableSeen = TRUE;
            break;

        case SxmKindDependentAssembly:
        case SxmKindDependentIdentity:
            if (Parent->ChildSeen)
                return SxmFail(State);
            Parent->ChildSeen = TRUE;
            break;

        case SxmKindExecutionLevel:
            if (State->ExecutionLevelSeen)
                return SxmFail(State);
            State->ExecutionLevelSeen = TRUE;
            break;

        default:
            break;
    }
    return TRUE;
}

static
BOOLEAN
SxmIsNameChar(_In_ WCHAR Char)
{
    return SxmIsLetter(Char) || SxmIsDigit(Char) || Char == L'.' || Char == L'-' || Char == L'_' || Char == L':' || Char >= 0x80;
}

static
BOOLEAN
SxmSkipMarkup(
    _Inout_ SXM_STATE *State,
    _In_ BOOLEAN InsideElement,
    _Out_ BOOLEAN *Skipped)
{
    PCWSTR p = State->Current, Close, Target;
    SXM_STRING Name;

    *Skipped = TRUE;

    if (SxmStartsWith(State, p, L"!--"))
    {
        Close = SxmFind(State, p + 3, L"--");
        if (!Close || !SxmStartsWith(State, Close, L"-->"))
            return SxmFail(State);
        State->Current = Close + 3;
        return TRUE;
    }

    if (SxmStartsWith(State, p, L"![CDATA["))
    {
        if (!InsideElement)
            return SxmFail(State);
        Close = SxmFind(State, p + 8, L"]]>");
        if (!Close)
            return SxmFail(State);
        State->Current = Close + 3;
        return TRUE;
    }

    if (SxmStartsWith(State, p, L"?"))
    {
        for (Target = p + 1; Target < State->End && SxmIsNameChar(*Target); Target++) ;
        Name.Buffer = p + 1;
        Name.Length = (ULONG)(Target - (p + 1));
        if (!Name.Length || SxmEqualLiteral(&Name, L"xml", TRUE))
            return SxmFail(State);
        if (Target == State->End || (!SxmIsSpace(*Target) && !SxmStartsWith(State, Target, L"?>")))
            return SxmFail(State);
        Close = SxmFind(State, Target, L"?>");
        if (!Close)
            return SxmFail(State);
        State->Current = Close + 2;
        return TRUE;
    }

    if (SxmStartsWith(State, p, L"!"))
        return SxmFail(State);

    *Skipped = FALSE;
    return TRUE;
}

static
BOOLEAN
SxmCloseElement(
    _Inout_ SXM_STATE *State,
    _In_ const SXM_FRAME *Frame)
{
    if (Frame->Kind == SxmKindWindowClass && Frame->Content.Length &&
        !SxmAddUnique(State, SxmSetWindowClass, &Frame->Content, NULL, FALSE))
    {
        return FALSE;
    }

    if (Frame->Kind == SxmKindProgId && Frame->Content.Length &&
        !SxmAddUnique(State, SxmSetProgId, &Frame->Content, NULL, FALSE))
    {
        return FALSE;
    }

    State->NamespaceCount = Frame->SavedNamespaces;
    return TRUE;
}

static
BOOLEAN
SxmOpenElement(_Inout_ SXM_STATE *State)
{
    SXM_FRAME Frame, *Frames, *Parent;
    SXM_ATTRIBUTE Attribute, Other;
    SXM_STRING Prefix, Name, Uri, Year;
    PCWSTR p = State->Current, TagStart, Cursor, Inner, Colon = NULL;
    ULONG Count, i;
    SXM_TOKEN Token;
    BOOLEAN HasNamespace;

    RtlZeroMemory(&Frame, sizeof(Frame));
    Frame.SavedNamespaces = State->NamespaceCount;
    Frame.QualifiedName.Buffer = p;
    while (p < State->End && !SxmIsSpace(*p) && *p != L'>' && *p != L'/')
    {
        if (*p == L':' && !Colon)
            Colon = p;
        p++;
    }
    Frame.QualifiedName.Length = (ULONG)(p - Frame.QualifiedName.Buffer);
    if (!Frame.QualifiedName.Length || p == State->End)
        return SxmFail(State);

    if (Colon)
    {
        Prefix.Buffer = Frame.QualifiedName.Buffer;
        Prefix.Length = (ULONG)(Colon - Frame.QualifiedName.Buffer);
        Name.Buffer = Colon + 1;
        Name.Length = (ULONG)(p - (Colon + 1));
    }
    else
    {
        Prefix.Buffer = Frame.QualifiedName.Buffer;
        Prefix.Length = 0;
        Name = Frame.QualifiedName;
    }

    TagStart = p;
    Cursor = TagStart;
    Count = 0;
    for (;;)
    {
        Token = SxmNextAttribute(State, &Cursor, &Attribute);
        if (Token == SxmTokenError)
            return SxmFail(State);
        if (Token != SxmTokenAttribute)
            break;

        Inner = TagStart;
        for (i = 0; i < Count; i++)
        {
            SxmNextAttribute(State, &Inner, &Other);
            if (SxmEqualStrings(&Other.Prefix, &Attribute.Prefix, FALSE) && SxmEqualStrings(&Other.Name, &Attribute.Name, FALSE))
                return SxmFail(State);
        }
        Count++;

        if (SxmIsNamespaceDeclaration(&Attribute) && !SxmPushNamespace(State, &Attribute))
            return FALSE;
    }

    HasNamespace = SxmFindNamespace(State, &Prefix, &Uri);
    if (HasNamespace && !Uri.Length)
        HasNamespace = FALSE;

    Parent = &State->Frames[State->FrameCount - 1];
    Frame.Kind = SxmClassifyElement(State, Parent->Kind, HasNamespace, &Uri, &Name);
    if (Frame.Kind == SxmKindReject)
        return SxmFail(State);

    if (!SxmCheckPlacement(State, Parent, Frame.Kind))
        return FALSE;

    if (!SxmCheckAttributes(State, Frame.Kind, TagStart))
        return FALSE;

    if (Frame.Kind == SxmKindSetting)
    {
        SxmClassifyNamespace(&Uri, &Year);
        if (!SxmAddUnique(State, SxmSetSetting, &Name, &Year, FALSE))
            return FALSE;
    }

    State->Current = Cursor;
    if (Token == SxmTokenEmpty)
        return SxmCloseElement(State, &Frame);

    Frames = SxmGrow(State, State->Frames, State->FrameCount, &State->FrameCapacity, sizeof(*Frames));
    if (!Frames)
        return FALSE;
    State->Frames = Frames;
    State->Frames[State->FrameCount++] = Frame;
    return TRUE;
}

static
BOOLEAN
SxmParseContent(_Inout_ SXM_STATE *State)
{
    SXM_FRAME *Frame = &State->Frames[State->FrameCount - 1];
    SXM_STRING CloseName;
    PCWSTR p, TextStart = State->Current;
    BOOLEAN Skipped;

    for (p = TextStart; p < State->End && *p != L'<'; p++)
    {
        if (*p == L']' && SxmStartsWith(State, p, L"]]>"))
            return SxmFail(State);
    }
    if (p == State->End || !SxmValidReferences(TextStart, p))
        return SxmFail(State);

    if (!Frame->ContentSeen)
    {
        Frame->Content.Buffer = TextStart;
        Frame->Content.Length = (ULONG)(p - TextStart);
        Frame->ContentSeen = TRUE;
    }

    State->Current = p + 1;
    if (!SxmSkipMarkup(State, TRUE, &Skipped))
        return FALSE;
    if (Skipped)
        return TRUE;

    if (State->Current == State->End || *State->Current != L'/')
        return SxmOpenElement(State);

    p = State->Current + 1;
    CloseName.Buffer = p;
    while (p < State->End && !SxmIsSpace(*p) && *p != L'>') p++;
    CloseName.Length = (ULONG)(p - CloseName.Buffer);
    if (!SxmEqualStrings(&CloseName, &Frame->QualifiedName, FALSE))
        return SxmFail(State);
    while (p < State->End && SxmIsSpace(*p)) p++;
    if (p == State->End || *p != L'>')
        return SxmFail(State);
    State->Current = p + 1;

    if (!SxmCloseElement(State, Frame))
        return FALSE;
    State->FrameCount--;
    return TRUE;
}

static
BOOLEAN
SxmParsePseudoAttribute(
    _Inout_ SXM_STATE *State,
    _Inout_ PCWSTR *Cursor,
    _In_z_ PCWSTR Expected,
    _Out_ SXM_STRING *Value)
{
    PCWSTR p = *Cursor, Start;
    SXM_STRING Name;
    WCHAR Quote;

    Value->Buffer = p;
    Value->Length = 0;

    if (p == State->End || !SxmIsSpace(*p))
        return FALSE;
    while (p < State->End && SxmIsSpace(*p)) p++;

    Start = p;
    while (p < State->End && SxmIsNameChar(*p)) p++;
    Name.Buffer = Start;
    Name.Length = (ULONG)(p - Start);
    if (!SxmEqualLiteral(&Name, Expected, FALSE))
        return FALSE;

    while (p < State->End && SxmIsSpace(*p)) p++;
    if (p == State->End || *p != L'=')
        return FALSE;
    p++;
    while (p < State->End && SxmIsSpace(*p)) p++;
    if (p == State->End || (*p != L'"' && *p != L'\''))
        return FALSE;
    Quote = *p++;
    Value->Buffer = p;
    while (p < State->End && *p != Quote) p++;
    if (p == State->End)
        return FALSE;
    Value->Length = (ULONG)(p - Value->Buffer);
    *Cursor = p + 1;
    return TRUE;
}

static
BOOLEAN
SxmParseDeclaration(
    _Inout_ SXM_STATE *State,
    _In_ BOOLEAN Utf16Source)
{
    SXM_STRING Value;
    PCWSTR p = State->Current + 5;

    if (!SxmParsePseudoAttribute(State, &p, L"version", &Value) || !SxmEqualLiteral(&Value, L"1.0", FALSE))
        return SxmFail(State);

    if (SxmParsePseudoAttribute(State, &p, L"encoding", &Value))
    {
        if (Utf16Source)
        {
            if (!SxmEqualLiteral(&Value, L"UTF-16", TRUE) && !SxmEqualLiteral(&Value, L"UCS-2", TRUE))
                return SxmFail(State);
        }
        else if (!SxmEqualLiteral(&Value, L"UTF-8", TRUE))
        {
            return SxmFail(State);
        }
    }

    if (SxmParsePseudoAttribute(State, &p, L"standalone", &Value) &&
        !SxmEqualLiteral(&Value, L"yes", FALSE) && !SxmEqualLiteral(&Value, L"no", FALSE))
    {
        return SxmFail(State);
    }

    while (p < State->End && SxmIsSpace(*p)) p++;
    if (!SxmStartsWith(State, p, L"?>"))
        return SxmFail(State);

    State->Current = p + 2;
    return TRUE;
}

NTSTATUS
NTAPI
RtlpValidateSxsManifest(
    _In_reads_(Length) PCWSTR Text,
    _In_ SIZE_T Length,
    _In_ BOOLEAN Utf16Source,
    _In_ BOOLEAN ApplicationManifest)
{
    SXM_STATE State;
    SXM_FRAME Document;
    PCWSTR p;
    BOOLEAN Skipped, RootSeen = FALSE;

    RtlZeroMemory(&State, sizeof(State));
    State.Current = Text;
    State.End = Text + Length;
    State.ApplicationManifest = ApplicationManifest;
    State.Status = STATUS_SUCCESS;

    RtlZeroMemory(&Document, sizeof(Document));
    Document.Kind = SxmKindDocument;
    State.Frames = SxmGrow(&State, NULL, 0, &State.FrameCapacity, sizeof(*State.Frames));
    if (!State.Frames)
        return State.Status;
    State.Frames[State.FrameCount++] = Document;

    if (State.Current < State.End && *State.Current == 0xFEFF)
        State.Current++;

    while (State.Current < State.End && SxmIsSpace(*State.Current)) State.Current++;

    if (SxmStartsWith(&State, State.Current, L"<?xml") &&
        State.Current + 5 < State.End && !SxmIsNameChar(State.Current[5]))
    {
        SxmParseDeclaration(&State, Utf16Source);
    }

    while (NT_SUCCESS(State.Status))
    {
        if (State.FrameCount > 1)
        {
            SxmParseContent(&State);
            continue;
        }

        for (p = State.Current; p < State.End && SxmIsSpace(*p); p++) ;
        if (p == State.End)
            break;
        if (*p != L'<')
        {
            SxmFail(&State);
            break;
        }

        State.Current = p + 1;
        if (!SxmSkipMarkup(&State, FALSE, &Skipped) || Skipped)
            continue;

        if (RootSeen)
        {
            SxmFail(&State);
            break;
        }
        RootSeen = TRUE;
        SxmOpenElement(&State);
    }

    if (NT_SUCCESS(State.Status) && !RootSeen)
        SxmFail(&State);

    RtlFreeHeap(RtlGetProcessHeap(), 0, State.Frames);
    if (State.Namespaces)
        RtlFreeHeap(RtlGetProcessHeap(), 0, State.Namespaces);
    if (State.Seen)
        RtlFreeHeap(RtlGetProcessHeap(), 0, State.Seen);

    return State.Status;
}
