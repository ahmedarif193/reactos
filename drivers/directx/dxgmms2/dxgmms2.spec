; Windows 11 26100 exports DriverUnload at ordinal 1. Ordinals 2 and 3 are
; intentionally left empty until the private read-only VidMmInterface and
; VidSchInterface data-table contracts can be established independently.
1 stdcall DriverUnload(ptr)

; ReactOS-private registration bridge consumed by dxgkrnl.sys by name.
4 stdcall DxgkMms2Register(ptr ptr)
5 stdcall DxgkMms2Unregister(ptr)
