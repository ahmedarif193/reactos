<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Ahmed ARIF <arif193@gmail.com>
-->

# DWM Settings

`dwmsettings.exe` is installed in System32 and appears on the desktop and in
Accessories / System Tools on LiveCD, preinstalled, and setup-created images.
Every checkbox saves on `BN_CLICKED`. The panel uses native Win32 controls.

| Control | Persistence | Rendering |
| --- | --- | --- |
| Window animations | `SPI_SETANIMATION`, `HKCU\Control Panel\Desktop\WindowMetrics\MinAnimate`, `REG_SZ` `0` / `1` | win32k starts or cancels animation geometry; DWM renders the transformed GPU textures |
| Window shadows | `HKCU\Software\ReactOS\DWM\EnableShadows`, `REG_DWORD` `0` / `1` | Enables the existing GPU window shadow |
| Enable glass look | Same key, `EnableAcrylic`, `REG_DWORD` | Enables the transient glass materials of the Windows 8 visual style; when off, every material and blur is opaque/classic |
| Blur window titles | Same key, `EnableBlur`, `REG_DWORD` | Available with the glass look; blurs title bars and other non-client glass, the taskbar, menus, flyouts, and application blur-behind; off keeps them sharp, tinted glass |
| Blur window content | Same key, `EnableContentBlur`, `REG_DWORD` | Available with the glass look, independent of title blur; when off, participating application content is opaque (classic) while its frame keeps the glass look |
| Color scheme | Same key, `ColorScheme`, `REG_DWORD` `0` Light / `1` Dark | Selects the Light or Dark palette of participating applications; absent or invalid values select Dark |

The four ReactOS effect preferences default to enabled when absent. They control GPU
composition; they do not enable software effects. Window alpha and color-key
semantics remain application-owned. All changes apply to open windows. Mica and
Mica Alt are not exposed because their wallpaper material is not implemented by
the active GPU renderer. There is no rounded-corner switch; corner radii remain
per-window.

The glass look, blur, and color scheme require a visual style that provides
composited `ContentLight::Liquid` and `ContentDark::Liquid` materials. The
Windows 8 visual style defines them with the Windows 11 palette bases (Light
243, 243, 243; Dark 32, 32, 32). With another visual style the panel disables
these controls and asks to select the Windows 8 visual style first.

Explorer cabinet views, Task Manager 11, and this panel opt into this common
policy; Task Manager 11 has no separate theme choice. They paint their background
with the selected scheme material and mark it as application content. DWM keeps
that content blurred glass only while the glass look and content blur are
enabled; otherwise it limits the material to the non-client frame, so content is
opaque. This is an interim glass look, not the Windows DWM material pipeline.
Explorer's desktop, high-contrast, common-dialog, and custom-bitmap views do not
opt in.

The console also opts into content glass, but intentionally derives its material
key from the active console background palette instead of `ColorScheme`. This
keeps command-line foreground/background color contracts intact. Start, menu,
taskbar, and flyout materials are not marked as application content and are
therefore unaffected by the content-only switch.

The Windows-compatible animation API now loads and saves `MinAnimate` in
win32k. Disabling it also cancels an animation already in progress. New and
active animation changes take effect without restarting DWM.

After a successful save the panel sends `WM_SETTINGCHANGE` directly to the
message-only `ReactOS.Dwm.Settings` window on the compositor thread. No registry
handle is held during this call. That thread reloads and validates the values,
marks the entire scene dirty, and replies with the active settings and GPU
availability. A color scheme change is also broadcast synchronously as
`WM_SETTINGCHANGE` with `lParam` `Software\ReactOS\DWM`, so participating
applications repaint with the new material; win32k refuses posted
`WM_SETTINGCHANGE` messages. The next frame rebuilds cached backdrops. The reply confirms
reload, not completion of GPU presentation. If DWM is absent, unresponsive, or
using a renderer without GPU composition, the panel reports that live effects
were not confirmed and that DWM may need to be restarted. The scalar reply is
a private ReactOS protocol, not a Windows DWM export or ABI.

The native API boundaries were checked against Microsoft documentation:

- [SystemParametersInfoW](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-systemparametersinfow)
  documents `SPI_GETANIMATION`, `SPI_SETANIMATION`, `ANIMATIONINFO`, and
  `SPIF_UPDATEINIFILE`. Its `SPI_SETDROPSHADOW` option requires `CS_DROPSHADOW`;
  this panel does not treat it as a global DWM shadow policy.
- [Rounded corners](https://learn.microsoft.com/en-us/windows/apps/desktop/modernize/ui/apply-rounded-corners)
  and [DWM window attributes](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/ne-dwmapi-dwmwindowattribute)
  define per-window preferences. The global shadow, blur, and material
  switches above are explicitly ReactOS preferences.
- [DWM policies](https://learn.microsoft.com/en-us/windows/client-management/mdm/policy-csp-admx-dwm)
  documents `DisallowAnimations` as a policy that requires logoff on Windows.
  The panel edits the interactive animation preference instead of that policy.

Reference binaries available locally were Windows 11 ARM64 10.0.26100.1742.
Their strings confirm the Microsoft DWM preference and policy paths, but do
not establish a public global effect-switch registry ABI. No Windows VM or
physical device was used for this implementation's validation.
