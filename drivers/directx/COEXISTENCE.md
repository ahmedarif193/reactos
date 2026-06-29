# XDDM/WDDM Display Driver Coexistence

**Document:** Display ownership handoff and driver coexistence layer design
**Source base:** ReactOS, branch `down-uefi-path`
**Applies to:** `drivers/directx/dxgkrnl/`, `win32ss/drivers/videoprt/`

---

## 1. Overview

ReactOS supports two GPU driver models simultaneously:

- **XDDM** (XP Display Driver Model): miniport calls `VideoPortInitialize` in
  `videoprt.sys`, exposes `HwFindAdapter`/`HwInitialize`/`HwStartIO` callbacks,
  and communicates with win32k via IOCTLs on `\Device\VideoN`.

- **WDDM** (Windows Display Driver Model): miniport calls `DxgkInitialize` in
  `dxgkrnl.sys`, exposes `DxgkDdiAddDevice`/`DxgkDdiStartDevice` callbacks,
  and communicates with win32k via the `DXGK_INTERFACE` function pointer table.

These models must coexist because:

- Legacy hardware (VGA, Bochs, VMware SVGA) only has XDDM miniports.
- New or emulated hardware targeting WDDM 1.0 uses the WDDM path.
- Mixed systems (e.g., a WDDM primary GPU + XDDM secondary VGA adapter)
  must work without the two stacks interfering.

---

## 2. Key Detection Mechanism

The sole discriminator between the two models is which DriverObjectExtension
was allocated on the miniport's `DRIVER_OBJECT`:

| Port driver | Extension type | First ULONG value |
|-------------|---------------|-------------------|
| `videoprt.sys` | `VIDEO_PORT_DRIVER_EXTENSION` | `HwInitDataSize` (NT4 ≈ 0x68, W2k ≈ 0xB8, XP ≈ 0x190) |
| `dxgkrnl.sys` | `DXGKRNL_MINIPORT_CONTEXT` | `InitData.Version` (WDDM ≥ 0x1052) |

Both structures are allocated with the same unique-ID key (`DriverObject`
itself) via `IoAllocateDriverObjectExtension`.  Any extension whose first
`ULONG` is `< DXGKDDI_INTERFACE_VERSION_VISTA (0x1052)` is XDDM; otherwise
WDDM.

Additionally, `HKLM\System\CurrentControlSet\Services\<driver>\WDDMCapable`
(REG_DWORD) can be set during driver installation to explicitly declare
WDDM capability (`VidPortQueryWddmCapableFromRegistry`).

---

## 3. Coexistence Scenario: Pure WDDM System

**Configuration:** Single GPU with WDDM miniport; no XDDM miniport present.

**Boot sequence:**

1. **FreeLOADer / UEFI phase:** GOP framebuffer active; no kernel driver.
2. **InbvDriverInitialize (early kernel):** Captures
   `LOADER_PARAMETER_FRAMEBUFFER` from the loader block; sets
   `InbvGopInfoValid = TRUE`; calls `VidSetBootGraphicsPreservation(TRUE)`.
   Boot progress bar/logo is displayed on the raw GOP framebuffer.
3. **PnP Manager:** Matches GPU PCI device to WDDM miniport INF.  Calls
   `DxgkpAddDevice` (installed by `DxgkInitialize`).
   - dxgkrnl creates FDO, calls `DxgkDdiAddDevice`.
   - `IRP_MN_START_DEVICE` → `DxgkAdapterStart` → `DxgkDdiStartDevice`.
4. **DxgkDdiStartDevice:** Miniport calls
   `DxgkCbAcquirePostDisplayOwnership(DeviceHandle, &DisplayInfo)`.
   - dxgkrnl calls `InbvHasValidGopFrameBuffer()` / `InbvGetGopFrameBufferInfo()`.
   - Translates EFI GOP pixel format to `D3DDDIFORMAT`.
   - Fills `DXGK_DISPLAY_INFORMATION` with GOP framebuffer address, size,
     and pixel format.
   - Calls `InbvAcquireDisplayOwnership()` to release InbV's hold on the
     framebuffer.
   - Returns `STATUS_SUCCESS` with a populated `DXGK_DISPLAY_INFORMATION`.
5. **Miniport:** Takes over the GOP framebuffer (can scan out existing pixels
   without a blank screen), then programs the display pipeline.
6. **win32k:** Opens the WDDM display device, receives `DXGK_INTERFACE`,
   and uses the WDDM DDI path from this point forward.

**XDDM involvement:** None.  `videoprt.sys` is loaded but its `IntVideoPortAddDevice`
callback returns `STATUS_DEVICE_ALREADY_ATTACHED` if dxgkrnl already attached
(guarded by `VidPortCheckWddmFdoPresent`).

