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
| Rounded window corners | Same key, `EnableRoundedCorners`, `REG_DWORD` | Enables the existing GPU corner mask |
| Application blur behind windows | Same key, `EnableBlur`, `REG_DWORD` | Enables explicit application blur-behind requests while retaining per-pixel alpha |
| Translucent window backgrounds | Same key, `EnableAcrylic`, `REG_DWORD` | Enables the existing transient backdrop material and its GPU blur |

The four ReactOS preferences default to enabled when absent. They control GPU
composition; they do not enable software effects. Window alpha and color-key
semantics remain application-owned. Explicit blur and translucent material
blur have independent switches. Mica and Mica Alt are not exposed because
their wallpaper material is not implemented by the active GPU renderer.

The Windows-compatible animation API now loads and saves `MinAnimate` in
win32k. Disabling it also cancels an animation already in progress. New and
active animation changes take effect without restarting DWM.

After a successful save the panel sends `WM_SETTINGCHANGE` directly to the
message-only `ReactOS.Dwm.Settings` window on the compositor thread. No registry
handle is held during this call. That thread reloads and validates the values,
marks the entire scene dirty, and replies with the active settings and GPU
availability. The next frame rebuilds cached backdrops. The reply confirms
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
  define per-window preferences. The global shadow, corner, blur, and material
  switches above are explicitly ReactOS preferences.
- [DWM policies](https://learn.microsoft.com/en-us/windows/client-management/mdm/policy-csp-admx-dwm)
  documents `DisallowAnimations` as a policy that requires logoff on Windows.
  The panel edits the interactive animation preference instead of that policy.

Reference binaries available locally were Windows 11 ARM64 10.0.26100.1742.
Their strings confirm the Microsoft DWM preference and policy paths, but do
not establish a public global effect-switch registry ABI. No Windows VM or
physical device was used for this implementation's validation.
