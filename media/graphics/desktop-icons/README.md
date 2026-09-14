# Desktop icons

The PNGs in this directory are the 512-pixel RGBA artwork selected from
`/Users/mac/Downloads/Modern-Desktop-Icons`. They are encoded into the existing
ReactOS ICO resource identities used by the LiveCD desktop shortcuts. The
Network Places PNG is the supplied `codex-clipboard-YwSDRf.png` artwork because
it replaces the pack's globe design.

| Artwork | ReactOS resource |
| --- | --- |
| My Documents | `shell32.dll,-235` |
| My PC | `shell32.dll,-16`; Explorer11 resource 378 |
| Network Places | `shell32.dll,-18`; Explorer11 resource 379 |
| Internet Browser | `shell32.dll,-512`; `iexplore.exe` |
| Recycle Bin | `shell32.dll,-32` and `-33` |
| Applications Manager | `rapps.exe` |
| Command Prompt | `cmd.exe` |
| Device Manager | `devmgmt.exe` |
| DWM Settings | `dwmsettings.exe` |
| Read Me | `shell32.dll,-152` |
| RosGet | `rosget.exe` |
| Task Manager | `taskmgr.exe` and `taskmgr11.exe` |

Network Places uses the supplied two-monitor artwork. Its original magenta
background was removed with the built-in image generation tool using this
prompt:

> Remove the solid bright magenta background completely and output a true
> transparent RGBA PNG. Keep ONLY the two silver desktop monitors with blue
> screens and their round stands, exactly as supplied. Preserve every monitor,
> stand, color, highlight, outline, overlap, position and proportion. Make the
> gaps and canvas outside the monitors transparent. No magenta pixels, no
> checkerboard, no added objects, no redesign, no text, no shadow outside the
> monitors. Clean antialiased silhouette.

The shared ICO encoder writes 16, 20, 24, 32, 40, 48, 64, 96, 128, and 256
pixel 32-bit DIB frames with alpha and legacy AND masks:

```sh
python media/graphics/desktop-icons/render-icons.py
```
