# RPi5VC4 Cursor/Framebuffer Glitch Brief

Working tree: `/home/ahmed/WorkDir/TTE/reactos_arm64/output-Clang-arm64-debug`
Branch: `rpi5/uptimate-bringup`

## Current Symptom

ReactOS reaches the desktop on Raspberry Pi 5 with `rpi5vc4.sys` + `framebuf.dll`, but there is still a visual corruption path around the mouse cursor:

- Moving/hovering the mouse over some window strips and colorful regions causes flicker/redraw-like artifacts.
- The artifact looks like content from the bottom/underlying area becomes visible higher up, or stale black/color patches are repaired when the cursor passes over them.
- The user observed that it happens especially when the cursor changes icon, not just during plain position movement.
- This is not an early boot failure. Treat `flash_test.py` "stall" after desktop as serial quiet unless the log contains `Fatal`, `Assertion`, `DABORT`, `Entered debugger`, or a KDB prompt.

## Current Relevant Code

Display miniport:

- `win32ss/drivers/miniport/rpi5vc4/rpi5vc4.c`
- `win32ss/drivers/miniport/rpi5vc4/rpi5vc4.h`
- `win32ss/drivers/miniport/rpi5vc4/rpi5vc4_hvs.c`
- `win32ss/drivers/miniport/rpi5vc4/rpi5vc4_hvs.h`
- `win32ss/drivers/miniport/rpi5vc4/rpi5vc4_crtc.c`
- `win32ss/drivers/miniport/rpi5vc4/rpi5vc4_crtc.h`

Framebuffer display driver:

- `win32ss/drivers/displays/framebuf/pointer.c`
- `win32ss/drivers/displays/framebuf/surface.c`
- `win32ss/drivers/displays/framebuf/screen.c`
- `win32ss/drivers/displays/framebuf/framebuf.h`

Mouse engine:

- `win32ss/gdi/eng/mouse.c`

## Fixes Already Present / Keep Unless Proven Wrong

- `framebuf.dll` queries hardware pointer caps and uses miniport pointer IOCTLs.
- `rpi5vc4` exposes a 64x64 color async hardware pointer.
- The cursor is composited by the HVS as a second plane.
- Cursor plane uses a separate SCALER6D UPM slot; without that, the cursor plane existed but did not contribute pixels.
- The scanout plane forces fixed opaque alpha so GDI's zero reserved byte does not make the desktop black.
- `framebuf.dll` has reserved-byte hooks for 32bpp surfaces and drains ARM64 stores after marking touched regions opaque.
- `IntEngSetPointerShape()` now clears `Pointer.Exclude` when the driver returns `SPS_ACCEPT_NOEXCLUDE`.
- `framebuf` hardware `DrvSetPointerShape()` / `DrvMovePointer()` also clear the returned exclusion rectangle on successful hardware pointer handling.

## Attempted and Reverted

The last attempted change was reverted because it did not appear to fix the live symptom:

- two cursor backing buffers,
- `CursorActiveBuffer`,
- alternating private HVS display-list heads,
- changing cursor move to rebuild/publish a full inactive list instead of updating the cursor plane in place.

Do not reintroduce that exact change as a blind fix. If revisiting it, first prove the HVS is actually fetching a partially rewritten cursor buffer or display list during icon changes.

## Current Suspicion

The strongest current signal is that the artifact appears when the cursor icon changes. That puts focus on the shape-change path:

1. `win32k` calls `DrvSetPointerShape`.
2. `framebuf!DrvSetPointerShape` builds `VIDEO_POINTER_ATTRIBUTES`.
3. `rpi5vc4!Rpi5Vc4SetPointerAttributes` zeroes and copies the cursor pixels, updates cursor dimensions/position/visibility, and calls `Rpi5HvsInstallScanout`.
4. HVS cursor plane fetches the cursor buffer and composites over scanout.

Possible roots to prove/disprove:

- GDI still believes there is a cursor exclusion/damage rectangle on some path despite `SPS_ACCEPT_NOEXCLUDE`, causing hide/show or redraw around the cursor.
- The cursor image alpha contract is wrong. `framebuf` may be sending straight ARGB while HVS plane is configured as premultiplied alpha. This should cause cursor halo/blending issues, but verify whether it can disturb surrounding pixels.
- `framebuf` reserved-byte opaque marking is incomplete for some drawing operations, so cursor movement merely flushes/repairs stale WC or alpha state. Check whether the affected strips/colored areas are painted by hooks not covered in `FRAMEBUF_RESERVED_SURFACE_HOOKS`, or by a path where `FrameBufferMarkOpaque()` gets an insufficient rect.
- Shape-change IOCTL may happen while win32k mouse safety state is active; verify `MouseSafetyOnDrawStart/End` does not hide/show a hardware pointer after the no-exclude path.
- HVS list/plane ordering and `NEXT`/`END` layout need another hardware check, but the cursor is visible and desktop is generally stable, so this is lower confidence than the shape/alpha/exclude path.

## Suggested Minimal Instrumentation

Keep instrumentation tiny and remove it before staging. Good counters/one-line traces:

- In `win32ss/gdi/eng/mouse.c`:
  - when `MouseSafetyOnDrawStart()` hides a hardware pointer,
  - when `MouseSafetyOnDrawEnd()` restores one,
  - current `Pointer.Exclude`.
- In `win32ss/drivers/displays/framebuf/pointer.c`:
  - `DrvSetPointerShape`: shape size, `fl`, `SPS_ALPHA`, whether hardware path accepted, returned exclude rect.
  - `DrvMovePointer`: whether hardware path accepted and returned exclude rect.
- In `win32ss/drivers/miniport/rpi5vc4/rpi5vc4.c`:
  - `Rpi5Vc4SetPointerAttributes`: width/height/enable/column/row, first few pixel alpha values, count of nonzero alpha pixels.
- In `win32ss/drivers/displays/framebuf/surface.c`:
  - optionally sample only the affected operation type and rect if a missing opaque-marking path is suspected.

Do not add broad boot `BOOTPROF` logs for this issue.

## Validation

Build:

```sh
ninja rpi5vc4 framebuf win32k
```

Hardware run:

```sh
../scripts/flash_test.py
```

Important: only run one `flash_test.py` at a time. Check first:

```sh
pgrep -af 'flash_test\.py'
```

Manual visual validation:

- Wait for desktop/setup shell.
- Move the cursor over the window strip and colorful/glitched regions.
- Specifically trigger cursor icon changes by hovering over window borders/title strips/control areas.
- Capture/inspect HDMI frames. The bug is visual; serial quiet after desktop is not sufficient evidence of failure.

Pass criteria for this bug:

- Cursor icon changes do not cause black/color strips, upward redraw artifacts, or repair-on-hover behavior.
- Moving the cursor over previously corrupt areas does not change underlying pixels except normal cursor compositing.
- Serial log has no `Fatal`, `Assertion`, `DABORT`, `Entered debugger`, or KDB prompt.

## Current Local State Notes

The last failed experiment has been manually reverted. The earlier smaller pointer-exclusion fix remains in the tree and should be treated as a candidate fix, not yet proven final.

