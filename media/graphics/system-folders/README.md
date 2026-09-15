# Shell icons

User-supplied shell artwork, with backgrounds already exported as transparent
RGBA PNGs. The generic folder comes from the replacement set in
`/Users/mac/Downloads/Colored-Folder-Icons`.

| Image | Source filename in Downloads | Resource |
| --- | --- | --- |
| Control Panel | ChatGPT Image Sep 13, 2026, 11_28_38 PM (2).png | shell32.dll, -137 |
| NT Object Namespace | ChatGPT Image Sep 13, 2026, 11_28_38 PM (3).png | ntobjshex.dll, first icon (ID 1) |
| System Registry | ChatGPT Image Sep 13, 2026, 11_28_38 PM (4).png | ntobjshex.dll, -7 |
| Generic folder | Colored-Folder-Icons/png/1024/folder-generic.png | shell32.dll, -4 and -5; explorer11.exe, 376 |
| Fixed drive | codex-clipboard-ASjWpd.png | shell32.dll, -9; explorer11.exe, 377 |
| My PC | Modern-Desktop-Icons 2/png/512/my-pc.png | shell32.dll, -16; explorer11.exe, 378 |

The My PC source is maintained with the other desktop shortcut artwork in
`media/graphics/desktop-icons`.

The fixed-drive icon serves local fixed disks such as C: and D:, including
Explorer11 drive tiles and navigation items. Drive letters retain their existing
drive-type classification.

Fixed-drive background removal used the built-in image generation tool in two
steps because the source checkerboard was baked into the image:

> Replace the entire checkerboard cloth background with perfectly flat solid
> magenta #FF00FF. Keep only the silver hard drive with its cyan light, exactly
> as it is. No gray checkers anywhere. No cloth. No texture on the background.
> This is an icon on a solid bright magenta backdrop.

> Remove the magenta background to transparent alpha. Output a transparent PNG
> cutout of this exact silver hard drive icon. Preserve the drive unchanged and
> opaque. No background, no checkerboard, no shadow.

The generic folder PNG is the yellow plain folder from the replacement set. It
is used for normal and expanded folder states, generic folder tiles,
navigation items, search results, and the Explorer11 window icon. The Desktop
known-folder fallback uses the teal house resource (`35.ico`).

The registry icon resource also serves registry keys within the namespace.
Existing resource IDs and registrations are preserved.

Background-removal prompt (one call per icon, substituting its name):

> Use case: background-extraction. Edit target: the attached NAME blue folder
> icon. Remove ONLY the exterior magenta or black background and any fringe or
> stray exterior pixels, replacing it with actual transparent alpha. Preserve
> the exact folder silhouette, blue colors, interior symbol, white papers,
> shading, proportions, placement and generous square canvas margins of the
> original. No redesign, no new objects, no shadow outside the icon, no text,
> no checkerboard drawn into pixels. Output a single transparent PNG icon.

Rebuild using Python with Pillow and NumPy:

```sh
python media/graphics/system-folders/render-icons.py
```

The shared user-folder encoder writes 16, 20, 24, 32, 40, 48, 64, 96, 128,
and 256 pixel frames as 32-bit DIBs with alpha and legacy AND masks. Ordinary
ReactOS builds consume the ICO files directly.
