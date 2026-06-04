#!/usr/bin/env bash
# Idempotently re-apply ALL tracked WDDM/DWM wiring that the reverter drops.
# Safe to run repeatedly. Run in the SAME bash invocation as every ninja call.
set -e
T=/home/ahmed/WorkDir/TTE/reactos_arm64
D=/home/ahmed/WorkDir/TTE/reactosv2_session2
cd "$T"

# ---- 1. The 5 add_subdirectory wiring lines (component build inclusion) ----
grep -q "add_subdirectory(directx)"   drivers/CMakeLists.txt            || echo "add_subdirectory(directx)" >> drivers/CMakeLists.txt
grep -q "add_subdirectory(wddm)"      win32ss/drivers/CMakeLists.txt    || echo "add_subdirectory(wddm)" >> win32ss/drivers/CMakeLists.txt
grep -q "add_subdirectory(dwm)"       base/system/CMakeLists.txt        || echo "add_subdirectory(dwm)" >> base/system/CMakeLists.txt
grep -q "add_subdirectory(d3d10warp)" dll/directx/CMakeLists.txt        || echo "add_subdirectory(d3d10warp)" >> dll/directx/CMakeLists.txt
grep -q "add_subdirectory(dwmapi)"    dll/win32/CMakeLists.txt          || echo "MISSING dwmapi wiring"  # dwmapi pre-exists
grep -q "add_subdirectory(dwmcore)"   dll/win32/CMakeLists.txt          || sed -i 's/    add_subdirectory(dwmapi)/    add_subdirectory(dwmapi)\n    add_subdirectory(dwmcore)/' dll/win32/CMakeLists.txt

# ---- 1b. Top-level CMakeLists: CDDB override registered LAST (softgpu wins) ----
if ! grep -q "wddm_cddb_override.inf" CMakeLists.txt; then
python3 - <<'PYEOF'
p='CMakeLists.txt'
s=open(p).read()
anchor='''    add_subdirectory(win32ss)

    # Create the registry hives
    create_registry_hives()'''
repl='''    add_subdirectory(win32ss)

    # WDDM bring-up: register the CDDB override LAST so the WDDM softgpu miniport
    # wins PCI\\\\VEN_1234&DEV_1111 over the legacy XPDM bochsmp (last-write-wins).
    if(ARCH STREQUAL "arm64")
        add_registry_inf(boot/bootdata/wddm_cddb_override.inf)
    endif()

    # Create the registry hives
    create_registry_hives()'''
assert anchor in s, "CMakeLists anchor missing"
s=s.replace(anchor, repl, 1)
open(p,'w').write(s)
print("CMakeLists.txt: CDDB override registration inserted")
PYEOF
fi

# ---- 1c. Top-level CMakeLists: rpi5dod ROOT-enum seed (DEFAULT ON, switchable) ----
# rpi5dod is the Pi5 WDDM display-only miniport.  It only binds when a
# ROOT\RPI5DOD PnP device instance exists in the Enum hive.  Registering
# boot/bootdata/rpi5dod_root.inf seeds that instance (Service=rpi5dod) LAST in
# the hive so PnP loads rpi5dod at boot -> dxgkrnl AddDevice -> Rpi5DodStartDevice
# -> DxgkCbAcquirePostDisplayOwnership (firmware GOP framebuffer).  With the seed
# present a PLAIN boot composites via rpi5dod (dwm opens the rpi5dod adapter and
# presents complete frames through D3DKMTPresent -> dxgkrnl -> rpi5dod); without
# it dwm gets STATUS_NO_SUCH_DEVICE (0xC000000E) opening the adapter and exits,
# leaving the rpi5vc4 XPDM desktop.
#
# DEFAULT is now rpi5dod-ON so the tear-free composited desktop comes up on a
# plain boot.  Selection precedence:
#   RPI5DOD_ROOT=1            -> force the seed ON  (rpi5dod composites).
#   RPI5DOD_ROOT=0            -> force the seed OFF (rpi5vc4-primary fallback).
#   scripts/.rpi5vc4_default  -> force OFF (a reverter-proof, UNTRACKED fallback
#                                flag: `touch scripts/.rpi5vc4_default` then
#                                rebuild to fall back to rpi5vc4 at any time).
#   neither set/present       -> DEFAULT ON (rpi5dod composites).
#
# This is reverter-safe: rpi5dod_root.inf is TRACKED + STAGED (survives a
# restore-from-index reverter), and the CMake registration is re-applied here
# every build, so both an atomic build and a stock flash_test.py rebuild produce
# a composing image unless explicitly switched back to rpi5vc4.
RPI5DOD_SEED_ON=1
if [ "${RPI5DOD_ROOT:-}" = "0" ]; then
    RPI5DOD_SEED_ON=0
