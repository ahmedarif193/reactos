/* Render the supplied SVG artwork for Explorer's WIC image pipeline. */
const fs = require('fs');
const path = require('path');
const { Resvg } = require('@resvg/resvg-js');

const states = { default: 'color', hover: 'hover', pressed: 'pressed' };
for (const [state, source] of Object.entries(states))
{
    const svg = fs.readFileSync(path.join(__dirname, `reactos-start-${source}.svg`), 'utf8');
    // The hover/pressed SVGs add four units around the same 32-unit logo.
    // Give every state that canvas so switching states never rescales the logo.
    const aligned = svg.replace(/<svg\b[^>]*>/,
        '<svg xmlns="http://www.w3.org/2000/svg" width="40" height="40" viewBox="-4 -4 40 40">');
    const image = new Resvg(aligned, {
        fitTo: { mode: 'width', value: 640 },
        font: { loadSystemFonts: false }
    }).render();
    fs.writeFileSync(path.join(__dirname, `${state}.png`), image.asPng());
}
