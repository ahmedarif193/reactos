<!--
SPDX-License-Identifier: GPL-3.0-or-later
Copyright 2026 Ahmed Arif
-->

# Windows 7 Start-menu context and taskbar-pinning checklist

This checklist separates shell-owned behavior from verbs supplied by installed
context-menu extensions.  It is based on a Windows 7 reference run and the
public shell/taskbar contracts linked under **References**.

Checked items below describe implemented source behavior. Runtime testing is
stopped at the user's request. The latest launch, icon, taskbar-menu, click, and
pin-folder fixes have build verification only; the earlier native and ReactOS runs did not establish a
passing end-to-end parity result.

## Recent applications

- [x] Order applications by their persisted last-use timestamps instead of
  filling the list with fixed application names.
- [x] Record Start-menu launches, new task windows, and application activation.
- [x] Include used executables outside the Start-menu folders, preferring an
  installed shortcut when one resolves to the same executable.
- [x] Remove all recent aliases of an application without deleting its files.
- [x] Remove Help and Support; show disabled Lock and Log off entries as `(soon)`.

## Context-menu surface

| Feature | Desktop application shortcut | Start-menu application row | Owner | ReactOS target |
|---|---:|---:|---|---|
| Open | Yes | Yes | Shell item | Use the item's `IContextMenu` |
| Open file location | Yes | Yes | Shell item/host | Preserve the item's physical shortcut location |
| Run as administrator | Yes | Yes | Shell item | Keep the `runas` verb when the item supports it |
| Run as different user | Shift menu | Shift menu | Shell item | Request `CMF_EXTENDEDVERBS` while Shift is down |
| Pin to taskbar | When unpinned | When unpinned | `CLSID_StartMenuPin` | Add one persistent taskbar shortcut |
| Unpin from taskbar | When pinned | When pinned | `CLSID_StartMenuPin` | Remove the persistent shortcut without closing a running app |
| Copy as path | Shift menu | Shift menu | Shell extension | Route `IContextMenu2/3` menu messages |
| Send to | Yes | Yes | Shell extension | Route dynamic submenu messages |
| Cut | Yes | No for recent virtual rows | Shell item/host | Suppress for virtual recent rows |
| Copy | Yes | Yes | Shell item | Preserve |
| Create shortcut | Yes | No for recent virtual rows | Shell item/host | Suppress for virtual recent rows |
| Delete | Yes | No for recent virtual rows | Shell item/host | Suppress for virtual recent rows |
| Remove from this list | No | Recent rows only | Start-menu host | Remove only the recent-list record |
| Rename | Yes | Shortcut rows | Shell item/host | Rename through the parent folder and preserve recency |
| Properties | Yes | Yes | Shell item | Preserve |
| Context-menu keyboard key / Shift+F10 | Yes | Yes | Host | Open at the selected row |

Entries such as 7-Zip, ImDisk, and ownership tools in the reference image are
installed shell extensions.  ReactOS should expose them through the normal
`IContextMenu` aggregation path rather than hard-code them into Explorer.

## Persistent taskbar behavior

- [x] Store pins as shortcuts under
  `%APPDATA%\Microsoft\Internet Explorer\Quick Launch\User Pinned\TaskBar`.
- [x] Resolve and create `FOLDERID_UserPinned`, including an AppData redirection,
  so saving and reloading pins works on a profile without an existing pin folder.
- [x] Keep a pinned button visible when the application has no window.
- [x] Read the executable, arguments, working directory, show state, and Run As
  flag from the saved shortcut. Start that executable directly when a closed
  pinned button is clicked, without opening the `.lnk` through file associations.
- [x] Refresh existing windows before launching and retain the launched process
  handle until its window appears or it exits, suppressing repeated ordinary
  clicks during startup. Shift-click, middle-click, and the application's menu
  entry explicitly request a new instance.
- [x] Merge a newly created application window into its matching pinned button.
- [x] Preserve the displayed icon when pinning a running task. Save an owned icon
  beside the shortcut and keep using it after the last window closes and after
  Explorer restarts. Remove that owned icon when the pin is removed.
- [x] Activate/minimize one window and cycle through multiple windows on click.
  Show previews on hover and cancel pending previews when a button is clicked.
  Keep the grouped running-state indicators on the same button.
- [x] Keep the button after the final window closes when it is pinned.
- [x] Remove the button after the final window closes when it is not pinned.
- [x] Offer **Pin this program to taskbar** for a running unpinned application.
- [x] Offer **Unpin this program from taskbar** for a pinned application.
- [x] Refresh saved pins before opening a taskbar icon's menu and apply successful
  pin/unpin actions to that application's group immediately.
- [x] Read back a newly saved shortcut before reporting success. Report taskbar
  pin/unpin failures and preserve known pins when a shortcut cannot be read.
- [x] Unpinning a running application keeps its running task button.
- [x] Prevent duplicate pins that resolve to the same executable, including
  overlapping shell context handlers and taskbar actions.
- [x] Normalize short and long executable paths consistently for taskbar groups
  and desktop/Start-menu pins.
- [x] Preserve pin creation order across Explorer restart and logon.
- [x] Refresh a running taskbar when a desktop or Start-menu context handler
  changes the pin folder.
