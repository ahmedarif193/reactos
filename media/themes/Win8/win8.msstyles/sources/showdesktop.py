import os
import struct
import sys
import zlib

STYLES = os.environ.get("WIN81_AERO_MSSTYLES",
                        "/Users/mac/working_dir/win81-fs/Windows/Resources/Themes/aero/aero.msstyles")
HORIZONTAL_IMAGE = 1276
VERTICAL_IMAGE = 1278


def sections(data):
    e = struct.unpack("<I", data[0x3C:0x40])[0]
    nsec = struct.unpack("<H", data[e + 6:e + 8])[0]
    ohsz = struct.unpack("<H", data[e + 20:e + 22])[0]
    magic = struct.unpack("<H", data[e + 24:e + 26])[0]
    dd = e + 24 + (96 if magic == 0x10B else 112)
    res = struct.unpack("<I", data[dd + 16:dd + 20])[0]
    tbl = e + 24 + ohsz
    out = []
    for i in range(nsec):
        r = data[tbl + i * 40:tbl + (i + 1) * 40]
        vs, va, rs, ra = struct.unpack("<IIII", r[8:24])
        out.append((va, max(vs, rs), ra))
    return res, out


def to_offset(secs, rva):
    for va, sz, ra in secs:
        if va <= rva < va + sz:
            return ra + (rva - va)
    raise KeyError(rva)


def walk(data, res, secs, rva, path):
    off = to_offset(secs, rva)
    named, ids = struct.unpack("<HH", data[off + 12:off + 16])
    out = []
    for i in range(named + ids):
        name, child = struct.unpack("<II", data[off + 16 + i * 8:off + 24 + i * 8])
        if name & 0x80000000:
            no = to_offset(secs, res) + (name & 0x7FFFFFFF)
            ln = struct.unpack("<H", data[no:no + 2])[0]
            name = data[no + 2:no + 2 + ln * 2].decode("utf-16le")
        if child & 0x80000000:
            out += walk(data, res, secs, res + (child & 0x7FFFFFFF), path + (name,))
        else:
            do = to_offset(secs, res) + child
            drva, dsz = struct.unpack("<II", data[do:do + 8])
            out.append((path + (name,), to_offset(secs, drva), dsz))
    return out


def resource(path, kind, ident):
    data = open(path, "rb").read()
    res, secs = sections(data)
    for name, off, size in walk(data, res, secs, res, ()):
        if name[0] == kind and name[1] == ident:
            return data[off:off + size]
    raise KeyError(ident)


def png(blob):
    idat = b""
    pal = trns = None
    i = 8
    while i < len(blob):
        ln = struct.unpack(">I", blob[i:i + 4])[0]
        tag = blob[i + 4:i + 8]
        chunk = blob[i + 8:i + 8 + ln]
        i += 12 + ln
        if tag == b"IHDR":
            w, h, depth, color = struct.unpack(">IIBB", chunk[:10])
        elif tag == b"IDAT":
            idat += chunk
        elif tag == b"PLTE":
            pal = chunk
        elif tag == b"tRNS":
            trns = chunk
    raw = zlib.decompress(idat)
    nch = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[color]
    bpp = max(1, nch * depth // 8)
    stride = (w * nch * depth + 7) // 8
    out = bytearray()
    prev = bytearray(stride)
    pos = 0
    for _ in range(h):
        filt = raw[pos]
        pos += 1
        line = bytearray(raw[pos:pos + stride])
        pos += stride
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0
            b = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            if filt == 1:
                line[x] = (line[x] + a) & 255
            elif filt == 2:
                line[x] = (line[x] + b) & 255
            elif filt == 3:
                line[x] = (line[x] + ((a + b) >> 1)) & 255
            elif filt == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        out += line
        prev = line
    rows = []
    for y in range(h):
        row = []
        for x in range(w):
            base = y * stride + x * bpp
            if color == 6:
                row.append(tuple(out[base:base + 4]))
            elif color == 2:
                row.append(tuple(out[base:base + 3]) + (255,))
            elif color == 3:
                idx = out[base]
                alpha = trns[idx] if trns and idx < len(trns) else 255
                row.append((pal[idx * 3], pal[idx * 3 + 1], pal[idx * 3 + 2], alpha))
            else:
                v = out[base]
                row.append((v, v, v, out[base + 1] if color == 4 else 255))
        rows.append(row)
    return w, h, rows


def straighten(rows):
    out = []
    for row in rows:
        line = []
        for r, g, b, a in row:
            if a == 0:
                line.append((0, 0, 0, 0))
            else:
                line.append((min(255, (r * 255 + a // 2) // a),
                             min(255, (g * 255 + a // 2) // a),
                             min(255, (b * 255 + a // 2) // a), a))
        out.append(line)
    return out


def save_bmp(w, h, rows, path):
    bits = bytearray()
    for y in range(h - 1, -1, -1):
        for r, g, b, a in rows[y]:
            bits += bytes((b, g, r, a))
    hdr = struct.pack("<IiiHHIIiiII", 108, w, h, 1, 32, 3, len(bits), 2835, 2835, 0, 0)
    hdr += struct.pack("<IIII", 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000)
    hdr += b"BGRs" + bytes(36) + struct.pack("<III", 0, 0, 0)
    off = 14 + len(hdr)
    with open(path, "wb") as f:
        f.write(b"BM" + struct.pack("<IHHI", off + len(bits), 0, 0, off) + hdr + bits)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "bitmaps")
    for ident, name in ((HORIZONTAL_IMAGE, "NORMAL_SHOWDESKTOP.bmp"),
                        (VERTICAL_IMAGE, "NORMAL_SHOWDESKTOPVERTICAL.bmp")):
        w, h, rows = png(resource(STYLES, "IMAGE", ident))
        save_bmp(w, h, straighten(rows), os.path.join(out, name))


if __name__ == "__main__":
    main()
