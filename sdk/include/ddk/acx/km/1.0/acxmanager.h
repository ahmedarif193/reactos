/*
 * PROJECT:     LiberNT DDK
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Audio class extension 1.0 interface
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _ACXMANAGER_H_
#define _ACXMANAGER_H_

#ifndef WDF_EXTERN_C
  #ifdef __cplusplus
    #define WDF_EXTERN_C       extern "C"
    #define WDF_EXTERN_C_START extern "C" {
    #define WDF_EXTERN_C_END   }
  #else
    #define WDF_EXTERN_C
    #define WDF_EXTERN_C_START
    #define WDF_EXTERN_C_END
  #endif
#endif

WDF_EXTERN_C_START

#if (NTDDI_VERSION >= NTDDI_WIN2K)

typedef struct _ACX_CIRCUIT_TEMPLATE_CONFIG {
    ULONG                   Size;
    ULONG                   Flags;

    const GUID *            ContainerId;

    ACXOBJECTBAG            FactoryProperties;

    const GUID *            FactoryId;
    PCUNICODE_STRING        FactoryUri;
    PCUNICODE_STRING        FactoryName;

    ACXOBJECTBAG            CircuitProperties;

    const GUID *            CircuitId;
    PCUNICODE_STRING        CircuitUri;
    PCUNICODE_STRING        CircuitName;

} ACX_CIRCUIT_TEMPLATE_CONFIG, *PACX_CIRCUIT_TEMPLATE_CONFIG;

typedef enum _ACX_CIRCUIT_TEMPLATE_CONFIG_FLAGS {
    AcxCircuitTemplateConfigNoFlags     = 0x00000000,
    AcxCircuitTemplateCircuitOnDemand   = 0x00000001,
    AcxCircuitTemplateConfigValidFlags  = 0x00000001
} ACX_CIRCUIT_TEMPLATE_CONFIG_FLAGS;

VOID
FORCEINLINE
ACX_CIRCUIT_TEMPLATE_CONFIG_INIT(
    _Out_ PACX_CIRCUIT_TEMPLATE_CONFIG Config
    )
{
    RtlZeroMemory(Config, sizeof(ACX_CIRCUIT_TEMPLATE_CONFIG));
    Config->Size = ACX_STRUCTURE_SIZE(ACX_CIRCUIT_TEMPLATE_CONFIG);
    Config->Flags = AcxCircuitTemplateConfigNoFlags;
}

typedef struct _ACX_COMPOSITE_TEMPLATE_CONFIG {
    ULONG                   Size;
    ULONG                   Flags;
    ACXOBJECTBAG            Properties;
} ACX_COMPOSITE_TEMPLATE_CONFIG, *PACX_COMPOSITE_TEMPLATE_CONFIG;

typedef enum _ACX_COMPOSITE_TEMPLATE_CONFIG_FLAGS {
    AcxCompositeTemplateConfigNoFlags       = 0x00000000,
    AcxCompositeTemplateConfigPrivate       = 0x00000001,
    AcxCompositeTemplateConfigSingleton     = 0x00000002,
    AcxCompositeTemplateConfigValidFlags    = 0x00000003
} ACX_COMPOSITE_TEMPLATE_CONFIG_FLAGS;

VOID
FORCEINLINE
ACX_COMPOSITE_TEMPLATE_CONFIG_INIT(
    _Out_ PACX_COMPOSITE_TEMPLATE_CONFIG Config
    )
{
    RtlZeroMemory(Config, sizeof(ACX_COMPOSITE_TEMPLATE_CONFIG));
    Config->Size = ACX_STRUCTURE_SIZE(ACX_COMPOSITE_TEMPLATE_CONFIG);
    Config->Flags = AcxCompositeTemplateConfigNoFlags;
}

typedef
_IRQL_requires_max_(PASSIVE_LEVEL)
WDFAPI
ACXMANAGER
(NTAPI *PFN_ACXGETMANAGER)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXCONTEXT Context
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
FORCEINLINE
ACXMANAGER
AcxGetManager(
    _In_
    ACXCONTEXT Context
    )
{
    return ((PFN_ACXGETMANAGER) AcxFunctions[AcxGetManagerTableIndex])(AcxDriverGlobals, Context);
}

typedef
_Must_inspect_result_
_IRQL_requires_max_(PASSIVE_LEVEL)
WDFAPI
NTSTATUS
(NTAPI *PFN_ACXMANAGERADDCOMPOSITETEMPLATE)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXMANAGER Manager,
    _In_
    ACXCOMPOSITETEMPLATE Template
    );

_Must_inspect_result_
_IRQL_requires_max_(PASSIVE_LEVEL)
FORCEINLINE
NTSTATUS
AcxManagerAddCompositeTemplate(
    _In_
    ACXMANAGER Manager,
    _In_
    ACXCOMPOSITETEMPLATE Template
    )
{
    return ((PFN_ACXMANAGERADDCOMPOSITETEMPLATE) AcxFunctions[AcxManagerAddCompositeTemplateTableIndex])(AcxDriverGlobals, Manager, Template);
}

typedef
_Must_inspect_result_
_IRQL_requires_max_(PASSIVE_LEVEL)
WDFAPI
NTSTATUS
(NTAPI *PFN_ACXMANAGERREMOVECOMPOSITETEMPLATE)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXMANAGER Manager,
    _In_
    ACXCOMPOSITETEMPLATE Template
    );

_Must_inspect_result_
_IRQL_requires_max_(PASSIVE_LEVEL)
FORCEINLINE
NTSTATUS
AcxManagerRemoveCompositeTemplate(
    _In_
    ACXMANAGER Manager,
    _In_
    ACXCOMPOSITETEMPLATE Template
    )
{
    return ((PFN_ACXMANAGERREMOVECOMPOSITETEMPLATE) AcxFunctions[AcxManagerRemoveCompositeTemplateTableIndex])(AcxDriverGlobals, Manager, Template);
}

typedef
_Must_inspect_result_
_IRQL_requires_max_(PASSIVE_LEVEL)
WDFAPI
NTSTATUS
(NTAPI *PFN_ACXCOMPOSITETEMPLATECREATE)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    WDFDRIVER Driver,
    _In_
    PWDF_OBJECT_ATTRIBUTES Attributes,
    _In_
    PACX_COMPOSITE_TEMPLATE_CONFIG Config,
    _Out_
    ACXCOMPOSITETEMPLATE* Template
    );

_Must_inspect_result_
_IRQL_requires_max_(PASSIVE_LEVEL)
FORCEINLINE
NTSTATUS
AcxCompositeTemplateCreate(
    _In_
    WDFDRIVER Driver,
    _In_
    PWDF_OBJECT_ATTRIBUTES Attributes,
    _In_
    PACX_COMPOSITE_TEMPLATE_CONFIG Config,
    _Out_
    ACXCOMPOSITETEMPLATE* Template
    )
{
    return ((PFN_ACXCOMPOSITETEMPLATECREATE) AcxFunctions[AcxCompositeTemplateCreateTableIndex])(AcxDriverGlobals, Driver, Attributes, Config, Template);
}

typedef
_Must_inspect_result_
_IRQL_requires_max_(PASSIVE_LEVEL)
WDFAPI
NTSTATUS
(NTAPI *PFN_ACXCOMPOSITETEMPLATEASSIGNCIRCUITS)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXCOMPOSITETEMPLATE CompositeTemplate,
    _In_reads_(CircuitTemplatesCount)
    ACXCIRCUITTEMPLATE* CircuitTemplates,
    _In_
    ULONG CircuitTemplatesCount
    );

_Must_inspect_result_
_IRQL_requires_max_(PASSIVE_LEVEL)
FORCEINLINE
NTSTATUS
AcxCompositeTemplateAssignCircuits(
    _In_
    ACXCOMPOSITETEMPLATE CompositeTemplate,
    _In_reads_(CircuitTemplatesCount)
    ACXCIRCUITTEMPLATE* CircuitTemplates,
    _In_
    ULONG CircuitTemplatesCount
    )
{
    return ((PFN_ACXCOMPOSITETEMPLATEASSIGNCIRCUITS) AcxFunctions[AcxCompositeTemplateAssignCircuitsTableIndex])(AcxDriverGlobals, CompositeTemplate, CircuitTemplates, CircuitTemplatesCount);
}

typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
WDFAPI
VOID
(NTAPI *PFN_ACXCOMPOSITETEMPLATESETCORECIRCUIT)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXCOMPOSITETEMPLATE CompositeTemplate,
    _In_
    ACXCIRCUITTEMPLATE CircuitTemplates
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
FORCEINLINE
VOID
AcxCompositeTemplateSetCoreCircuit(
    _In_
    ACXCOMPOSITETEMPLATE CompositeTemplate,
    _In_
    ACXCIRCUITTEMPLATE CircuitTemplates
    )
{
    ((PFN_ACXCOMPOSITETEMPLATESETCORECIRCUIT) AcxFunctions[AcxCompositeTemplateSetCoreCircuitTableIndex])(AcxDriverGlobals, CompositeTemplate, CircuitTemplates);
}

typedef
_Must_inspect_result_
_IRQL_requires_max_(PASSIVE_LEVEL)
WDFAPI
NTSTATUS
(NTAPI *PFN_ACXCIRCUITTEMPLATECREATE)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    WDFDRIVER Driver,
    _In_
    PWDF_OBJECT_ATTRIBUTES Attributes,
    _In_
    PACX_CIRCUIT_TEMPLATE_CONFIG Config,
    _Out_
    ACXCIRCUITTEMPLATE* Template
    );

_Must_inspect_result_
_IRQL_requires_max_(PASSIVE_LEVEL)
FORCEINLINE
NTSTATUS
AcxCircuitTemplateCreate(
    _In_
    WDFDRIVER Driver,
    _In_
    PWDF_OBJECT_ATTRIBUTES Attributes,
    _In_
    PACX_CIRCUIT_TEMPLATE_CONFIG Config,
    _Out_
    ACXCIRCUITTEMPLATE* Template
    )
{
    return ((PFN_ACXCIRCUITTEMPLATECREATE) AcxFunctions[AcxCircuitTemplateCreateTableIndex])(AcxDriverGlobals, Driver, Attributes, Config, Template);
}

#endif

WDF_EXTERN_C_END

#endif
