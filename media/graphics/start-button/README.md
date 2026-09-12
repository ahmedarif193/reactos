The three `reactos-start-*.svg` files are the supplied Start button artwork:
`color` for normal, `hover` for hot, and `pressed` while clicked or the menu is open.

`render.js` produces the transparent PNG resources consumed by Explorer's WIC
renderer. All states use the same `-4 -4 40 40` canvas, including the normal SVG,
so the logo keeps its position and scale while leaving room for the glow.
Explorer retains its existing 36-pixel image size, DPI scaling, and button margins.
Disabled and keyboard-focused buttons reuse the normal artwork.

The PNGs were generated with `@resvg/resvg-js` 2.6.2. To regenerate them from the
repository root using a temporary dependency directory:

```sh
orb_tools_dir=$(mktemp -d)
npm install --prefix "$orb_tools_dir" --no-audit --no-fund @resvg/resvg-js@2.6.2
NODE_PATH="$orb_tools_dir/node_modules" node media/graphics/start-button/render.js
```

Normal ReactOS builds use the checked-in PNGs and do not require Node.js or resvg.
