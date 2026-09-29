# FreeLdr artwork and assets

The interactive boot menu uses a solid dark gray background (`#202020`) and embeds these assets in the UEFI executable; no external image file is needed at boot.

The initial countdown uses a black screen with the firmware's BGRT logo. A key, volume button, click or tap opens the menu; the opening input is consumed. Pointer movement alone does not open it. When the countdown expires, the configured default entry starts automatically; the supplied media configurations default to the normal LiberNT entry. Selecting an entry from the menu also restores the black BGRT screen with the graphical loading bar. Error dialogs temporarily hide BGRT so it cannot cover their text.

- `logo.rgba` is the existing [LiberNT mark](../../../../../media/graphics/branding/libernt-mark.svg), rasterized by AppKit at 48 x 42 with its aspect ratio preserved. It contains premultiplied RGBA pixels. The UI renders it beside the name LiberNT in the header and beside entries whose section contains `BootLogo=LiberNT`. Logo selection uses entry metadata, so other configured operating systems remain independent. Automatic OS detection is not implemented.
- `icons.alpha` contains the 24 x 24 coverage masks of Desktop, Chevron Right, Arrow Left and Cursor, in that order. Desktop is retained in the asset bundle but is not drawn in menu rows. These are unmodified regular icons from [Microsoft Fluent UI System Icons](https://github.com/microsoft/fluentui-system-icons), under the MIT license in `LICENSE`. They are rasterized at their native size by AppKit with no translation, composition or gamma adjustment. The original SVGs are retained here. The UI applies its foreground color when drawing these monochrome masks.
- The UI font is the existing `media/fonts/selawk.ttf`, embedded by `native-bin2c`.

Startup choices follow the supported subset of Microsoft's [Windows startup settings](https://support.microsoft.com/en-us/windows/experience/startup-boot/windows-startup-settings). The existing loader option parser supplies the SAFEBOOT, BOOTLOG, DEBUG and BASEVIDEO strings. No Windows structures are introduced. Basic video names the existing BASEVIDEO behavior; it does not claim a new display implementation. Firmware settings appear only when the existing firmware capability check succeeds. Unsupported recovery features and the old HAL-selection/custom-boot wizard are absent from the menu tree. Startup overrides are kept per boot entry for the current session.

The supplied media configurations contain one LiberNT entry. Startup options offers direct boot actions: normal, serial debugging, file logging, screen debugging, one processor, and processor diagnostics. The single-processor action supplies `NUMPROC=1` to the existing architecture-neutral consumer in `ntoskrnl/ex/init.c`; it does not substitute a different kernel or HAL. Recovery options contains safe-mode selection, basic display, and driver-load recording. Reset appears only when session overrides exist. Settings contains firmware setup, when supported, and restart. The command editor is available through F10, outside the visible menu tree; the diagnostic-channel editor is removed.

## Input

Mouse motion highlights a row; releasing a click on the pressed row chooses it. Touch taps choose rows, and vertical drags move through longer menus without activating a row on release. Mouse wheel input moves the selection, right-click returns, and Back rows are touch targets. Dialogs have clickable Continue/Back or Save/Cancel buttons. Editing command text still requires a keyboard.

Either volume button opens the initial menu in Tablet mode. Subsequent volume presses move the selection and restart a full five-second timer. The top-left label identifies Tablet mode; a bottom-center countdown names the selected boot entry or action. Expiration chooses that item. New submenus wait for another volume press before arming a countdown, so recovery toggles cannot repeat automatically. Keyboard input leaves Tablet mode. Pointer input cancels an armed countdown and allows immediate selection by tap/click.

The UEFI input implementation uses Simple Text Input Ex when available and falls back to Simple Text Input. Volume scan codes come from the existing EDK II `SimpleTextInEx.h`. Pointer protocol GUIDs, declarations and semantics come from [UEFI console protocols](https://uefi.org/specs/UEFI/2.10/12_Protocols_Console_Support.html) and the authoritative EDK II [SimplePointer.h](https://github.com/tianocore/edk2/blob/master/MdePkg/Include/Protocol/SimplePointer.h) and [AbsolutePointer.h](https://github.com/tianocore/edk2/blob/master/MdePkg/Include/Protocol/AbsolutePointer.h). Timers use UEFI Boot Services timer events. Platform hooks have inert fallbacks on platforms without these interfaces.

Firmware must expose the input device through these protocols. This does not add USB, I2C or GPIO drivers to FreeLdr. Physical touch and volume-button delivery therefore need validation on the target device.

## Validation

`uefildr` builds on ARM64; `uefildr`, `freeldr` and `rosload` build on amd64 and i386 with `CCACHE_DISABLE=1`. ARM64 VM checks exercise the menu tree, touch taps, relative mouse clicks, both volume directions, countdown resets, volume entry during the initial timeout, and the BGRT loading transition. The central firmware-logo pixels match before menu entry and after the tablet countdown starts loading. The single-processor action is verified to supply `NUMPROC=1` before firmware handoff. Input checks inject UEFI key/pointer responses through the debugger: the current ARM64 test firmware omits USB pointer drivers, and its USB keyboard driver does not deliver volume scan codes. These checks validate the loader response to firmware input; they do not establish physical device support.

## Unused wallpaper artwork

`wallpaper.png` and `wallpaper.rgbz` retain the original generated artwork. They are not embedded or displayed by the boot UI.

### Generation prompt

Built-in imagegen mode:

> Use case: stylized-concept. Asset type: widescreen wallpaper artwork embedded in the LiberNT operating-system boot manager. Create a refined abstract digital artwork using only near-black, midnight navy and deep dark blue, with restrained sapphire edge light. Broad layered flowing glass/satin ribbons sweep diagonally from lower left toward upper right, with subtle depth and soft gradients. Extremely clean contemporary operating-system aesthetic. The central 50 percent must remain very dark, calm and low-detail so a vertically centered white-text boot menu is legible; put most sculptural detail toward the outer left and right edges. Upper center also needs quiet dark negative space for a separately rendered logo. Landscape 16:10 composition, ideally 1920x1200. No text, no logos, no interface, no icons, no laptop frame, no green, no teal, no purple, no bright white areas, no watermark. This is the wallpaper itself, edge to edge.