elif [ -f scripts/.rpi5vc4_default ]; then
    RPI5DOD_SEED_ON=0
fi
if [ "$RPI5DOD_SEED_ON" = "1" ]; then
if ! grep -q "rpi5dod_root.inf" CMakeLists.txt; then
python3 - <<'PYEOF'
p='CMakeLists.txt'
s=open(p).read()
anchor='''    # Create the registry hives
    create_registry_hives()'''
repl='''    # RPi5 WDDM bring-up: seed ROOT\\\\RPI5DOD LAST so rpi5dod binds at boot.
    if(ARCH STREQUAL "arm64")
        add_registry_inf(boot/bootdata/rpi5dod_root.inf)
    endif()

    # Create the registry hives
    create_registry_hives()'''
assert anchor in s, "CMakeLists create_registry_hives anchor missing"
s=s.replace(anchor, repl, 1)
open(p,'w').write(s)
print("CMakeLists.txt: rpi5dod ROOT-enum seed registration inserted")
PYEOF
fi
echo "rpi5dod ROOT seed: ENABLED (default; rpi5dod composites). Set RPI5DOD_ROOT=0 or 'touch scripts/.rpi5vc4_default' to fall back to rpi5vc4."
else
# Deterministically REMOVE any seed registration so this build is
# rpi5vc4-primary (no ROOT\RPI5DOD device -> rpi5dod never binds ->
# rpi5vc4 owns the display, the proven working fallback).
if grep -q "rpi5dod_root.inf" CMakeLists.txt; then
python3 - <<'PYEOF'
p='CMakeLists.txt'
s=open(p).read()
block='''    # RPi5 WDDM bring-up: seed ROOT\\\\RPI5DOD LAST so rpi5dod binds at boot.
    if(ARCH STREQUAL "arm64")
        add_registry_inf(boot/bootdata/rpi5dod_root.inf)
    endif()

'''
s=s.replace(block, '', 1)
open(p,'w').write(s)
print("CMakeLists.txt: rpi5dod ROOT-enum seed registration removed (default)")
PYEOF
fi
echo "rpi5dod ROOT seed: DISABLED (rpi5vc4-primary fallback). Unset RPI5DOD_ROOT and remove scripts/.rpi5vc4_default for the default rpi5dod composite."
fi

grep -q "user/ntuser/dwm.c" win32ss/CMakeLists.txt || \
    sed -i 's#    user/ntuser/display.c#    user/ntuser/display.c\n    user/ntuser/dwm.c#' win32ss/CMakeLists.txt
grep -q "libcntpr wddm_bridge" win32ss/CMakeLists.txt || \
    sed -i 's#target_link_libraries(win32k ${PSEH_LIB} dxguid libcntpr)#target_link_libraries(win32k ${PSEH_LIB} dxguid libcntpr wddm_bridge)#' win32ss/CMakeLists.txt

