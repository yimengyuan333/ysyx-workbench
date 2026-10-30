#!/usr/bin/env python3
"""把 EMU 导出的 24 位 BMP 转成 PNG（纯标准库，不依赖 PIL/ffmpeg）。

VS Code 的图片预览不支持 BMP，所以转一下才能直接看。

用法：
    python3 bmp2png.py [in.bmp] [out.png]
"""

import struct
import sys
import zlib


def read_bmp(path):
    d = open(path, "rb").read()
    assert d[:2] == b"BM", "不是 BMP 文件"
    off = struct.unpack_from("<I", d, 10)[0]
    w, h = struct.unpack_from("<ii", d, 18)
    bpp = struct.unpack_from("<H", d, 28)[0]
    assert bpp == 24, f"只支持 24 位 BMP，当前 {bpp}"
    flip = h > 0                      # h>0 表示行自底向上
    h = abs(h)
    row = (w * 3 + 3) // 4 * 4
    px = bytearray()
    for y in range(h):
        src = off + (h - 1 - y) * row if flip else off + y * row
        line = d[src:src + w * 3]
        # BGR -> RGB
        rgb = bytearray(w * 3)
        rgb[0::3], rgb[1::3], rgb[2::3] = line[2::3], line[1::3], line[0::3]
        px += rgb
    return w, h, bytes(px)


def write_png(path, w, h, rgb):
    def chunk(tag, data):
        c = tag + data
        return struct.pack(">I", len(data)) + c + struct.pack(">I", zlib.crc32(c))

    raw = b"".join(b"\x00" + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))
    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(raw, 9))
           + chunk(b"IEND", b""))
    open(path, "wb").write(png)


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else "out.bmp"
    dst = sys.argv[2] if len(sys.argv) > 2 else src.rsplit(".", 1)[0] + ".png"
    w, h, rgb = read_bmp(src)
    write_png(dst, w, h, rgb)
    print(f"{src} -> {dst} ({w}x{h})")


if __name__ == "__main__":
    main()
