/******************************Module*Header*******************************\
* Module Name: bdd.cxx
*
* Basic Display Driver functions implementation
*
*
* Copyright (c) 2010 Microsoft Corporation
\**************************************************************************/


#include "BDD.hxx"

#define KMDOD_SOURCE_BYTES_PER_PIXEL 4

/* ---- VBE DISPI register definitions (bochs-display / QEMU stdvga) ---- */
#define BDD_VBE_DISPI_IOPORT_INDEX  0x01CE
#define BDD_VBE_DISPI_IOPORT_DATA   0x01CF

#define BDD_VBE_DISPI_INDEX_ID      0
#define BDD_VBE_DISPI_INDEX_XRES    1
#define BDD_VBE_DISPI_INDEX_YRES    2
#define BDD_VBE_DISPI_INDEX_BPP     3
#define BDD_VBE_DISPI_INDEX_ENABLE  4

#define BDD_VBE_DISPI_ENABLED       0x01
#define BDD_VBE_DISPI_LFB_ENABLED   0x40

#define BDD_VBE_DISPI_ID_MIN        0xB0C0
#define BDD_VBE_DISPI_ID_MAX        0xB0C5

/* Non-paged: called from SystemDisplayEnable (bugcheck path) */
static USHORT
BddVbeDispiRead(USHORT Index)
{
    WRITE_PORT_USHORT((PUSHORT)(ULONG_PTR)BDD_VBE_DISPI_IOPORT_INDEX, Index);
    return READ_PORT_USHORT((PUSHORT)(ULONG_PTR)BDD_VBE_DISPI_IOPORT_DATA);
}

/* Non-paged: called from SystemDisplayEnable (bugcheck path) */
static VOID
BddVbeDispiWrite(USHORT Index, USHORT Value)
{
    WRITE_PORT_USHORT((PUSHORT)(ULONG_PTR)BDD_VBE_DISPI_IOPORT_INDEX, Index);
    WRITE_PORT_USHORT((PUSHORT)(ULONG_PTR)BDD_VBE_DISPI_IOPORT_DATA, Value);
}

static BOOLEAN
BddRectHasValidOrder(_In_ CONST RECT* Rect)
{
    return (Rect->left <= Rect->right) && (Rect->top <= Rect->bottom);
}

static BOOLEAN
BddClipRectToSurface(
    _Inout_ RECT* Rect,
    _In_ LONG Width,
    _In_ LONG Height)
{
    if (Rect->left < 0)
        Rect->left = 0;
    if (Rect->top < 0)
        Rect->top = 0;
    if (Rect->right > Width)
        Rect->right = Width;
    if (Rect->bottom > Height)
        Rect->bottom = Height;

    return (Rect->left < Rect->right) && (Rect->top < Rect->bottom);
}

static VOID
BddMakeFullSurfaceRect(
    _Out_ RECT* Rect,
    _In_ LONG Width,
    _In_ LONG Height)
{
    Rect->left = 0;
    Rect->top = 0;
    Rect->right = Width;
    Rect->bottom = Height;
}