# ---- 3. Full-file donor replacements (D3DKMT routing + svc tables + exports) ----
# Re-copy only if the target diverged back to the stock (smaller) version.
cmp -s "$D/win32ss/gdi/ntgdi/d3dkmt.c"   win32ss/gdi/ntgdi/d3dkmt.c   || cp "$D/win32ss/gdi/ntgdi/d3dkmt.c"   win32ss/gdi/ntgdi/d3dkmt.c
cmp -s "$D/win32ss/napi.h"               win32ss/napi.h               || cp "$D/win32ss/napi.h"               win32ss/napi.h
cmp -s "$D/win32ss/w32ksvc32.h"          win32ss/w32ksvc32.h          || cp "$D/win32ss/w32ksvc32.h"          win32ss/w32ksvc32.h
cmp -s "$D/win32ss/w32ksvc64.h"          win32ss/w32ksvc64.h          || cp "$D/win32ss/w32ksvc64.h"          win32ss/w32ksvc64.h
# gdi32.spec: ensure OpenAdapterFromGdiDisplayName forwards to the win32k syscall
# (stock target spec leaves it target-less -> dwm.exe gets STATUS_ENTRYPOINT_NOT_FOUND).
grep -q "D3DKMTOpenAdapterFromGdiDisplayName(ptr) NtGdiDdDDIOpenAdapterFromGdiDisplayName" win32ss/gdi/gdi32/gdi32.spec || \
    sed -i 's#@ stdcall -version=0x600+ D3DKMTOpenAdapterFromGdiDisplayName(ptr)$#@ stdcall -version=0x600+ D3DKMTOpenAdapterFromGdiDisplayName(ptr) NtGdiDdDDIOpenAdapterFromGdiDisplayName#' win32ss/gdi/gdi32/gdi32.spec

# gdi32.spec: keep target's stock forwarders, ADD only the WDDM 1.1/1.2 block.
if ! grep -q "D3DKMTAcquireKeyedMutex" win32ss/gdi/gdi32/gdi32.spec; then
python3 - <<'PYEOF'
p='win32ss/gdi/gdi32/gdi32.spec'
s=open(p).read()
anchor='@ stdcall -version=0x600+ D3DKMTWaitForVerticalBlankEvent(ptr) NtGdiDdDDIWaitForVerticalBlankEvent'
add='''

; Win7 WDDM 1.1 additions
@ stdcall -version=0x601+ D3DKMTAcquireKeyedMutex(ptr) NtGdiDdDDIAcquireKeyedMutex
@ stdcall -version=0x601+ D3DKMTCheckSharedResourceAccess(ptr) NtGdiDdDDICheckSharedResourceAccess
@ stdcall -version=0x601+ D3DKMTCheckVidPnExclusiveOwnership(ptr) NtGdiDdDDICheckVidPnExclusiveOwnership
@ stdcall -version=0x601+ D3DKMTConfigureSharedResource(ptr) NtGdiDdDDIConfigureSharedResource
@ stdcall -version=0x601+ D3DKMTCreateAllocation2(ptr) NtGdiDdDDICreateAllocation2
@ stdcall -version=0x601+ D3DKMTCreateKeyedMutex(ptr) NtGdiDdDDICreateKeyedMutex
@ stdcall -version=0x601+ D3DKMTCreateSynchronizationObject2(ptr) NtGdiDdDDICreateSynchronizationObject2
@ stdcall -version=0x601+ D3DKMTDestroyKeyedMutex(ptr) NtGdiDdDDIDestroyKeyedMutex
@ stdcall -version=0x601+ D3DKMTGetOverlayState(ptr) NtGdiDdDDIGetOverlayState
@ stdcall -version=0x601+ D3DKMTGetPresentQueueEvent(long ptr) NtGdiDdDDIGetPresentQueueEvent
@ stdcall -version=0x601+ D3DKMTOpenKeyedMutex(ptr) NtGdiDdDDIOpenKeyedMutex
@ stdcall -version=0x601+ D3DKMTOpenResource2(ptr) NtGdiDdDDIOpenResource2
@ stdcall -version=0x601+ D3DKMTOpenSynchronizationObject(ptr) NtGdiDdDDIOpenSynchronizationObject
@ stdcall -version=0x601+ D3DKMTReleaseKeyedMutex(ptr) NtGdiDdDDIReleaseKeyedMutex
@ stdcall -version=0x601+ D3DKMTSignalSynchronizationObject2(ptr) NtGdiDdDDISignalSynchronizationObject2
@ stdcall -version=0x601+ D3DKMTWaitForSynchronizationObject2(ptr) NtGdiDdDDIWaitForSynchronizationObject2
; WDDM 1.2 additions (Win8+)
@ stdcall -version=0x602+ D3DKMTEnumAdapters(ptr) NtGdiDdDDIEnumAdapters
@ stdcall -version=0x602+ D3DKMTOpenAdapterFromLuid(ptr) NtGdiDdDDIOpenAdapterFromLuid
@ stdcall -version=0x602+ D3DKMTOfferAllocations(ptr) NtGdiDdDDIOfferAllocations
@ stdcall -version=0x602+ D3DKMTReclaimAllocations(ptr) NtGdiDdDDIReclaimAllocations
@ stdcall -version=0x602+ D3DKMTSetVidPnSourceOwner1(ptr) NtGdiDdDDISetVidPnSourceOwner1
@ stdcall -version=0x602+ D3DKMTWaitForVerticalBlankEvent2(ptr) NtGdiDdDDIWaitForVerticalBlankEvent2'''
assert anchor in s, "gdi32.spec anchor missing"
s=s.replace(anchor, anchor+add, 1)
open(p,'w').write(s)
print("gdi32.spec: added WDDM 1.1/1.2 exports")
PYEOF
fi
cmp -s "$D/sdk/include/ddk/d3dkmthk.h"   sdk/include/ddk/d3dkmthk.h   || cp "$D/sdk/include/ddk/d3dkmthk.h"   sdk/include/ddk/d3dkmthk.h

