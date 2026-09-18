`reactos-banner.svg` contains the supplied pastel ReactOS logo and outlined
wordmark. `render-banner.js` renders it directly at the existing 320 by 159 pixel
resource size, preserving the artwork's aspect ratio with transparent padding.
The wordmark uses paths, so rendering does not depend on installed fonts.

System Properties loads `rosbitmap.bmp` as 24-bit RGB and
`rosbitmap_mask.bmp` as an 8-bit grayscale alpha mask. White is opaque and black
is transparent; intermediate values preserve antialiased edges. The RGB resource
is not premultiplied because `InitLogo` applies the alpha before `AlphaBlend`.
Neither resource includes the dark background from the supplied previews.

To regenerate the resources from the repository root:

```sh
banner_tools_dir=$(mktemp -d)
npm install --prefix "$banner_tools_dir" --no-audit --no-fund @resvg/resvg-js@2.6.2 pngjs@7.0.0
NODE_PATH="$banner_tools_dir/node_modules" node dll/cpl/sysdm/resources/render-banner.js
```

Normal ReactOS builds use the bundled BMP resources and do not require Node.js,
resvg, or pngjs.
