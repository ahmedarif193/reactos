# User folder icons

The replacement set is `/Users/mac/Downloads/Colored-Folder-Icons`. It contains
transparent RGBA artwork in ten supplied sizes. The checked-in PNGs use its
1024px originals and the ICO encoder creates the Windows resource frames.

Generic folders are yellow, the Desktop/user folder is teal with a house,
Documents is slate blue-gray, Downloads is green, Music is coral and rose,
Pictures is cyan-blue, and Videos is violet. `sources.json` records the source
and output hashes.

| Folder | Package source | Shell32 resource |
| --- | --- | --- |
| Generic | `png/1024/folder-generic.png` | `4.ico`, `5.ico` |
| Desktop/user | `png/1024/folder-home.png` | `35.ico` |
| Documents | `png/1024/folder-documents.png` | `235.ico` |
| Downloads | `png/1024/folder-downloads.png` | `downloads.ico` (16722) |
| Music | `png/1024/folder-music.png` | `237.ico` |
| Pictures | `png/1024/folder-pictures.png` | `236.ico` |
| Videos | `png/1024/folder-videos.png` | `238.ico` |

The ICOs in `dll/win32/shell32/res/icons` contain 16, 20, 24, 32, 40, 48,
64, 96, 128, and 256 pixel frames. Each frame is a 32-bit DIB with alpha and
an AND mask for older icon consumers. Explorer11 embeds the same resources
for its navigation pane and This PC folder tiles.

To rebuild the ICOs from the transparent PNGs, install Pillow and NumPy in
a Python environment and run:

```sh
python media/graphics/user-folders/render-icons.py
```

To also repeat background removal from the five original files:

```sh
python media/graphics/user-folders/render-icons.py --originals /path/to/originals
```

Normal ReactOS builds use the checked-in ICOs and do not run the converter.