# dwm.c is untracked (persists) but ensure it exists/matches donor.
cmp -s "$D/win32ss/user/ntuser/dwm.c"    win32ss/user/ntuser/dwm.c    || cp "$D/win32ss/user/ntuser/dwm.c"    win32ss/user/ntuser/dwm.c

# ---- 4. ntuser.h: 9 DWM/ghost/session NtUser* prototypes (surgical insert) ----
if ! grep -q "NtUserDwmStartRedirection" win32ss/include/ntuser.h; then
python3 - <<'PYEOF'
p='win32ss/include/ntuser.h'
s=open(p).read()
anchor='ULONG\nRtlGetExpWinVer(_In_ PVOID BaseAddress);'
block='''/* DWM (Desktop Window Manager) / session / ghost-window support syscalls (Vista+) */
BOOL
NTAPI
NtUserDwmGetDxRgn(
    _In_ HWND hwnd,
    _In_ HANDLE hrgn,
    _In_ DWORD dwFlags);

BOOL
NTAPI
NtUserDwmHintDxUpdate(
    _In_ HWND hwnd,
    _In_ DWORD dwFlags);

BOOL
NTAPI
NtUserDwmStartRedirection(
    _Out_ PVOID pRedirectionInfo);

BOOL
NTAPI
NtUserDwmStopRedirection(VOID);

BOOL
NTAPI
NtUserRegisterSessionPort(
    _In_ HANDLE hPort);

BOOL
NTAPI
NtUserUnregisterSessionPort(VOID);

HWND
NTAPI
NtUserGhostWindowFromHungWindow(
    _In_ HWND hwndHung);

HWND
NTAPI
NtUserHungWindowFromGhostWindow(
    _In_ HWND hwndGhost);

BOOL
NTAPI
NtUserRegisterErrorReportingDialog(
    _In_ HWND hwndDialog,
    _In_ DWORD dwFlags);

'''
assert anchor in s, "ntuser.h anchor missing"
s=s.replace(anchor, block+anchor, 1)
open(p,'w').write(s)
print("ntuser.h: inserted DWM prototypes")
PYEOF
fi