---

## 4. Coexistence Scenario: Pure XDDM System

**Configuration:** GPU with XDDM miniport only (e.g., Bochs VGA, VBE).

**Boot sequence:**

1. **FreeLOADer / BIOS VGA phase:** VGA text mode or VESA framebuffer.
2. **InbvDriverInitialize:** May capture a VESA framebuffer if present;
   otherwise falls back to legacy VGA INT10.
3. **PnP Manager:** Matches GPU to XDDM miniport INF.  Calls
   `IntVideoPortAddDevice` (installed by `VideoPortInitialize`).
   - `VidPortCheckWddmFdoPresent(PDO)` returns `FALSE` (dxgkrnl not present).
   - `VidPortIsWddmDriver(DriverObject)` returns `FALSE` (XDDM extension).
   - videoprt proceeds normally: creates `\Device\VideoN`, runs
     `HwFindAdapter`, sets up the device.
4. **IRP_MJ_CREATE from CSRSS:** videoprt calls
   `InbvNotifyDisplayOwnershipLost(IntVideoPortResetDisplayParameters)`,
   transferring ownership from InbV to the XDDM display stack.
5. **win32k:** Opens `\??\DISPLAY1`, receives IOCTL-based XDDM interface,
   loads `InstalledDisplayDrivers` DLL, creates GDI surface.

**WDDM involvement:** None.  `dxgkrnl.sys` may be loaded but its
`DxgkpAddDevice` will find `MpCtx->InitData.Version < 0x1052` and return
`STATUS_NOT_SUPPORTED`, leaving the device to videoprt.

---

## 5. Coexistence Scenario: Mixed WDDM Primary + XDDM Secondary

**Configuration:** Two GPUs — GPU0 is WDDM (primary), GPU1 is XDDM (secondary
VGA adapter or legacy card).

**Boot sequence:**

1. FreeLOADer uses GPU0's GOP framebuffer (primary output).
2. Kernel init: InbV captures GOP framebuffer from GPU0.
3. PnP loads GPU0's WDDM miniport:
   - `DxgkpAddDevice` creates dxgkrnl FDO above GPU0's PDO.
   - `DxgkCbAcquirePostDisplayOwnership` returns GPU0's GOP framebuffer info.
   - `InbvAcquireDisplayOwnership()` called; InbV releases GPU0 framebuffer.
4. PnP loads GPU1's XDDM miniport:
   - `IntVideoPortAddDevice` runs for GPU1's PDO.
   - `VidPortCheckWddmFdoPresent(GPU1_PDO)` returns `FALSE` (dxgkrnl did not
     attach to GPU1; they are separate PDOs).
   - videoprt proceeds normally for GPU1.
5. win32k opens both adapters:
   - GPU0: WDDM DDI via `DXGK_INTERFACE`.
   - GPU1: XDDM DDI via IOCTL on `\Device\Video1`.

**Critical invariant:** Each `PDEVICE_OBJECT` (PDO) can only be owned by one
FDO at a time.  dxgkrnl and videoprt never compete over the same PDO because:

- INF-based driver selection ensures each GPU has exactly one service entry
  (either videoprt or dxgkrnl, not both).
- If both are listed (during migration), the detection guards
  (`VidPortCheckWddmFdoPresent`, `DxgkLegacyDetect`) prevent double-attach.

**INBV ownership** during mixed mode:

- The primary display (GPU0, WDDM) calls `InbvAcquireDisplayOwnership()` via
  `DxgkCbAcquirePostDisplayOwnership`.
- The secondary display (GPU1, XDDM) calls `InbvNotifyDisplayOwnershipLost`
  via `IntVideoPortInbvInitialize` in dispatch.c.
- Only one call to `InbvNotifyDisplayOwnershipLost` should be made (for the
  primary display owner).  In the mixed scenario, WDDM takes ownership first
  via `InbvAcquireDisplayOwnership`; videoprt's subsequent XDDM call sets
  INBV state to LOST (already-owned → no-op for the primary side).

---

## 6. Boot Display Handoff Sequence (Detail)

This documents the exact sequence from firmware framebuffer to WDDM miniport
framebuffer ownership.

