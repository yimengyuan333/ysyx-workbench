#!/usr/bin/env python3
"""把 Logisim 的 "v3.0 hex words addressed" 文件转成 minirvEMU 能读的裸二进制。

用法: python3 hex2bin.py <in.hex> <out.bin>
说明: hex 里的地址是"字下标"，每行最多 8 个字；输出的 bin 是小端 4 字节/字。
"""
import sys


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 1
    words = []
    with open(sys.argv[1]) as f:
        for line in f:
            line = line.strip()
            if not line or ':' not in line:      # 跳过 "v3.0 hex words addressed" 之类的头
                continue
            _, rest = line.split(':', 1)
            words.extend(int(w, 16) & 0xffffffff for w in rest.split())
    with open(sys.argv[2], 'wb') as f:
        for w in words:
            f.write(w.to_bytes(4, 'little'))
    print(f"{sys.argv[1]} -> {sys.argv[2]}: {len(words)} 字 = {len(words) * 4} 字节")
    return 0


if __name__ == '__main__':
    sys.exit(main())