# ---- 5. livecd.inf: autostart dwm.exe via explorer Run key ----
if ! grep -q "DwmCompositor" boot/bootdata/livecd.inf; then
python3 - <<'PYEOF'
p='boot/bootdata/livecd.inf'
s=open(p).read()
anchor='HKCU,"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run","Arm64MmSelfMapKmtest",0x00020000,"%SystemRoot%\\system32\\cmd.exe /c ""%SystemDrive%\\Profiles\\Default User\\My Documents\\livecd_start.cmd"""'
add='''

; WDDM/DWM bring-up: Explorer launches dwm.exe after the desktop is up.
; -standalone skips the 30s SCM service-dispatcher connect timeout so the
; compositor starts immediately.
HKLM,"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run","DwmCompositor",0x00020000,"%SystemRoot%\\system32\\dwm.exe -standalone"
; Enable the dwm wine-debug channel so its composition TRACEs reach the serial.
HKLM,"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment","DEBUGCHANNEL",0x00020000,"+dwm"'''
assert anchor in s, "livecd.inf anchor missing"
s=s.replace(anchor, anchor+add, 1)
open(p,'w').write(s)
print("livecd.inf: dwm autostart Run key added")
PYEOF
fi

# ---- 5b. preinstall.inf: autostart dwm.exe on the Pi5/preinstall image ----
# The Pi5 ReactOS.img uses preinstall.inf (NOT livecd.inf) for its SOFTWARE
# hive's Run keys.  Add the same dwm.exe Explorer Run key here so dwm.exe also
# autostarts on the real-hardware image.  ADDITIVE; standalone dwm exits
# gracefully when no WDDM adapter is present, so this is safe on any machine.
if ! grep -q "DwmCompositor" boot/bootdata/preinstall.inf; then
python3 - <<'PYEOF'
p='boot/bootdata/preinstall.inf'
s=open(p).read()
anchor='HKLM,"SYSTEM\\CurrentControlSet\\Services\\Cdrom","Start",0x00010001,0x00000000'
add='''

; WDDM/DWM bring-up (Pi5/preinstall): Explorer launches dwm.exe after the
; desktop is up.  dwm.exe opens the first started WDDM adapter (rpi5dod when
; opted in) via D3DKMTOpenAdapterFromGdiDisplayName, composes complete frames
; and presents them through D3DKMTPresent -> dxgkrnl -> rpi5dod (tear-free).
; -standalone skips the 30s SCM service-dispatcher connect timeout.
HKLM,"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run","DwmCompositor",0x00020000,"%SystemRoot%\\system32\\dwm.exe -standalone"
; Surface dwm's composition TRACEs on the serial.
HKLM,"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment","DEBUGCHANNEL",0x00020000,"+dwm"'''
assert anchor in s, "preinstall.inf anchor missing"
s=s.replace(anchor, anchor+add, 1)
open(p,'w').write(s)
print("preinstall.inf: dwm autostart Run key added")
PYEOF
fi

# ---- 6. rpi5vc4 ownership-yield: decline when rpi5dod owns the display -----
# rpi5vc4.c is TRACKED (reverter-sensitive), so re-apply the DriverEntry yield
# (Rpi5Vc4DodOwnsDisplay + the decline check) every build.  Harmless when the
# DisableForDod flag is absent (rpi5vc4 stays the primary fallback).
if ! grep -q "Rpi5Vc4DodOwnsDisplay" win32ss/drivers/miniport/rpi5vc4/rpi5vc4.c; then
python3 - <<'PYEOF'
p='win32ss/drivers/miniport/rpi5vc4/rpi5vc4.c'
s=open(p).read()