```
[Firmware / UEFI]
  GOP.SetMode(preferredMode)            -- selects resolution
  GOP framebuffer: PA=0xFD000000        -- physical address of linear FB

[FreeLOADer]
  UefiVidInitialize()
    gop->GetMode(&ModeInfo)
    Extension->GopFramebuffer.FrameBufferBase = fb_base
    Extension->GopFramebuffer.HorizontalResolution = 1024
    Extension->GopFramebuffer.VerticalResolution   = 768
    Extension->GopFramebuffer.PixelsPerScanLine    = 1024
    Extension->GopFramebuffer.PixelFormat          = 1 (BGRX)

[ntoskrnl — Phase 0 init]
  InbvDriverInitialize(LoaderBlock, ...)
    InbvGopFramebuffer = Extension->GopFramebuffer
    InbvGopInfoValid   = TRUE
    VidSetBootGraphicsPreservation(TRUE)
    VidInitialize() / bootvid.dll initialize

[PnP — WDDM miniport AddDevice]
  DxgkpAddDevice(DriverObject, PDO)
    IoCreateDevice(...)  -> FDO
    DxgkDdiAddDevice(PDO) -> MiniportDeviceContext
    IoAttachDeviceToDeviceStack(FDO, PDO)

[PnP — IRP_MN_START_DEVICE]
  DxgkAdapterStart(Adapter, Resources, Translated)
    DxgkpFillInterface(Adapter, &Interface)
      Interface.DxgkCbAcquirePostDisplayOwnership = DxgkCbAcquirePostDisplayOwnership
    DxgkDdiStartDevice(MiniportCtx, &StartInfo, &Interface,
                       &NumSources, &NumChildren)
      |
      +--> [miniport calls DxgkCbAcquirePostDisplayOwnership]
             DxgkCbAcquirePostDisplayOwnership(DeviceHandle, &DisplayInfo)
               InbvHasValidGopFrameBuffer()   -- TRUE
               InbvGetGopFrameBufferInfo(&Fb) -- copies InbvGopFramebuffer
               Translate: PixelFormat=1 -> D3DDDIFMT_X8R8G8B8
               DisplayInfo.Width         = 1024
               DisplayInfo.Height        = 768
               DisplayInfo.Pitch         = 1024 * 4 = 4096
               DisplayInfo.ColorFormat   = D3DDDIFMT_X8R8G8B8
               DisplayInfo.PhysicAddress = {0, 0xFD000000}
               InbvAcquireDisplayOwnership()  -- InbV state: OWNED -> OWNED
                                              -- (stops InbV from writing)
               return STATUS_SUCCESS
      |
      +--> miniport: map PA 0xFD000000 via MmMapIoSpace
      +--> miniport: scan out existing pixels (no blank screen flash)
      +--> miniport: program display pipeline for final mode

[win32k]
  Opens GPU device
  Receives DXGK_INTERFACE from dxgkrnl
  Uses WDDM DDI from this point forward
  Boot screen is never visible again
```

---

## 7. Implementation Files

| File | Role |
|------|------|
| `win32ss/drivers/videoprt/wddm_detect.c` | `VidPortIsWddmDriver`, `VidPortHandoffToWddm`, `VidPortCheckWddmFdoPresent`, `VidPortQueryWddmCapableFromRegistry` |
| `win32ss/drivers/videoprt/dispatch.c` | Modified `IntVideoPortAddDevice` to call WDDM guards |
| `win32ss/drivers/videoprt/videoprt.h` | Declarations for wddm_detect.c functions |
| `drivers/directx/dxgkrnl/adapter.c` | `DxgkCbAcquirePostDisplayOwnership` (real InbV query), `DxgkpAddDevice` (XDDM guard) |
| `drivers/directx/dxgkrnl/legacy.c` | `DxgkLegacyDetect`, `DxgkLegacyDetach` |
| `drivers/directx/dxgkrnl/dxgkrnl_private.h` | Declarations for legacy.c functions |

---

## 8. Registry Key: WDDMCapable

Setup tools can write the following value to declare driver capability:

```
HKLM\SYSTEM\CurrentControlSet\Services\<miniport>\WDDMCapable
  Type:  REG_DWORD
  Value: 1 = WDDM capable (route through dxgkrnl)
         0 or absent = XDDM (use videoprt)
```

This is read by `VidPortQueryWddmCapableFromRegistry()` in videoprt.  It is an
optional override; the primary detection remains the DriverObjectExtension
Version field comparison.

---

## 9. Error Returns and PnP Re-try Semantics

| Scenario | Status code | Effect |
|----------|------------|--------|
| videoprt detects dxgkrnl FDO present | `STATUS_DEVICE_ALREADY_ATTACHED` | PnP skips this driver; device already owned |
| videoprt detects WDDM miniport | `STATUS_NOT_SUPPORTED` | PnP tries next driver in compatible list |
| dxgkrnl detects XDDM miniport | `STATUS_NOT_SUPPORTED` | PnP tries next driver (videoprt) |
| `DxgkLegacyDetach` after FDO creation | `STATUS_NOT_SUPPORTED` | FDO removed; PnP re-tries with videoprt |
| `DxgkCbAcquirePostDisplayOwnership` no GOP FB | `STATUS_SUCCESS`, zeroed struct | Miniport cold-starts pipeline |

---

*End of COEXISTENCE.md*