static NTSTATUS
BddValidateMoveRect(
    _In_ CONST D3DKMT_MOVE_RECT* Move,
    _In_ LONG Width,
    _In_ LONG Height)
{
    LONG MoveWidth;
    LONG MoveHeight;

    if (!BddRectHasValidOrder(&Move->DestRect))
    {
        return STATUS_INVALID_PARAMETER;
    }

    MoveWidth = Move->DestRect.right - Move->DestRect.left;
    MoveHeight = Move->DestRect.bottom - Move->DestRect.top;
    if (MoveWidth <= 0 || MoveHeight <= 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (Move->DestRect.left < 0 ||
        Move->DestRect.top < 0 ||
        Move->DestRect.right > Width ||
        Move->DestRect.bottom > Height)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (Move->SourcePoint.x < 0 ||
        Move->SourcePoint.y < 0 ||
        Move->SourcePoint.x > (Width - MoveWidth) ||
        Move->SourcePoint.y > (Height - MoveHeight))
    {
        return STATUS_INVALID_PARAMETER;
    }

    return STATUS_SUCCESS;
}

#pragma code_seg("PAGE")


BASIC_DISPLAY_DRIVER::BASIC_DISPLAY_DRIVER(_In_ DEVICE_OBJECT* pPhysicalDeviceObject) : m_pPhysicalDevice(pPhysicalDeviceObject),
                                                                                        m_MonitorPowerState(PowerDeviceD0),
                                                                                        m_AdapterPowerState(PowerDeviceD0)
{
    PAGED_CODE();
    *((UINT*)&m_Flags) = 0;
    m_Flags._LastFlag = TRUE;
    RtlZeroMemory(&m_DxgkInterface, sizeof(m_DxgkInterface));
    RtlZeroMemory(&m_StartInfo, sizeof(m_StartInfo));
    RtlZeroMemory(m_CurrentModes, sizeof(m_CurrentModes));
    RtlZeroMemory(&m_DeviceInfo, sizeof(m_DeviceInfo));


    for (UINT i=0;i<MAX_VIEWS;i++)
    {
        m_HardwareBlt[i].Initialize(this,i);
    }
}

BASIC_DISPLAY_DRIVER::~BASIC_DISPLAY_DRIVER()
{
    PAGED_CODE();


    CleanUp();
}


NTSTATUS BASIC_DISPLAY_DRIVER::StartDevice(_In_  DXGK_START_INFO*   pDxgkStartInfo,
                                           _In_  DXGKRNL_INTERFACE* pDxgkInterface,
                                           _Out_ ULONG*             pNumberOfViews,
                                           _Out_ ULONG*             pNumberOfChildren)
{
    PAGED_CODE();

    if (pDxgkStartInfo == NULL ||
        pDxgkInterface == NULL ||
        pNumberOfViews == NULL ||
        pNumberOfChildren == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    *pNumberOfViews = 0;
    *pNumberOfChildren = 0;

    CleanUp();
    m_Flags.DriverStarted = FALSE;
    m_Flags.VbeFallbackActive = FALSE;

    RtlCopyMemory(&m_StartInfo, pDxgkStartInfo, sizeof(m_StartInfo));
    RtlCopyMemory(&m_DxgkInterface, pDxgkInterface, sizeof(m_DxgkInterface));
    RtlZeroMemory(m_CurrentModes, sizeof(m_CurrentModes));
    m_CurrentModes[0].DispInfo.TargetId = D3DDDI_ID_UNINITIALIZED;

    // Get device information from OS.
    NTSTATUS Status = m_DxgkInterface.DxgkCbGetDeviceInformation(m_DxgkInterface.DeviceHandle, &m_DeviceInfo);
    if (!NT_SUCCESS(Status))
    {
        BDD_LOG_ASSERTION1("DxgkCbGetDeviceInformation failed with status 0x%I64x",
                           Status);
        return Status;
    }

    // Ignore return value, since it's not the end of the world if we failed to write these values to the registry
    RegisterHWInfo();

    Status = CheckHardware();
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    // This sample driver only uses the frame buffer of the POST device. DxgkCbAcquirePostDisplayOwnership
    // gives you the frame buffer address and ensures that no one else is drawing to it. Be sure to give it back!
    Status = m_DxgkInterface.DxgkCbAcquirePostDisplayOwnership(m_DxgkInterface.DeviceHandle, &(m_CurrentModes[0].DispInfo));
    if (!NT_SUCCESS(Status) || m_CurrentModes[0].DispInfo.Width == 0)
    {
        // POST ownership failed or returned no display -- try VBE DISPI
        // register programming as a fallback (covers BIOS boot on
        // bochs-display / QEMU stdvga).
        if (!TryVbeDisplayFallback())
        {
            return NT_SUCCESS(Status) ? STATUS_UNSUCCESSFUL : Status;
        }
    }

    Status = InitializeCurrentMode(0);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    // Map the framebuffer so BlackOutScreen and presents can access it
    if (m_CurrentModes[0].FrameBuffer.Ptr == NULL &&
        m_CurrentModes[0].DispInfo.PhysicAddress.QuadPart != 0)
    {
        ULONG FrameBufferLength;

        Status = GetFrameBufferLength(0, &FrameBufferLength);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }

        Status = MapFrameBuffer(m_CurrentModes[0].DispInfo.PhysicAddress,
                                FrameBufferLength,
                                &m_CurrentModes[0].FrameBuffer.Ptr);
        if (NT_SUCCESS(Status))
        {
            m_CurrentModes[0].Flags.FrameBufferIsActive = TRUE;
        }
    }

    if (m_CurrentModes[0].FrameBuffer.Ptr == NULL ||
        !m_CurrentModes[0].Flags.FrameBufferIsActive)
    {
        CleanUp();
        return NT_SUCCESS(Status) ? STATUS_INVALID_DEVICE_STATE : Status;
    }

    BlackOutScreen(0);

    // Retrieve EDID
    if (!m_Flags.EDID_Attempted)
    {
        GetEdid(0);
    }

    m_Flags.DriverStarted = TRUE;
    *pNumberOfViews = MAX_VIEWS;
    *pNumberOfChildren = MAX_CHILDREN;

    return STATUS_SUCCESS;
}

NTSTATUS BASIC_DISPLAY_DRIVER::InitializeCurrentMode(D3DDDI_VIDEO_PRESENT_SOURCE_ID SourceId)
{
    PAGED_CODE();

    if (SourceId >= MAX_VIEWS)
    {
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE;
    }

    CURRENT_BDD_MODE* pMode = &m_CurrentModes[SourceId];
    if (pMode->DispInfo.Width == 0 ||
        pMode->DispInfo.Height == 0 ||
        pMode->DispInfo.Pitch == 0 ||
        pMode->DispInfo.PhysicAddress.QuadPart == 0 ||
        BPPFromPixelFormat(pMode->DispInfo.ColorFormat) == 0)
    {
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE_MODE;
    }

    ULONG MinimumPitch;
    NTSTATUS Status = RtlULongMult(pMode->DispInfo.Width,
                                   BPPFromPixelFormat(pMode->DispInfo.ColorFormat) / BITS_PER_BYTE,
                                   &MinimumPitch);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    if (pMode->DispInfo.Pitch < MinimumPitch)
    {
        return STATUS_GRAPHICS_INVALID_STRIDE;
    }

    pMode->Rotation = D3DKMDT_VPPR_IDENTITY;
    pMode->Scaling = D3DKMDT_VPPS_IDENTITY;
    pMode->SrcModeWidth = pMode->DispInfo.Width;
    pMode->SrcModeHeight = pMode->DispInfo.Height;
    pMode->Flags.SourceNotVisible = FALSE;
    pMode->Flags.FullscreenPresent = TRUE;
    pMode->Flags.FrameBufferIsActive = FALSE;
    pMode->Flags.DoNotMapOrUnmap = FALSE;
    pMode->ZeroedOutStart.QuadPart = 0;
    pMode->ZeroedOutEnd.QuadPart = 0;

    return STATUS_SUCCESS;
}

NTSTATUS BASIC_DISPLAY_DRIVER::GetFrameBufferLength(D3DDDI_VIDEO_PRESENT_SOURCE_ID SourceId,
                                                    _Out_ ULONG* Length) const
{
    PAGED_CODE();

    if (Length == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    *Length = 0;

    if (SourceId >= MAX_VIEWS)
    {
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE;
    }

    return RtlULongMult(m_CurrentModes[SourceId].DispInfo.Height,
                       m_CurrentModes[SourceId].DispInfo.Pitch,
                       Length);
}

BOOLEAN BASIC_DISPLAY_DRIVER::TryVbeDisplayFallback()
{
    PAGED_CODE();

    /*
     * Probe VBE DISPI ID register to detect bochs-display / QEMU stdvga.
     * If detected, program a linear framebuffer mode and populate
     * m_CurrentModes[0].DispInfo so the driver can proceed.
     */
    USHORT DispiId = BddVbeDispiRead(BDD_VBE_DISPI_INDEX_ID);
    if (DispiId < BDD_VBE_DISPI_ID_MIN || DispiId > BDD_VBE_DISPI_ID_MAX)
    {
        return FALSE;
    }

    /* Find framebuffer physical address from PCI memory BAR */
    if (m_DeviceInfo.TranslatedResourceList == NULL)
    {
        return FALSE;
    }

    PHYSICAL_ADDRESS FbPhysAddr = {{0}};
    ULONG FbWidth = DEFAULT_WIDTH;   /* 1024 */
    ULONG FbHeight = DEFAULT_HEIGHT; /* 768 */
    ULONG FbBpp = 32;
    ULONG MinFbSize;
    NTSTATUS Status = RtlULongMult(FbWidth, FbHeight, &MinFbSize);
    if (!NT_SUCCESS(Status) ||
        !NT_SUCCESS(RtlULongMult(MinFbSize, FbBpp / BITS_PER_BYTE, &MinFbSize)))
    {
        return FALSE;
    }

    PCM_FULL_RESOURCE_DESCRIPTOR FullDesc = &m_DeviceInfo.TranslatedResourceList->List[0];
    for (ULONG i = 0; i < FullDesc->PartialResourceList.Count; i++)
    {
        PCM_PARTIAL_RESOURCE_DESCRIPTOR Desc = &FullDesc->PartialResourceList.PartialDescriptors[i];
        if (Desc->Type == CmResourceTypeMemory && Desc->u.Memory.Length >= MinFbSize)
        {
            FbPhysAddr = Desc->u.Memory.Start;
            break;
        }
    }

    if (FbPhysAddr.QuadPart == 0)
    {
        return FALSE;
    }

    /* Program VBE linear framebuffer mode */
    BddVbeDispiWrite(BDD_VBE_DISPI_INDEX_ENABLE, 0);
    BddVbeDispiWrite(BDD_VBE_DISPI_INDEX_XRES, (USHORT)FbWidth);
    BddVbeDispiWrite(BDD_VBE_DISPI_INDEX_YRES, (USHORT)FbHeight);
    BddVbeDispiWrite(BDD_VBE_DISPI_INDEX_BPP,  (USHORT)FbBpp);
    BddVbeDispiWrite(BDD_VBE_DISPI_INDEX_ENABLE, BDD_VBE_DISPI_ENABLED | BDD_VBE_DISPI_LFB_ENABLED);

    m_CurrentModes[0].DispInfo.Width         = FbWidth;
    m_CurrentModes[0].DispInfo.Height        = FbHeight;
    m_CurrentModes[0].DispInfo.Pitch         = FbWidth * (FbBpp / 8);
    m_CurrentModes[0].DispInfo.ColorFormat   = D3DDDIFMT_X8R8G8B8;
    m_CurrentModes[0].DispInfo.PhysicAddress = FbPhysAddr;
    m_CurrentModes[0].DispInfo.TargetId      = 0;

    m_Flags.VbeFallbackActive = TRUE;

    return TRUE;
}

NTSTATUS BASIC_DISPLAY_DRIVER::StopDevice(VOID)
{
    PAGED_CODE();

    CleanUp();

    m_Flags.DriverStarted = FALSE;
    m_Flags.VbeFallbackActive = FALSE;

    return STATUS_SUCCESS;
}

VOID BASIC_DISPLAY_DRIVER::CleanUp()
{
    PAGED_CODE();

    for (UINT Source = 0; Source < MAX_VIEWS; ++Source)
    {
        if (m_CurrentModes[Source].FrameBuffer.Ptr)
        {
            ULONG FrameBufferLength;
            NTSTATUS Status = GetFrameBufferLength(Source, &FrameBufferLength);

            if (NT_SUCCESS(Status))
            {
                UnmapFrameBuffer(m_CurrentModes[Source].FrameBuffer.Ptr, FrameBufferLength);
            }

            m_CurrentModes[Source].FrameBuffer.Ptr = NULL;
        }

        m_CurrentModes[Source].Flags.FrameBufferIsActive = FALSE;
        m_CurrentModes[Source].Flags.FullscreenPresent = FALSE;
        m_CurrentModes[Source].ZeroedOutStart.QuadPart = 0;
        m_CurrentModes[Source].ZeroedOutEnd.QuadPart = 0;
    }

}


NTSTATUS BASIC_DISPLAY_DRIVER::DispatchIoRequest(_In_  ULONG                 VidPnSourceId,
                                                 _In_  VIDEO_REQUEST_PACKET* pVideoRequestPacket)
{
    PAGED_CODE();

    BDD_ASSERT(pVideoRequestPacket != NULL);
    BDD_ASSERT(VidPnSourceId < MAX_VIEWS);

    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS BASIC_DISPLAY_DRIVER::SetPowerState(_In_  ULONG              HardwareUid,
                                             _In_  DEVICE_POWER_STATE DevicePowerState,
                                             _In_  POWER_ACTION       ActionType)
{
    PAGED_CODE();

    UNREFERENCED_PARAMETER(ActionType);

    BDD_ASSERT((HardwareUid < MAX_CHILDREN) || (HardwareUid == DISPLAY_ADAPTER_HW_ID));

    if (HardwareUid == DISPLAY_ADAPTER_HW_ID)
    {
        if (DevicePowerState == PowerDeviceD0)
        {

            // When returning from D3 the device visibility defined to be off for all targets
            if (m_AdapterPowerState == PowerDeviceD3)
            {
                DXGKARG_SETVIDPNSOURCEVISIBILITY Visibility;
                Visibility.VidPnSourceId = D3DDDI_ID_ALL;
                Visibility.Visible = FALSE;
                SetVidPnSourceVisibility(&Visibility);
            }
        }

        // Store new adapter power state
        m_AdapterPowerState = DevicePowerState;

        // There is nothing to do to specifically power up/down the display adapter
        return STATUS_SUCCESS;
    }
    else
    {
        // TODO: This is where the specified monitor should be powered up/down
        NOTHING;
        return STATUS_SUCCESS;
    }
}

NTSTATUS BASIC_DISPLAY_DRIVER::QueryChildRelations(_Out_writes_bytes_(ChildRelationsSize) DXGK_CHILD_DESCRIPTOR* pChildRelations,
                                                   _In_                             ULONG                  ChildRelationsSize)
{
    PAGED_CODE();

    BDD_ASSERT(pChildRelations != NULL);

    // The last DXGK_CHILD_DESCRIPTOR in the array of pChildRelations must remain zeroed out, so we subtract this from the count
    ULONG ChildRelationsCount = (ChildRelationsSize / sizeof(DXGK_CHILD_DESCRIPTOR)) - 1;
    BDD_ASSERT(ChildRelationsCount <= MAX_CHILDREN);

    for (UINT ChildIndex = 0; ChildIndex < ChildRelationsCount; ++ChildIndex)
    {
        pChildRelations[ChildIndex].ChildDeviceType = TypeVideoOutput;
        pChildRelations[ChildIndex].ChildCapabilities.HpdAwareness = HpdAwarenessInterruptible;
        pChildRelations[ChildIndex].ChildCapabilities.Type.VideoOutput.InterfaceTechnology = m_CurrentModes[0].Flags.IsInternal ? D3DKMDT_VOT_INTERNAL : D3DKMDT_VOT_OTHER;
        pChildRelations[ChildIndex].ChildCapabilities.Type.VideoOutput.MonitorOrientationAwareness = D3DKMDT_MOA_NONE;
        pChildRelations[ChildIndex].ChildCapabilities.Type.VideoOutput.SupportsSdtvModes = FALSE;
        // TODO: Replace 0 with the actual ACPI ID of the child device, if available
        pChildRelations[ChildIndex].AcpiUid = 0;
        pChildRelations[ChildIndex].ChildUid = ChildIndex;
    }

    return STATUS_SUCCESS;
}

NTSTATUS BASIC_DISPLAY_DRIVER::QueryChildStatus(_Inout_ DXGK_CHILD_STATUS* pChildStatus,
                                                _In_    BOOLEAN            NonDestructiveOnly)
{
    PAGED_CODE();

    UNREFERENCED_PARAMETER(NonDestructiveOnly);
    BDD_ASSERT(pChildStatus != NULL);
    BDD_ASSERT(pChildStatus->ChildUid < MAX_CHILDREN);

    switch (pChildStatus->Type)
    {
        case StatusConnection:
        {
            // HpdAwarenessInterruptible was reported since HpdAwarenessNone is deprecated.
            // However, BDD has no knowledge of HotPlug events, so just always return connected.
            pChildStatus->HotPlug.Connected = IsDriverActive();
            return STATUS_SUCCESS;
        }

        case StatusRotation:
        {
            // D3DKMDT_MOA_NONE was reported, so this should never be called
            BDD_LOG_ERROR0("Child status being queried for StatusRotation even though D3DKMDT_MOA_NONE was reported");
            return STATUS_INVALID_PARAMETER;
        }

        default:
        {
            BDD_LOG_WARNING1("Unknown pChildStatus->Type (0x%I64x) requested.", pChildStatus->Type);
            return STATUS_NOT_SUPPORTED;
        }
    }
}

// EDID retrieval
NTSTATUS BASIC_DISPLAY_DRIVER::QueryDeviceDescriptor(_In_    ULONG                   ChildUid,
                                                     _Inout_ DXGK_DEVICE_DESCRIPTOR* pDeviceDescriptor)
{
    PAGED_CODE();

    BDD_ASSERT(pDeviceDescriptor != NULL);
    BDD_ASSERT(ChildUid < MAX_CHILDREN);

    // If we haven't successfully retrieved an EDID yet (invalid ones are ok, so long as it was retrieved)
    if (!m_Flags.EDID_Attempted)
    {
        GetEdid(ChildUid);
    }

    if (!m_Flags.EDID_Retrieved || !m_Flags.EDID_ValidHeader || !m_Flags.EDID_ValidChecksum)
    {
        // Report no EDID if a valid one wasn't retrieved
        return STATUS_GRAPHICS_CHILD_DESCRIPTOR_NOT_SUPPORTED;
    }
    else if (pDeviceDescriptor->DescriptorOffset == 0)
    {
        // Only the base block is supported
        RtlCopyMemory(pDeviceDescriptor->DescriptorBuffer,
                      m_EDIDs[ChildUid],
                      min(pDeviceDescriptor->DescriptorLength, EDID_V1_BLOCK_SIZE));

        return STATUS_SUCCESS;
    }
    else
    {
        return STATUS_MONITOR_NO_MORE_DESCRIPTOR_DATA;
    }
}

NTSTATUS BASIC_DISPLAY_DRIVER::QueryAdapterInfo(_In_ CONST DXGKARG_QUERYADAPTERINFO* pQueryAdapterInfo)
{
    PAGED_CODE();

    BDD_ASSERT(pQueryAdapterInfo != NULL);

    switch (pQueryAdapterInfo->Type)
    {
        case DXGKQAITYPE_DRIVERCAPS:
        {
            if (pQueryAdapterInfo->OutputDataSize < sizeof(DXGK_DRIVERCAPS))
            {
                BDD_LOG_ERROR2("pQueryAdapterInfo->OutputDataSize (0x%I64x) is smaller than sizeof(DXGK_DRIVERCAPS) (0x%I64x)", pQueryAdapterInfo->OutputDataSize, sizeof(DXGK_DRIVERCAPS));
                return STATUS_BUFFER_TOO_SMALL;
            }

            DXGK_DRIVERCAPS* pDriverCaps = (DXGK_DRIVERCAPS*)pQueryAdapterInfo->pOutputData;

            // Nearly all fields must be initialized to zero, so zero out to start and then change those that are non-zero.
            // Fields are zero since BDD is Display-Only and therefore does not support any of the render related fields.
            // It also doesn't support hardware interrupts, gamma ramps, etc.
            RtlZeroMemory(pDriverCaps, sizeof(DXGK_DRIVERCAPS));

            pDriverCaps->WDDMVersion = DXGKDDI_WDDMv1_2;
            pDriverCaps->HighestAcceptableAddress.QuadPart = -1;

            pDriverCaps->SupportNonVGA = TRUE;
            pDriverCaps->SupportSmoothRotation = TRUE;

            return STATUS_SUCCESS;
        }

        case DXGKQAITYPE_DISPLAY_DRIVERCAPS_EXTENSION:
        {
            DXGK_DISPLAY_DRIVERCAPS_EXTENSION* pDriverDisplayCaps;

            if (pQueryAdapterInfo->OutputDataSize < sizeof(*pDriverDisplayCaps))
            {
                BDD_LOG_ERROR2("pQueryAdapterInfo->OutputDataSize (0x%I64x) is smaller than sizeof(DXGK_DISPLAY_DRIVERCAPS_EXTENSION) (0x%I64x)",
                               pQueryAdapterInfo->OutputDataSize,
                               sizeof(DXGK_DISPLAY_DRIVERCAPS_EXTENSION));

                return STATUS_INVALID_PARAMETER;
            }

            pDriverDisplayCaps = (DXGK_DISPLAY_DRIVERCAPS_EXTENSION*)pQueryAdapterInfo->pOutputData;

            // Reset all caps values
            RtlZeroMemory(pDriverDisplayCaps, pQueryAdapterInfo->OutputDataSize);

            // We claim to support virtual display mode.
            pDriverDisplayCaps->VirtualModeSupport = 1;

            return STATUS_SUCCESS;
        }

        default:
        {
            // BDD does not need to support any other adapter information types
            BDD_LOG_WARNING1("Unknown QueryAdapterInfo Type (0x%I64x) requested", pQueryAdapterInfo->Type);
            return STATUS_NOT_SUPPORTED;
        }
    }
}


NTSTATUS BASIC_DISPLAY_DRIVER::CheckHardware()
{
    PAGED_CODE();

    /*
     * Verify that the translated resources can plausibly back a linear
     * framebuffer. Devices without resources are still allowed through so
     * DxgkCbAcquirePostDisplayOwnership can report the real POST state.
     */
    if (m_DeviceInfo.TranslatedResourceList == NULL)
    {
        /* No resources at all -- still allow; POST path will decide */
        return STATUS_SUCCESS;
    }

    ULONG MinFbSize;
    NTSTATUS Status = RtlULongMult(MIN_WIDTH, MIN_HEIGHT, &MinFbSize);
    if (!NT_SUCCESS(Status) ||
        !NT_SUCCESS(RtlULongMult(MinFbSize, 32 / BITS_PER_BYTE, &MinFbSize)))
    {
        return STATUS_INTEGER_OVERFLOW;
    }

    PCM_FULL_RESOURCE_DESCRIPTOR FullDesc = &m_DeviceInfo.TranslatedResourceList->List[0];
    BOOLEAN HasSuitableMemory = FALSE;

    for (ULONG i = 0; i < FullDesc->PartialResourceList.Count; i++)
    {
        PCM_PARTIAL_RESOURCE_DESCRIPTOR Desc = &FullDesc->PartialResourceList.PartialDescriptors[i];
        if (Desc->Type == CmResourceTypeMemory && Desc->u.Memory.Length >= MinFbSize)
        {
            HasSuitableMemory = TRUE;
            break;
        }
    }

    if (!HasSuitableMemory)
    {
        BDD_LOG_ERROR0("CheckHardware: no memory BAR large enough for minimum framebuffer");
        return STATUS_GRAPHICS_DRIVER_MISMATCH;
    }

    return STATUS_SUCCESS;
}

// Even though Sample Basic Display Driver does not support hardware cursors, and reports such
// in QueryAdapterInfo. This function can still be called to set the pointer to not visible
NTSTATUS BASIC_DISPLAY_DRIVER::SetPointerPosition(_In_ CONST DXGKARG_SETPOINTERPOSITION* pSetPointerPosition)
{
    PAGED_CODE();

    if (pSetPointerPosition == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (pSetPointerPosition->VidPnSourceId >= MAX_VIEWS)
    {
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE;
    }

    if (pSetPointerPosition->Flags.Reserved != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (!(pSetPointerPosition->Flags.Visible))
    {
        return STATUS_SUCCESS;
    }

    return STATUS_NOT_SUPPORTED;
}

// Basic Sample Display Driver does not support hardware cursors, and reports such
// in QueryAdapterInfo. Therefore this function should never be called.
NTSTATUS BASIC_DISPLAY_DRIVER::SetPointerShape(_In_ CONST DXGKARG_SETPOINTERSHAPE* pSetPointerShape)
{
    PAGED_CODE();
    UINT FormatCount;

    if (pSetPointerShape == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (pSetPointerShape->VidPnSourceId >= MAX_VIEWS)
    {
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE;
    }

    FormatCount = pSetPointerShape->Flags.Monochrome +
                  pSetPointerShape->Flags.Color +
                  pSetPointerShape->Flags.MaskedColor;

    if (pSetPointerShape->Flags.Reserved != 0 ||
        FormatCount != 1 ||
        pSetPointerShape->Width == 0 ||
        pSetPointerShape->Height == 0 ||
        pSetPointerShape->Pitch == 0 ||
        pSetPointerShape->pPixels == NULL ||
        pSetPointerShape->XHot >= pSetPointerShape->Width ||
        pSetPointerShape->YHot >= pSetPointerShape->Height)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (pSetPointerShape->Flags.Color || pSetPointerShape->Flags.MaskedColor)
    {
        ULONG MinimumPitch;
        NTSTATUS Status = RtlULongMult(pSetPointerShape->Width,
                                       KMDOD_SOURCE_BYTES_PER_PIXEL,
                                       &MinimumPitch);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }

        if (pSetPointerShape->Pitch < MinimumPitch)
        {
            return STATUS_INVALID_PARAMETER;
        }
    }

    return STATUS_NOT_SUPPORTED;
}

NTSTATUS BASIC_DISPLAY_DRIVER::PresentDisplayOnly(_In_ CONST DXGKARG_PRESENT_DISPLAYONLY* pPresentDisplayOnly)
{
    PAGED_CODE();
    NTSTATUS Status = STATUS_SUCCESS;
    CURRENT_BDD_MODE* pMode;
    RECT FullScreenDirtyRect;
    RECT* ClippedDirtyRects = NULL;
    RECT* DirtyRects = NULL;
    D3DKMT_MOVE_RECT* MoveRects = NULL;
    ULONG DirtyRectCount;
    ULONG MoveRectCount;
    LONG SurfaceWidth;
    LONG SurfaceHeight;
    UINT DstBitPerPixel;
    UINT DstBytesPerPixel;
    ULONG ExpectedPitch;
    BYTE* pDst;

    if (pPresentDisplayOnly == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (pPresentDisplayOnly->VidPnSourceId >= MAX_VIEWS)
    {
        return STATUS_GRAPHICS_INVALID_VIDEO_PRESENT_SOURCE;
    }

    pMode = &m_CurrentModes[pPresentDisplayOnly->VidPnSourceId];
    DirtyRectCount = pPresentDisplayOnly->NumDirtyRects;
    MoveRectCount = pPresentDisplayOnly->NumMoves;
    DirtyRects = pPresentDisplayOnly->pDirtyRect;
    MoveRects = pPresentDisplayOnly->pMoves;

    if (pPresentDisplayOnly->Flags.Reserved != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (pPresentDisplayOnly->pfnPresentDisplayOnlyProgress != NULL)
    {
        return STATUS_NOT_SUPPORTED;
    }

    if ((pPresentDisplayOnly->NumMoves != 0 && pPresentDisplayOnly->pMoves == NULL) ||
        (pPresentDisplayOnly->NumDirtyRects != 0 && pPresentDisplayOnly->pDirtyRect == NULL) ||
        ((pPresentDisplayOnly->NumMoves != 0 ||
          pPresentDisplayOnly->NumDirtyRects != 0 ||
          pMode->Flags.FullscreenPresent) &&
         pPresentDisplayOnly->pSource == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (pPresentDisplayOnly->BytesPerPixel != KMDOD_SOURCE_BYTES_PER_PIXEL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    // If it is in monitor off state or source is not supposed to be visible, don't present anything to the screen
    if ((m_MonitorPowerState > PowerDeviceD0) ||
        (pMode->Flags.SourceNotVisible))
    {
        return STATUS_SUCCESS;
    }

    if (!pMode->Flags.FrameBufferIsActive ||
        pMode->FrameBuffer.Ptr == NULL ||
        pMode->SrcModeWidth == 0 ||
        pMode->SrcModeHeight == 0 ||
        pMode->SrcModeWidth > (UINT)0x7fffffff ||
        pMode->SrcModeHeight > (UINT)0x7fffffff)
    {
        return STATUS_INVALID_DEVICE_STATE;
    }

    Status = RtlULongMult(pMode->SrcModeWidth,
                          KMDOD_SOURCE_BYTES_PER_PIXEL,
                          &ExpectedPitch);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    if (pPresentDisplayOnly->Pitch <= 0 ||
        (ULONG)pPresentDisplayOnly->Pitch != ExpectedPitch)
    {
        return STATUS_GRAPHICS_INVALID_STRIDE;
    }

    SurfaceWidth = (LONG)pMode->SrcModeWidth;
    SurfaceHeight = (LONG)pMode->SrcModeHeight;

    if (MoveRectCount != 0)
    {
        ULONG MoveRectBytes;

        Status = RtlULongMult(MoveRectCount,
                              (ULONG)sizeof(D3DKMT_MOVE_RECT),
                              &MoveRectBytes);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }

        if (MoveRectBytes == 0)
        {
            return STATUS_INVALID_PARAMETER;
        }
    }

    for (ULONG MoveIndex = 0; MoveIndex < MoveRectCount; ++MoveIndex)
    {
        Status = BddValidateMoveRect(&MoveRects[MoveIndex],
                                     SurfaceWidth,
                                     SurfaceHeight);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }
    }

    if (pMode->Flags.FullscreenPresent)
    {
        BddMakeFullSurfaceRect(&FullScreenDirtyRect, SurfaceWidth, SurfaceHeight);
        DirtyRects = &FullScreenDirtyRect;
        DirtyRectCount = 1;
        MoveRects = NULL;
        MoveRectCount = 0;
    }
    else if (DirtyRectCount != 0)
    {
        ULONG DirtyRectBytes;
        ULONG EffectiveDirtyRects = 0;

        Status = RtlULongMult(DirtyRectCount, (ULONG)sizeof(RECT), &DirtyRectBytes);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }

        ClippedDirtyRects = (RECT*)ExAllocatePoolWithTag(PagedPool,
                                                        DirtyRectBytes,
                                                        BDDTAG);
        if (ClippedDirtyRects == NULL)
        {
            return STATUS_NO_MEMORY;
        }

        for (ULONG RectIndex = 0; RectIndex < DirtyRectCount; ++RectIndex)
        {
            RECT Rect = pPresentDisplayOnly->pDirtyRect[RectIndex];

            if (!BddRectHasValidOrder(&Rect))
            {
                Status = STATUS_INVALID_PARAMETER;
                goto PresentDisplayOnlyExit;
            }

            if (BddClipRectToSurface(&Rect, SurfaceWidth, SurfaceHeight))
            {
                ClippedDirtyRects[EffectiveDirtyRects++] = Rect;
            }
        }

        DirtyRects = ClippedDirtyRects;
        DirtyRectCount = EffectiveDirtyRects;
    }

    if (MoveRectCount == 0 && DirtyRectCount == 0)
    {
        Status = STATUS_SUCCESS;
        goto PresentDisplayOnlyExit;
    }

    DstBitPerPixel = BPPFromPixelFormat(pMode->DispInfo.ColorFormat);
    DstBytesPerPixel = DstBitPerPixel / BITS_PER_BYTE;
    if (DstBitPerPixel == 0 || DstBytesPerPixel == 0)
    {
        Status = STATUS_INVALID_DEVICE_STATE;
        goto PresentDisplayOnlyExit;
    }

    pDst = (BYTE*)pMode->FrameBuffer.Ptr;
    if (pMode->Scaling == D3DKMDT_VPPS_CENTERED)
    {
        ULONG TopRows;
        ULONG LeftPixels;
        ULONG VerticalOffset;
        ULONG HorizontalOffset;
        ULONG CenterOffset;

        if (pMode->DispInfo.Width < pMode->SrcModeWidth ||
            pMode->DispInfo.Height < pMode->SrcModeHeight)
        {
            Status = STATUS_INVALID_DEVICE_STATE;
            goto PresentDisplayOnlyExit;
        }

        TopRows = (pMode->DispInfo.Height - pMode->SrcModeHeight) / 2;
        LeftPixels = (pMode->DispInfo.Width - pMode->SrcModeWidth) / 2;

        Status = RtlULongMult(TopRows, pMode->DispInfo.Pitch, &VerticalOffset);
        if (!NT_SUCCESS(Status))
        {
            goto PresentDisplayOnlyExit;
        }

        Status = RtlULongMult(LeftPixels, DstBytesPerPixel, &HorizontalOffset);
        if (!NT_SUCCESS(Status))
        {
            goto PresentDisplayOnlyExit;
        }

        Status = RtlULongAdd(VerticalOffset, HorizontalOffset, &CenterOffset);
        if (!NT_SUCCESS(Status))
        {
            goto PresentDisplayOnlyExit;
        }

        pDst += CenterOffset;
    }
    else if (pMode->Scaling != D3DKMDT_VPPS_IDENTITY)
    {
        Status = STATUS_NOT_SUPPORTED;
        goto PresentDisplayOnlyExit;
    }

    // If actual pixels are coming through, BlackOutScreen must clear the full physical range next time.
    pMode->ZeroedOutStart.QuadPart = 0;
    pMode->ZeroedOutEnd.QuadPart = 0;

    Status = m_HardwareBlt[pPresentDisplayOnly->VidPnSourceId].ExecutePresentDisplayOnly(
                                                        pDst,
                                                        DstBitPerPixel,
                                                        (BYTE*)pPresentDisplayOnly->pSource,
                                                        pPresentDisplayOnly->BytesPerPixel,
                                                        pPresentDisplayOnly->Pitch,
                                                        MoveRectCount,
                                                        MoveRects,
                                                        DirtyRectCount,
                                                        DirtyRects,
                                                        pPresentDisplayOnly->Flags.Rotate ?
                                                            pMode->Rotation :
                                                            D3DKMDT_VPPR_IDENTITY);
    if (NT_SUCCESS(Status))
    {
        pMode->Flags.FullscreenPresent = FALSE;
    }

PresentDisplayOnlyExit:
    if (ClippedDirtyRects != NULL)
    {
        ExFreePoolWithTag(ClippedDirtyRects, BDDTAG);
    }

    return Status;
}

NTSTATUS BASIC_DISPLAY_DRIVER::StopDeviceAndReleasePostDisplayOwnership(_In_  D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
                                                                        _Out_ DXGK_DISPLAY_INFORMATION*      pDisplayInfo)
{
    PAGED_CODE();

    BDD_ASSERT(TargetId < MAX_CHILDREN);


    D3DDDI_VIDEO_PRESENT_SOURCE_ID SourceId = FindSourceForTarget(TargetId, TRUE);

    // In case BDD is the next driver to run, the monitor should not be off, since
    // this could cause the BIOS to hang when the EDID is retrieved on Start.
    if (m_MonitorPowerState > PowerDeviceD0)
    {
        SetPowerState(TargetId, PowerDeviceD0, PowerActionNone);
    }

    // The driver has to black out the display and ensure it is visible when releasing ownership
    BlackOutScreen(SourceId);

    *pDisplayInfo = m_CurrentModes[SourceId].DispInfo;

    return StopDevice();
}

NTSTATUS BASIC_DISPLAY_DRIVER::QueryVidPnHWCapability(_Inout_ DXGKARG_QUERYVIDPNHWCAPABILITY* pVidPnHWCaps)
{
    PAGED_CODE();

    BDD_ASSERT(pVidPnHWCaps != NULL);
    BDD_ASSERT(pVidPnHWCaps->SourceId < MAX_VIEWS);
    BDD_ASSERT(pVidPnHWCaps->TargetId < MAX_CHILDREN);

    pVidPnHWCaps->VidPnHWCaps.DriverRotation             = 1; // BDD does rotation in software
    pVidPnHWCaps->VidPnHWCaps.DriverScaling              = 0; // BDD does not support scaling
    pVidPnHWCaps->VidPnHWCaps.DriverCloning              = 0; // BDD does not support clone
    pVidPnHWCaps->VidPnHWCaps.DriverColorConvert         = 1; // BDD does color conversions in software
    pVidPnHWCaps->VidPnHWCaps.DriverLinkedAdapaterOutput = 0; // BDD does not support linked adapters
    pVidPnHWCaps->VidPnHWCaps.DriverRemoteDisplay        = 0; // BDD does not support remote displays

    return STATUS_SUCCESS;
}

NTSTATUS BASIC_DISPLAY_DRIVER::GetEdid(D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId)
{
    PAGED_CODE();

    BDD_ASSERT_CHK(!m_Flags.EDID_Attempted);

    NTSTATUS Status;
    RtlZeroMemory(m_EDIDs[TargetId], sizeof(m_EDIDs[TargetId]));

    m_Flags.EDID_Attempted = TRUE;
    m_Flags.EDID_Retrieved = FALSE;
    m_Flags.EDID_ValidHeader = FALSE;
    m_Flags.EDID_ValidChecksum = FALSE;

    /*
     * Strategy 1: Try reading an EDID override from the device registry.
     * Windows stores EDID overrides under the device's software key as
     * a binary value named "EDID_OVERRIDE".
     */
    HANDLE DevRegKey = NULL;
    Status = IoOpenDeviceRegistryKey(m_pPhysicalDevice, PLUGPLAY_REGKEY_DRIVER,
                                     KEY_READ, &DevRegKey);
    if (NT_SUCCESS(Status))
    {
        UNICODE_STRING ValueName;
        RtlInitUnicodeString(&ValueName, L"EDID_OVERRIDE");

        /* Allocate buffer for KEY_VALUE_PARTIAL_INFORMATION + 128 bytes EDID */
        ULONG BufSize = sizeof(KEY_VALUE_PARTIAL_INFORMATION) + EDID_V1_BLOCK_SIZE;
        PKEY_VALUE_PARTIAL_INFORMATION ValueInfo =
            (PKEY_VALUE_PARTIAL_INFORMATION)ExAllocatePoolWithTag(PagedPool, BufSize, BDDTAG);

        if (ValueInfo != NULL)
        {
            ULONG ResultLength = 0;
            Status = ZwQueryValueKey(DevRegKey, &ValueName, KeyValuePartialInformation,
                                     ValueInfo, BufSize, &ResultLength);

            if (NT_SUCCESS(Status) &&
                ValueInfo->Type == REG_BINARY &&
                ValueInfo->DataLength >= EDID_V1_BLOCK_SIZE)
            {
                RtlCopyMemory(m_EDIDs[TargetId], ValueInfo->Data, EDID_V1_BLOCK_SIZE);
                m_Flags.EDID_Retrieved = TRUE;
            }

            ExFreePoolWithTag(ValueInfo, BDDTAG);
        }

        ZwClose(DevRegKey);
    }

    /*
     * Strategy 2: If no override, try reading from the hardware key
     * where the EDID may have been cached by a previous driver or
     * the PnP manager.
     */
    if (!m_Flags.EDID_Retrieved)
    {
        DevRegKey = NULL;
        Status = IoOpenDeviceRegistryKey(m_pPhysicalDevice, PLUGPLAY_REGKEY_DEVICE,
                                         KEY_READ, &DevRegKey);
        if (NT_SUCCESS(Status))
        {
            UNICODE_STRING ValueName;
            RtlInitUnicodeString(&ValueName, L"EDID");

            ULONG BufSize = sizeof(KEY_VALUE_PARTIAL_INFORMATION) + EDID_V1_BLOCK_SIZE;
            PKEY_VALUE_PARTIAL_INFORMATION ValueInfo =
                (PKEY_VALUE_PARTIAL_INFORMATION)ExAllocatePoolWithTag(PagedPool, BufSize, BDDTAG);

            if (ValueInfo != NULL)
            {
                ULONG ResultLength = 0;
                Status = ZwQueryValueKey(DevRegKey, &ValueName, KeyValuePartialInformation,
                                         ValueInfo, BufSize, &ResultLength);

                if (NT_SUCCESS(Status) &&
                    ValueInfo->Type == REG_BINARY &&
                    ValueInfo->DataLength >= EDID_V1_BLOCK_SIZE)
                {
                    RtlCopyMemory(m_EDIDs[TargetId], ValueInfo->Data, EDID_V1_BLOCK_SIZE);
                    m_Flags.EDID_Retrieved = TRUE;
                }

                ExFreePoolWithTag(ValueInfo, BDDTAG);
            }

            ZwClose(DevRegKey);
        }
    }

    /* Validate the EDID if we got one */
    if (m_Flags.EDID_Retrieved)
    {
        m_Flags.EDID_ValidHeader = IsEdidHeaderValid(m_EDIDs[TargetId]);
        m_Flags.EDID_ValidChecksum = IsEdidChecksumValid(m_EDIDs[TargetId]);
    }

    return STATUS_SUCCESS;
}

VOID BASIC_DISPLAY_DRIVER::BlackOutScreen(D3DDDI_VIDEO_PRESENT_SOURCE_ID SourceId)
{
    PAGED_CODE();


    UINT ScreenHeight = m_CurrentModes[SourceId].DispInfo.Height;
    UINT ScreenPitch = m_CurrentModes[SourceId].DispInfo.Pitch;

    PHYSICAL_ADDRESS NewPhysAddrStart = m_CurrentModes[SourceId].DispInfo.PhysicAddress;
    PHYSICAL_ADDRESS NewPhysAddrEnd;
    NewPhysAddrEnd.QuadPart = NewPhysAddrStart.QuadPart + (ScreenHeight * ScreenPitch);

    if (m_CurrentModes[SourceId].Flags.FrameBufferIsActive)
    {
        BYTE* MappedAddr = reinterpret_cast<BYTE*>(m_CurrentModes[SourceId].FrameBuffer.Ptr);

        // Zero any memory at the start that hasn't been zeroed recently
        if (NewPhysAddrStart.QuadPart < m_CurrentModes[SourceId].ZeroedOutStart.QuadPart)
        {
            if (NewPhysAddrEnd.QuadPart < m_CurrentModes[SourceId].ZeroedOutStart.QuadPart)
            {
                // No overlap
                RtlZeroMemory(MappedAddr, ScreenHeight * ScreenPitch);
            }
            else
            {
                RtlZeroMemory(MappedAddr, (UINT)(m_CurrentModes[SourceId].ZeroedOutStart.QuadPart - NewPhysAddrStart.QuadPart));
            }
        }

        // Zero any memory at the end that hasn't been zeroed recently
        if (NewPhysAddrEnd.QuadPart > m_CurrentModes[SourceId].ZeroedOutEnd.QuadPart)
        {
            if (NewPhysAddrStart.QuadPart > m_CurrentModes[SourceId].ZeroedOutEnd.QuadPart)
            {
                // No overlap
                // NOTE: When actual pixels were the most recent thing drawn, ZeroedOutStart & ZeroedOutEnd will both be 0
                // and this is the path that will be used to black out the current screen.
                RtlZeroMemory(MappedAddr, ScreenHeight * ScreenPitch);
            }
            else
            {
                RtlZeroMemory(MappedAddr, (UINT)(NewPhysAddrEnd.QuadPart - m_CurrentModes[SourceId].ZeroedOutEnd.QuadPart));
            }
        }
    }

    m_CurrentModes[SourceId].ZeroedOutStart.QuadPart = NewPhysAddrStart.QuadPart;
    m_CurrentModes[SourceId].ZeroedOutEnd.QuadPart = NewPhysAddrEnd.QuadPart;

}

NTSTATUS BASIC_DISPLAY_DRIVER::WriteHWInfoStr(_In_ HANDLE DevInstRegKeyHandle, _In_ PCWSTR pszwValueName, _In_ PCSTR pszValue)
{
    PAGED_CODE();

    NTSTATUS Status;
    ANSI_STRING AnsiStrValue;
    UNICODE_STRING UnicodeStrValue;
    UNICODE_STRING UnicodeStrValueName;

    // ZwSetValueKey wants the ValueName as a UNICODE_STRING
    RtlInitUnicodeString(&UnicodeStrValueName, pszwValueName);

    // REG_SZ is for WCHARs, there is no equivalent for CHARs
    // Use the ansi/unicode conversion functions to get from PSTR to PWSTR
    RtlInitAnsiString(&AnsiStrValue, pszValue);
    Status = RtlAnsiStringToUnicodeString(&UnicodeStrValue, &AnsiStrValue, TRUE);
    if (!NT_SUCCESS(Status))
    {
        BDD_LOG_ERROR1("RtlAnsiStringToUnicodeString failed with Status: 0x%I64x", Status);
        return Status;
    }

    // Write the value to the registry
    Status = ZwSetValueKey(DevInstRegKeyHandle,
                           &UnicodeStrValueName,
                           0,
                           REG_SZ,
                           UnicodeStrValue.Buffer,
                           UnicodeStrValue.MaximumLength);

    // Free the earlier allocated unicode string
    RtlFreeUnicodeString(&UnicodeStrValue);

    if (!NT_SUCCESS(Status))
    {
        BDD_LOG_ERROR1("ZwSetValueKey failed with Status: 0x%I64x", Status);
    }

    return Status;
}

NTSTATUS BASIC_DISPLAY_DRIVER::RegisterHWInfo()
{
    PAGED_CODE();

    NTSTATUS Status;
    HANDLE DevInstRegKeyHandle = NULL;
    UNICODE_STRING ValueNameMemorySize;
    DWORD MemorySize = 0; // BDD has no access to video memory

    // TODO: Replace these strings with proper information
    PCSTR StrHWInfoChipType = "Replace with the chip name";
    PCSTR StrHWInfoDacType = "Replace with the DAC name or identifier (ID)";
    PCSTR StrHWInfoAdapterString = "Replace with the name of the adapter";
    PCSTR StrHWInfoBiosString = "Replace with information about the BIOS";

    Status = IoOpenDeviceRegistryKey(m_pPhysicalDevice, PLUGPLAY_REGKEY_DRIVER, KEY_SET_VALUE, &DevInstRegKeyHandle);
    if (!NT_SUCCESS(Status))
    {
        BDD_LOG_ERROR2("IoOpenDeviceRegistryKey failed for PDO: 0x%I64x, Status: 0x%I64x", m_pPhysicalDevice, Status);
        return Status;
    }

    Status = WriteHWInfoStr(DevInstRegKeyHandle, L"HardwareInformation.ChipType", StrHWInfoChipType);
    if (!NT_SUCCESS(Status))
    {
        goto RegisterHWInfoExit;
    }

    Status = WriteHWInfoStr(DevInstRegKeyHandle, L"HardwareInformation.DacType", StrHWInfoDacType);
    if (!NT_SUCCESS(Status))
    {
        goto RegisterHWInfoExit;
    }

    Status = WriteHWInfoStr(DevInstRegKeyHandle, L"HardwareInformation.AdapterString", StrHWInfoAdapterString);
    if (!NT_SUCCESS(Status))
    {
        goto RegisterHWInfoExit;
    }

    Status = WriteHWInfoStr(DevInstRegKeyHandle, L"HardwareInformation.BiosString", StrHWInfoBiosString);
    if (!NT_SUCCESS(Status))
    {
        goto RegisterHWInfoExit;
    }

    // MemorySize is a ULONG, unlike the others which are all strings
    RtlInitUnicodeString(&ValueNameMemorySize, L"HardwareInformation.MemorySize");
    Status = ZwSetValueKey(DevInstRegKeyHandle,
                           &ValueNameMemorySize,
                           0,
                           REG_DWORD,
                           &MemorySize,
                           sizeof(MemorySize));
    if (!NT_SUCCESS(Status))
    {
        BDD_LOG_ERROR1("ZwSetValueKey for MemorySize failed with Status: 0x%I64x", Status);
        goto RegisterHWInfoExit;
    }

RegisterHWInfoExit:
    if (DevInstRegKeyHandle != NULL)
    {
        ZwClose(DevInstRegKeyHandle);
    }

    return Status;
}

//
// Non-Paged Code
//
#pragma code_seg(push)
#pragma code_seg()
D3DDDI_VIDEO_PRESENT_SOURCE_ID BASIC_DISPLAY_DRIVER::FindSourceForTarget(D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId, BOOLEAN DefaultToZero)
{
    UNREFERENCED_PARAMETER(TargetId);
    BDD_ASSERT_CHK(TargetId < MAX_CHILDREN);

    for (UINT SourceId = 0; SourceId < MAX_VIEWS; ++SourceId)
    {
        if (m_CurrentModes[SourceId].FrameBuffer.Ptr != NULL)
        {
            return SourceId;
        }
    }

    return DefaultToZero ? 0 : D3DDDI_ID_UNINITIALIZED;
}

VOID BASIC_DISPLAY_DRIVER::DpcRoutine(VOID)
{
    m_DxgkInterface.DxgkCbNotifyDpc((HANDLE)m_DxgkInterface.DeviceHandle);
}

BOOLEAN BASIC_DISPLAY_DRIVER::InterruptRoutine(_In_  ULONG MessageNumber)
{
    UNREFERENCED_PARAMETER(MessageNumber);

    // BDD cannot handle interrupts
    return FALSE;
}

VOID BASIC_DISPLAY_DRIVER::ResetDevice(VOID)
{
}

// Must be Non-Paged, as it sets up the display for a bugcheck
NTSTATUS BASIC_DISPLAY_DRIVER::SystemDisplayEnable(_In_  D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
                                                   _In_  PDXGKARG_SYSTEM_DISPLAY_ENABLE_FLAGS Flags,
                                                   _Out_ UINT* pWidth,
                                                   _Out_ UINT* pHeight,
                                                   _Out_ D3DDDIFORMAT* pColorFormat)
{
    UNREFERENCED_PARAMETER(Flags);

    m_SystemDisplaySourceId = D3DDDI_ID_UNINITIALIZED;

    BDD_ASSERT((TargetId < MAX_CHILDREN) || (TargetId == D3DDDI_ID_UNINITIALIZED));

    // Find the frame buffer for displaying the bugcheck, if it was successfully mapped
    if (TargetId == D3DDDI_ID_UNINITIALIZED)
    {
        for (UINT SourceIdx = 0; SourceIdx < MAX_VIEWS; ++SourceIdx)
        {
            if (m_CurrentModes[SourceIdx].FrameBuffer.Ptr != NULL)
            {
                m_SystemDisplaySourceId = SourceIdx;
                break;
            }
        }
    }
    else
    {
        m_SystemDisplaySourceId = FindSourceForTarget(TargetId, FALSE);
    }

    if (m_SystemDisplaySourceId == D3DDDI_ID_UNINITIALIZED)
    {
        /*
         * No framebuffer is mapped. Try VBE DISPI register programming
         * to establish a minimal 640x480x32 display for the bugcheck
         * screen. This is non-paged code so we cannot call paged
         * helpers -- use direct port I/O only.
         */
        USHORT DispiId = BddVbeDispiRead(BDD_VBE_DISPI_INDEX_ID);
        if (DispiId >= BDD_VBE_DISPI_ID_MIN && DispiId <= BDD_VBE_DISPI_ID_MAX)
        {
            BddVbeDispiWrite(BDD_VBE_DISPI_INDEX_ENABLE, 0);
            BddVbeDispiWrite(BDD_VBE_DISPI_INDEX_XRES, MIN_WIDTH);
            BddVbeDispiWrite(BDD_VBE_DISPI_INDEX_YRES, MIN_HEIGHT);
            BddVbeDispiWrite(BDD_VBE_DISPI_INDEX_BPP,  32);
            BddVbeDispiWrite(BDD_VBE_DISPI_INDEX_ENABLE, BDD_VBE_DISPI_ENABLED | BDD_VBE_DISPI_LFB_ENABLED);

            /* Try to find a framebuffer from existing mode info */
            if (m_CurrentModes[0].DispInfo.PhysicAddress.QuadPart != 0)
            {
                PHYSICAL_ADDRESS FbPhys = m_CurrentModes[0].DispInfo.PhysicAddress;
                ULONG FbSize = MIN_WIDTH * MIN_HEIGHT * 4;
                PVOID FbVa = MmMapIoSpace(FbPhys, FbSize, MmWriteCombined);
                if (FbVa == NULL)
                    FbVa = MmMapIoSpace(FbPhys, FbSize, MmNonCached);

                if (FbVa != NULL)
                {
                    m_CurrentModes[0].DispInfo.Width = MIN_WIDTH;
                    m_CurrentModes[0].DispInfo.Height = MIN_HEIGHT;
                    m_CurrentModes[0].DispInfo.Pitch = MIN_WIDTH * 4;
                    m_CurrentModes[0].DispInfo.ColorFormat = D3DDDIFMT_X8R8G8B8;
                    m_CurrentModes[0].FrameBuffer.Ptr = FbVa;
                    m_CurrentModes[0].Flags.FrameBufferIsActive = TRUE;
                    m_CurrentModes[0].Rotation = D3DKMDT_VPPR_IDENTITY;
                    m_SystemDisplaySourceId = 0;
                }
            }
        }

        if (m_SystemDisplaySourceId == D3DDDI_ID_UNINITIALIZED)
        {
            return STATUS_UNSUCCESSFUL;
        }
    }

    if ((m_CurrentModes[m_SystemDisplaySourceId].Rotation == D3DKMDT_VPPR_ROTATE90) ||
        (m_CurrentModes[m_SystemDisplaySourceId].Rotation == D3DKMDT_VPPR_ROTATE270))
    {
        *pHeight = m_CurrentModes[m_SystemDisplaySourceId].DispInfo.Width;
        *pWidth = m_CurrentModes[m_SystemDisplaySourceId].DispInfo.Height;
    }
    else
    {
        *pWidth = m_CurrentModes[m_SystemDisplaySourceId].DispInfo.Width;
        *pHeight = m_CurrentModes[m_SystemDisplaySourceId].DispInfo.Height;
    }

    *pColorFormat = m_CurrentModes[m_SystemDisplaySourceId].DispInfo.ColorFormat;


    return STATUS_SUCCESS;
}

// Must be Non-Paged, as it is called to display the bugcheck screen
VOID BASIC_DISPLAY_DRIVER::SystemDisplayWrite(_In_reads_bytes_(SourceHeight * SourceStride) VOID* pSource,
                                              _In_ UINT SourceWidth,
                                              _In_ UINT SourceHeight,
                                              _In_ UINT SourceStride,
                                              _In_ INT PositionX,
                                              _In_ INT PositionY)
{

    // Rect will be Offset by PositionX/Y in the src to reset it back to 0
    RECT Rect;
    Rect.left = PositionX;
    Rect.top = PositionY;
    Rect.right =  Rect.left + SourceWidth;
    Rect.bottom = Rect.top + SourceHeight;

    // Set up destination blt info
    BLT_INFO DstBltInfo;
    DstBltInfo.pBits = m_CurrentModes[m_SystemDisplaySourceId].FrameBuffer.Ptr;
    DstBltInfo.Pitch = m_CurrentModes[m_SystemDisplaySourceId].DispInfo.Pitch;
    DstBltInfo.BitsPerPel = BPPFromPixelFormat(m_CurrentModes[m_SystemDisplaySourceId].DispInfo.ColorFormat);
    DstBltInfo.Offset.x = 0;
    DstBltInfo.Offset.y = 0;
    DstBltInfo.Rotation = m_CurrentModes[m_SystemDisplaySourceId].Rotation;
    DstBltInfo.Width = m_CurrentModes[m_SystemDisplaySourceId].DispInfo.Width;
    DstBltInfo.Height = m_CurrentModes[m_SystemDisplaySourceId].DispInfo.Height;

    // Set up source blt info
    BLT_INFO SrcBltInfo;
    SrcBltInfo.pBits = pSource;
    SrcBltInfo.Pitch = SourceStride;
    SrcBltInfo.BitsPerPel = 32;

    SrcBltInfo.Offset.x = -PositionX;
    SrcBltInfo.Offset.y = -PositionY;
    SrcBltInfo.Rotation = D3DKMDT_VPPR_IDENTITY;
    SrcBltInfo.Width = SourceWidth;
    SrcBltInfo.Height = SourceHeight;

    BltBits(&DstBltInfo,
            &SrcBltInfo,
            1, // NumRects
            &Rect);
}

#pragma code_seg(pop) // End Non-Paged Code
