# Explorer toolbar artwork

The eleven PNGs were supplied by the user on 2026-09-13. The magenta
backgrounds are removed using the same color key and edge unmatting as the
user folder icons. The original canvas, proportions, colors and opaque
interior pixels are preserved, including the yellow tile in image 10 and
the transparent opening in image 8's handle.

The default toolbar uses images 1 through 11 from left to right, matching
each image to its action:

| Supplied image | Artwork | Toolbar position |
| --- | --- | --- |
| 1 | Back arrow | Back, far left |
| 2 | Forward arrow | Forward |
| 3 | Up arrow | Up |
| 4 | Search | Search |
| 5 | Folders pane | Folders |
| 6 | Move folder | Move To |
| 7 | Copy folder | Copy To |
| 8 | Delete | Delete |
| 9 | Undo | Undo |
| 10 | Views | Views |
| 11 | Dropdown | Views dropdown, far right |

`sources.json` records original filenames, hashes and assignments. The
Windows ICO resources in `dll/win32/browseui/res/toolbar` contain 11, 16, 20,
24, 32, 40, 48 and 64 pixel frames with 32-bit alpha and legacy AND masks.
BrowseUI installs these into 32-bit toolbar image lists; the final dropdown
image is drawn from its icon resource. Normal and hot states use the same
supplied artwork.

With Pillow and NumPy installed, regenerate the ICOs from these transparent
PNGs using:

```sh
python media/graphics/explorer-toolbar/render-icons.py
```

To repeat background removal from the original files as well:

```sh
python media/graphics/explorer-toolbar/render-icons.py --originals /path/to/originals
```

The converter shares the alpha and ICO helpers in
`../user-folders/render-icons.py`. Normal ReactOS builds use the stored ICOs.
