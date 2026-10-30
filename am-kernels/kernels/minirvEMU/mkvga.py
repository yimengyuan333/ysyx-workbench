#!/usr/bin/env python3
"""生成一个 VGA 测试镜像：256x256 的 8 条竖彩条。

只用到 lui / addi / sw / ebreak 四条指令，完全落在 minirvEMU 的
8 指令子集内，并且镜像自带 ebreak，所以不需要 find_halt.py 打补丁。

用法：
    python3 mkvga.py [out.bin]      # 默认 vgatest.bin
    ./EMU_mem vgatest.bin           # 注意：不要给第二个参数（halt 地址）
"""

import struct
import sys

W = H = 256
FB = 0x20000000

COLORS = [0xFF0000, 0x00FF00, 0x0000FF, 0xFFFF00,
          0xFF00FF, 0x00FFFF, 0xFFFFFF, 0x808080]

T0, T1 = 5, 6          # x5, x6（RV32E 只有 x0~x15）


# ---------- 指令编码 ----------
def LUI(rd, imm20):
    return ((imm20 & 0xFFFFF) << 12) | (rd << 7) | 0x37


def ADDI(rd, rs1, imm):
    return ((imm & 0xFFF) << 20) | (rs1 << 15) | (rd << 7) | 0x13


def SW(rs2, rs1, imm):
    imm &= 0xFFF
    return (((imm >> 5) & 0x7F) << 25) | (rs2 << 20) | (rs1 << 15) \
         | (0b010 << 12) | ((imm & 0x1F) << 7) | 0x23


EBREAK = 0x00100073


def li(rd, val):
    """lui + addi 组成 li，返回指令列表"""
    hi = ((val & 0xFFFFFFFF) + 0x800) >> 12
    lo = (val & 0xFFFFFFFF) - (hi << 12)
    if hi == 0:
        return [ADDI(rd, 0, lo)]
    out = [LUI(rd, hi)]
    if lo:
        out.append(ADDI(rd, rd, lo))
    return out


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "vgatest.bin"
    ins = []

    ins += li(T0, FB)                  # t0 = 显存基址 0x20000000

    # sw 的立即数只有 12 位有符号（-2048..2047），所以每 511 个像素
    # （511*4 = 2044 字节）就要把基址往前推进 2044
    CHUNK = 511
    idx = 0
    cur_bar = -1
    while idx < W * H:
        if idx:
            ins.append(ADDI(T0, T0, CHUNK * 4))
        n = min(CHUNK, W * H - idx)
        for k in range(n):
            bar = ((idx + k) % W) // 32          # 每 32 像素换一条
            if bar != cur_bar:
                ins += li(T1, COLORS[bar])
                cur_bar = bar
            ins.append(SW(T1, T0, k * 4))
        idx += n

    ins.append(EBREAK)

    with open(out, "wb") as f:
        for i in ins:
            f.write(struct.pack("<I", i))
    print(f"{out}: {len(ins)} 条指令, {len(ins) * 4} 字节")


if __name__ == "__main__":
    main()