- [x] Respect the executable `NoStartPage` exclusion and `NoPinningToTaskbar`.
- [x] Keep numeric and canonical pin/unpin actions stable after other menus
  change the stored pin; support ANSI and Unicode command invocation.

## ABI and compatibility surface

- [x] Declare `IStartMenuPinnedList` with IID
  `{4CD19ADA-25A5-4A32-B3B7-347BEE5BE36B}` and its single public
  `RemoveFromList(IShellItem *)` method.
- [x] Register `CLSID_StartMenuPin`
  `{A2A9545D-A0C2-42B4-9708-A0B2BADD77C8}` in `shell32.dll`.
- [x] Expose `IUnknown`, `IStartMenuPinnedList`, `IShellExtInit`,
  `IContextMenu`, and `IObjectWithSite` from that class.
- [x] Return `S_OK` from `RemoveFromList` when the item was not pinned, as the
  public contract requires.
- [x] Keep undocumented `IPinnedList` out of the claimed ABI until its method
  layout is established independently; matching only its IID would create an
unsafe vtable contract.

This implementation groups ordinary Win32 applications by executable path. It
does not implement AppUserModelID or packaged-app grouping, application-supplied
Jump List destinations, drag reordering, or shortcut/window `PreventPinning`
property-store support. `runas` uses the existing ReactOS Run As backend; Windows
7 UAC and a separate `runasuser` implementation are outside this change. Extended
verbs supplied by installed shell extensions are requested and dispatched.

## Verification matrix

The same focused probe should be run on Windows 7, a current Windows release,
and ReactOS.  It must record the `CLSID_StartMenuPin` QI matrix, canonical menu
verbs, pin/unpin results, pin-folder contents, closed-button persistence,
running-window merge behavior, and Explorer-restart persistence.  Source and
build success do not count as runtime parity.

The native Windows 11 UI run exercised three and five pins, mixed unpin/repin
operations, Explorer restart, reboot, launching closed pins, and unpinning a
running group without closing its windows. Its final cleanup failed, so it is
not a passing suite. Direct external invocation of the native pin verb also
differs from invocation through Explorer's visible menu.

The ReactOS runs confirmed MenuBand registration but failed pin creation because
`FOLDERID_UserPinned` was marked disallowed. The folder implementation and
callers are corrected in source. Both Explorer targets and native, WoW64, and
ARM64EC shell32 build successfully. Runtime checks of those fixes and the latest
launch, icon, and click behavior were not run after the user stopped testing.

The permanent regression probe is
`modules/rostests/apitests/shell32/StartMenuPin.c`, available as
`shell32_apitest StartMenuPin` when tests are enabled. It uses a private copied
executable and verifies filesystem-handler registration, numeric and canonical
verbs, stale-menu idempotency, shortcut metadata, and public removal semantics.
It also checks MenuBand activation, because a malformed earlier registration
resource can prevent later shell classes from registering. The standalone build
uses `PIN_TEST_STANDALONE`; `PIN_TEST_NO_CRT` supplies an optional direct entry
point for native probes without a CRT startup dependency.

Local Windows 7 reference captures are kept in the ignored build tree as
`win7_desktop_app_context_reference.png` and
`win7_start_app_context_reference.png`.  The prepared probe is
`native-pin-probe.c`. The newer assert-based binaries are
`start-menu-pin-probe-arm64.exe` and `start-menu-pin-probe-amd64.exe`; they must
not be run while the active ReactOS test session is in use.

## Registration recovery

The new `startmenupin.rgs` resource must begin with `HKCR`. ReactOS's ATL registry
script parser does not accept C comments; putting a license header there aborted
shell32 registration before MenuBand and produced `REGDB_E_CLASSNOTREG`
(`80040154`). Copyright and GPL metadata are preserved in
`startmenupin.rgs.license`. The compiled resource is checked in each shell32
architecture.

A fresh preinstalled image registers shell32 during mini setup. An installation
that already encountered the malformed resource needs registration rerun after
installing the corrected DLL, for example
`regsvr32 /s %SystemRoot%\System32\shell32.dll`. Copying a DLL alone does not
recreate the missing registry keys. No registration command or guest restart was
performed on the user's running test session.

## References

- Microsoft, [Windows desktop environment](https://learn.microsoft.com/en-us/windows/win32/uxguide/winenv-desktop)
- Microsoft, [Windows 7 desktop experience](https://learn.microsoft.com/en-us/windows/win32/win7devguide/the-desktop-experience)
- Microsoft, [Windows 7 taskbar APIs](https://learn.microsoft.com/en-us/archive/msdn-magazine/2009/brownfield/windows-7-taskbar-apis)
- Microsoft, [`IStartMenuPinnedList`](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl/nn-shobjidl-istartmenupinnedlist)
- Microsoft, [Exclude items from taskbar pinning and recent/frequent lists](https://learn.microsoft.com/en-us/windows/win32/shell/how-to-exclude-items-from-taskbar-pinning-and-recent-frequent-lists)
- Microsoft, [Customize the taskbar](https://support.microsoft.com/en-us/windows/customize-the-taskbar-in-windows)
