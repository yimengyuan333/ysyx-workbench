#!/usr/bin/env python3
"""在裸 RISC-V 镜像里自动找出 "halt"（程序永不退出的自环），支持三种常见编译形态：

  A) jal  x0, 0                      -> 单条自环（0x0000006f，即 `j .`）
  B) jalr x0, addr(x0)               -> 立即数恰好等于自身地址
  C) lui rX,hi; addi rX,rX,lo; jalr x0,0(rX)
                                     -> 把自身地址装进寄存器再跳回去（三条自环）
  D) li rX,imm ; jalr x0,0(rX)       -> 同上，但地址小到一条 addi 就够（两条自环）

用法: python3 find_halt.py <img.bin>
说明: 只在"看起来像指令"的区域里报告，避免把数据里的巧合当成 halt。
"""
import sys

# RISC-V 合法 opcode（用于判断某处是代码还是数据）
VALID = {0x03, 0x0f, 0x13, 0x17, 0x23, 0x2f, 0x33, 0x37,
         0x63, 0x67, 0x6f, 0x73}


def s32(v):
    return v - (1 << 32) if v & 0x80000000 else v


def imm_i(w):                      # I 型立即数（符号扩展）
    return s32(w) >> 20


def imm_j(w):                      # J 型立即数（符号扩展）
    imm = (((w >> 31) & 1) << 20) | (((w >> 12) & 0xff) << 12) | \
          (((w >> 20) & 1) << 11) | (((w >> 21) & 0x3ff) << 1)
    return imm - (1 << 21) if imm & (1 << 20) else imm


def looks_like_code(words, center, span=8):
    """中心点前后 span 个字里，合法 opcode 的比例"""
    lo = max(0, center - span)
    hi = min(len(words), center + span + 1)
    chunk = words[lo:hi]
    return sum(1 for w in chunk if (w & 0x7f) in VALID) / len(chunk)


def try_indirect_jump(words, i, rs1):
    """words[i] 是 `jalr x0,0(rX)`，回溯看 rX 是否恰好被装成了这段代码自己的地址"""
    rs1 = rs1
    prev = words[i - 1]
    # D) li rX, imm  （即 addi rX, x0, imm）—— 低地址时编译器只用一条
    if (prev & 0x7f) == 0x13 and ((prev >> 12) & 7) == 0 \
            and ((prev >> 7) & 0x1f) == rs1 and ((prev >> 15) & 0x1f) == 0:
        val = imm_i(prev) & 0xffffffff
        if val == (i - 1) * 4:
            return (i - 1, (i - 1) * 4,
                    f"li x{rs1}, 0x{val:x} ; jalr x0,0(x{rs1})"
                    f"            # 两条自环 (D)")
    # C) lui rX,hi ; addi rX,rX,lo —— 高地址时两条拼出来
    if i >= 2:
        w1, w2 = words[i - 1], words[i - 2]
        if (w1 & 0x7f) == 0x13 and ((w1 >> 12) & 7) == 0 \
                and ((w1 >> 7) & 0x1f) == rs1 and ((w1 >> 15) & 0x1f) == rs1 \
                and (w2 & 0x7f) == 0x37 and ((w2 >> 7) & 0x1f) == rs1:
            val = (((w2 >> 12) & 0xfffff) << 12) + imm_i(w1)
            if val == (i - 2) * 4:
                return (i - 2, (i - 2) * 4,
                        f"lui x{rs1},.. ; addi x{rs1},x{rs1},.. ; "
                        f"jalr x0,0(x{rs1})   # 三条自环 (C)")
    return None


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 1
    data = open(sys.argv[1], 'rb').read()
    words = [int.from_bytes(data[i:i + 4], 'little')
             for i in range(0, len(data) // 4 * 4, 4)]

    hits = []
    for i, w in enumerate(words):
        ba = i * 4
        op = w & 0x7f
        if op == 0x6f:                                    # A) jal
            tgt = (ba + imm_j(w)) & 0xffffffff
            if tgt == ba:
                hits.append((i, ba, f"jal  x{(w >> 7) & 0x1f}, 0"
                                     f"            # 单条自环 (A)"))
        elif op == 0x67:                                  # B) / C) jalr
            f3, rd, rs1 = (w >> 12) & 7, (w >> 7) & 0x1f, (w >> 15) & 0x1f
            if f3 or rd:
                continue
            if rs1 == 0:                                  # jalr x0, imm(x0)
                if (imm_i(w) & 0xffffffff) == ba:
                    hits.append((i, ba, f"jalr x0, {imm_i(w)}(x0)"
                                         f"      # 立即数 = 自身 (B)"))
            else:                                         # jalr x0,0(rX)：回溯找 rX 的来源
                found = try_indirect_jump(words, i, rs1)
                if found:
                    hits.append(found)

    print(f"{sys.argv[1]}: {len(words)} 字 = {len(words) * 4} 字节")
    if not hits:
        print("没找到自环（可能是间接跳转/多分支汇合，需要看反汇编或跑 trace）")
        return 0
    for i, ba, desc in hits:
        tag = "代码" if looks_like_code(words, i) > 0.9 else "很可能是数据"
        print(f"  候选: byte 0x{ba:06x}  字[0x{i:x}]  {desc}   [{tag}]")
    return 0


if __name__ == '__main__':
    sys.exit(main())