# (a) the helper, inserted right after Rpi5Vc4IsRpi5Platform()
plat_anchor='''    return HalGetCachedAcpiTable(RPI5VC4_ACPI_FADT, "RPIFDN", "RPI5") != NULL;
}'''
helper='''    return HalGetCachedAcpiTable(RPI5VC4_ACPI_FADT, "RPIFDN", "RPI5") != NULL;
}

/*
 * Returns TRUE if the rpi5dod WDDM display-only miniport is configured to own
 * the display (registry flag HKLM\\System\\CurrentControlSet\\Services\\rpi5vc4\\
 * DisableForDod == 1, seeded by rpi5dod_root.inf when rpi5dod is opted in).
 * rpi5vc4 and rpi5dod both drive the Pi 5 HVS scanout, so only one may own
 * \\Device\\Video0; when rpi5dod is primary, rpi5vc4 declines to avoid a
 * collision (rpi5dod is the default fallback otherwise).
 */
static BOOLEAN
Rpi5Vc4DodOwnsDisplay(VOID)
{
    UNICODE_STRING KeyName =
        RTL_CONSTANT_STRING(L"\\\\Registry\\\\Machine\\\\SYSTEM\\\\CurrentControlSet\\\\Services\\\\rpi5vc4");
    UNICODE_STRING ValueName = RTL_CONSTANT_STRING(L"DisableForDod");
    OBJECT_ATTRIBUTES ObjectAttributes;
    HANDLE KeyHandle;
    UCHAR Buffer[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + sizeof(ULONG)];
    PKEY_VALUE_PARTIAL_INFORMATION Info = (PKEY_VALUE_PARTIAL_INFORMATION)Buffer;
    ULONG ResultLength = 0;
    NTSTATUS Status;
    BOOLEAN Result = FALSE;

    InitializeObjectAttributes(&ObjectAttributes,
                               &KeyName,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);

    Status = ZwOpenKey(&KeyHandle, KEY_QUERY_VALUE, &ObjectAttributes);
    if (!NT_SUCCESS(Status))
        return FALSE;

    Status = ZwQueryValueKey(KeyHandle,
                             &ValueName,
                             KeyValuePartialInformation,
                             Info,
                             sizeof(Buffer),
                             &ResultLength);
    if (NT_SUCCESS(Status) &&
        Info->Type == REG_DWORD &&
        Info->DataLength == sizeof(ULONG) &&
        (*(PULONG)Info->Data) != 0)
    {
        Result = TRUE;
    }

    ZwClose(KeyHandle);
    return Result;
}'''
assert plat_anchor in s, "rpi5vc4 platform anchor missing"
s=s.replace(plat_anchor, helper, 1)

# (b) the decline check in DriverEntry
de_anchor='''        return STATUS_SUCCESS;
    }

    VideoPortZeroMemory(&InitData, sizeof(InitData));'''
de_repl='''        return STATUS_SUCCESS;
    }

    if (Rpi5Vc4DodOwnsDisplay())
    {
        /*
         * The rpi5dod WDDM display-only miniport is configured as the primary
         * display.  Decline so rpi5vc4 does not collide on \\Device\\Video0.
         */
        DbgPrint("RPI5VC4: rpi5dod owns the display (DisableForDod) - declining\\n");
        return STATUS_SUCCESS;
    }

    VideoPortZeroMemory(&InitData, sizeof(InitData));'''
assert de_anchor in s, "rpi5vc4 DriverEntry anchor missing"
s=s.replace(de_anchor, de_repl, 1)

open(p,'w').write(s)
print("rpi5vc4.c: ownership-yield (DisableForDod) applied")
PYEOF
fi

echo "reapply_wddm: OK  (svc64 GdiDdDDI=$(grep -c GdiDdDDI win32ss/w32ksvc64.h), d3dkmt.c=$(wc -l < win32ss/gdi/ntgdi/d3dkmt.c) lines, link=$(grep -c 'libcntpr wddm_bridge' win32ss/CMakeLists.txt), cddb=$(grep -c wddm_cddb_override CMakeLists.txt), dwmrun=$(grep -c DwmCompositor boot/bootdata/livecd.inf), preinst_dwm=$(grep -c DwmCompositor boot/bootdata/preinstall.inf), vc4yield=$(grep -c Rpi5Vc4DodOwnsDisplay win32ss/drivers/miniport/rpi5vc4/rpi5vc4.c))"
