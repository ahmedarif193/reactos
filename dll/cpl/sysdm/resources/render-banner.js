/* Render the supplied SVG for the System Properties color/alpha resources. */
const fs = require('fs');
const path = require('path');
const { Resvg } = require('@resvg/resvg-js');
const { PNG } = require('pngjs');

const source = fs.readFileSync(path.join(__dirname, 'reactos-banner.svg'), 'utf8');
// Keep the current resource size and fit the SVG without changing its aspect ratio.
const svg = source.replace(/<svg\b[^>]*>/, tag => tag
    .replace(/\bwidth="[^"]*"/, 'width="320"')
    .replace(/\bheight="[^"]*"/, 'height="159"'));
const rendered = new Resvg(svg, { font: { loadSystemFonts: false } }).render();
// Decode the PNG to obtain straight RGB; InitLogo premultiplies it at load time.
const { width, height, data } = PNG.sync.read(rendered.asPng());

function bitmap(mask)
{
    const depth = mask ? 8 : 24;
    const stride = Math.ceil(width * depth / 32) * 4;
    const paletteSize = mask ? 256 * 4 : 0;
    const pixelOffset = 14 + 40 + paletteSize;
    const pixelsSize = stride * height;
    const result = Buffer.alloc(pixelOffset + pixelsSize);

    result.write('BM');
    result.writeUInt32LE(result.length, 2);
    result.writeUInt32LE(pixelOffset, 10);
    result.writeUInt32LE(40, 14); // BITMAPINFOHEADER
    result.writeInt32LE(width, 18);
    result.writeInt32LE(height, 22); // Bottom-up rows
    result.writeUInt16LE(1, 26);
    result.writeUInt16LE(depth, 28);
    result.writeUInt32LE(pixelsSize, 34);
    result.writeInt32LE(2835, 38);
    result.writeInt32LE(2835, 42);
    if (mask)
    {
        result.writeUInt32LE(256, 46);
        for (let i = 0; i < 256; ++i)
            result.fill(i, 54 + i * 4, 54 + i * 4 + 3);
    }

    for (let y = 0; y < height; ++y)
    {
        const row = pixelOffset + (height - 1 - y) * stride;
        for (let x = 0; x < width; ++x)
        {
            const src = (y * width + x) * 4;
            if (mask)
                result[row + x] = data[src + 3];
            else
            {
                const dst = row + x * 3;
                result[dst] = data[src + 2];
                result[dst + 1] = data[src + 1];
                result[dst + 2] = data[src];
            }
        }
    }
    return result;
}

fs.writeFileSync(path.join(__dirname, 'rosbitmap.bmp'), bitmap(false));
fs.writeFileSync(path.join(__dirname, 'rosbitmap_mask.bmp'), bitmap(true));
