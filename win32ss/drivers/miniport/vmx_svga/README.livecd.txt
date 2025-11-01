VMX SVGA LiveCD Notes
=====================

When you rebuild the VMware/VirtualBox SVGA miniport and want to validate it on
ReactOS LiveCD images:

1. Copy `win32ss\drivers\miniport\vmx_svga\vmx_svga.sys` into the
   `reactos/system32/drivers` directory of the LiveCD tree before running the
   ISO packaging step.
2. Make sure the updated `vmx_svga.inf` is present under
   `reactos/inf` so that the service keeps `Start=0` in the `Video` load
   group.
3. Regenerate the ISO (for example `ninja livecd`) *after* both files are in
   place; otherwise the boot image will still contain the older miniport.
4. When testing multiple revisions, clean the VirtualBox snapshot cache so the
   new LiveCD is actually booted.

These steps keep the boot-time accelerated path active on LiveCDs while we work
through the legacy videoprt compatibility plan.

Validation checklist
--------------------

1. VBoxSVGA boot
   - Create a fresh VM that uses the `VBoxSVGA` controller, attach the rebuilt LiveCD,
     and enable serial debugging (`VBoxManage modifyvm <vm> --uart1 0x3f8 4`).
   - Boot to the first stage GUI, save the serial log, and confirm `vmx_svga` stays loaded
     (no 0x37 fallback) and reports `framebuffer-mapped` in the boot log.
2. VMSVGA boot
   - Switch the virtual GPU to `VMSVGA`, keep the same LiveCD, and repeat the boot/log
     capture. Verify the adapter binding and review the new `HardwareInformation.RunMode`
     registry value via `SYSTEM32\config\SYSTEM` if needed.
3. videoprt diagnostics
   - Reboot with `set _VIDEO_DEBUG=FFFF` in the bootloader options or an equivalent setting
     so videoprt emits its channel traces. Ensure the forced zero IRQ path is logged and
     that no thunk patching warnings appear.
4. Incremental stress
   - After confirming stable boots, change resolutions in the Display control panel and
     exercise window dragging or Win32k GDI demos. Capture traces of any glitches before
     re-enabling FIFO acceleration in source.
