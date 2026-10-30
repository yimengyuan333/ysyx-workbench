# minirvEMU

一个极简的 RV32E 教学解释器（取指走 `ROM`、数据走 `RAM`），外加两个小脚本，
用来把 Logisim 的十六进制镜像转成裸二进制并自动定位程序里的 `halt`。

## 文件一览

| 文件 | 说明 |
|---|---|
| `EMU_mem.c` | 解释器主程序：镜像同时载入 ROM/RAM + 结束时 dump GPR 和结果区 |
| `makefile` | 和 `kernels/screensaver` 一样，直接 `include $(AM_HOME)/Makefile` |
| `hex2bin.py` | 工具①：Logisim hex → 裸二进制 |
| `find_halt.py` | 工具②：自动定位 `halt` |
| `*.hex` | Logisim `v3.0 hex words addressed` 镜像（`mem` / `sum` / `vga` / `prog_good`） |
| `*.bin` | 转换出的裸镜像，或手写的小镜像 |
| `build/EMU_mem-native.elf` | 编译产物（AM native 平台的完整运行时，含 SDL 显示） |

## 工具用法

### ① hex2bin.py —— Logisim hex → 裸二进制

```bash
python3 hex2bin.py <in.hex> <out.bin>
```

- 输入：Logisim 的 `v3.0 hex words addressed` 格式，每行 `<字下标>: w0 w1 ... w7`
- 注意 **地址是"字下标"而不是字节地址**（字节地址 = 字下标 × 4）
- 输出按**小端**写成 4 字节/字，正好是 EMU `fread` 进 `uint32_t ROM[]` 需要的排列
- 例：

```bash
$ python3 hex2bin.py mem.hex mem.bin
mem.hex -> mem.bin: 75184 字 = 300736 字节
```

### ② find_halt.py —— 自动定位 `halt`

```bash
python3 find_halt.py <img.bin>
```

程序结尾的 `halt` 是个"永不退出的自环"，编译器会写成几种不同的形态，脚本都能认：

| 形态 | 样子 | 例 |
|---|---|---|
| A | `jal x0, 0`（即 `j .`） | 单条自环 |
| B | `jalr x0, addr(x0)`，且 `addr` 恰好等于该指令自身地址 | `prog_good.bin` @ `0xc` |
| C | `lui rX,hi` + `addi rX,rX,lo` + `jalr x0,0(rX)` | `mem.bin` @ `0x1218` |
| D | `li rX,addr` + `jalr x0,0(rX)`（地址不大时不用 `lui`） | `sum.bin` @ `0x224` |

输出会带 `[代码]` / `[很可能是数据]` 标记——用中心点前后 8 个字的"合法 opcode 比例"
区分代码和数据，这样数据区里恰好长得像 `j .` 的字节不会误导你：

```bash
$ python3 find_halt.py mem.bin
mem.bin: 75184 字 = 300736 字节
  候选: byte 0x001218  字[0x486]   lui x4,.. ; addi x4,x4,.. ; jalr x0,0(x4)   # 三条自环 (C)   [代码]
  候选: byte 0x0317bc  字[0xc5ef]  jal  x0, 0                                # 单条自环 (A)   [很可能是数据]
  ...
```

拿到地址后直接打补丁（`>> 2` 是"字节地址 → 字下标"的换算）：

```c
ROM[addr >> 2] = 0x00100073;   // ebreak
```

## 完整流程

```bash
# 1. hex -> 裸 bin
python3 hex2bin.py mem.hex mem.bin

# 2. 找 halt 地址
python3 find_halt.py mem.bin            # -> 0x1218

# 3. 把地址填进 EMU_mem.c 的运行时补丁
#    ROM[0x1218 >> 2] = 0x00100073;

# 4. 编译并运行（和 screensaver 一样走 AM 的 Makefile，产物在 build/ 下）
make ARCH=native                 # -> build/EMU_mem-native.elf
./build/EMU_mem-native.elf       # 或 make ARCH=native run
```

镜像名和 halt 补丁地址都写死在 `EMU_mem.c` 的 `main()` 里，换镜像时改这两行：

```c
const char *instruction_sequence = "vga.bin";
//ROM[0x1218 >> 2] = 0x00100073;   // 测 mem.bin 时放开，地址换成 find_halt.py 给的
```

> 补丁默认注释掉：`vga.bin` 自带 `ebreak`，而 `0x1218` 落在它的代码里，硬补会跑飞
> （实测会直接打 `程序没有写过显存`）。`mem.bin` / `sum.bin` 结尾是自旋，才需要补丁。
>
> `main` 只写 `int main()`（和 screensaver 一致）：AM 的 native 平台由
> `am/src/native/platform.c` 的构造函数用 `main(mainargs)` 调 kernel，
> kernel 拿不到 `argc/argv`，我们也不需要它。

## 已知镜像

| 镜像 | 大小 | `halt` | 期望输出 |
|---|---|---|---|
| `mem.bin` | 300736 | `0x1218` | `HIT GOOD TRAP` |
| `sum.bin` | 296512 | `0x224` | `HIT GOOD TRAP` |
| `prog_good.bin` | 28 | `0xc`（自旋，靠补丁替换） | `HIT GOOD TRAP` |
| `prog.bin` | 24 | 文件里自带 `ebreak` | `HIT BAD TRAP`（结束时 a0=30） |

## 约定与常量

- **运行时约定**：程序结束前把状态码写进 `a0`（=`x10`），`0` → `HIT GOOD TRAP`，
  非 0 → `HIT BAD TRAP`。EMU 无法判断"算得对不对"，只能读程序自报的状态码。
- **`ebreak`** = `0x00100073`（`opcode=0x73 SYSTEM`，`funct3=0`，`imm=1`；`ecall` 是 `imm=0`）。
- **运行时补丁**：镜像里的 `halt` 是自旋，EMU 在 `fread` 之后把它替换成 `ebreak`，
  于是"程序结束"由运行环境保证，程序本身不需要知道这个机制。
- `ROM` / `RAM` 各 64 MiB（`uint32_t[1u << 24]`），**按字寻址**，且两者是分离的地址空间。

## 踩过的坑

1. **大镜像（`mem` / `sum`）必须同时载入 RAM。** 镜像里既有代码也有初始数据
   （测试向量就躺在 `0x3d600` / `0x31500` / `0x49690` 这些地址上），只读进 `ROM`
   的话程序读到的数据全是 0，计算跳转会乱飞——表现为跑几千万条指令后 SIGSEGV。
   ```c
   for (size_t i = 0; i < size / 4; i++) RAM[i] = ROM[i];
   ```
2. **补丁地址每个程序都不同**，别硬编码同一个；用 `find_halt.py` 取。
3. **别复用已 `fclose` 的 `FILE*`**：想在 `fclose` 之后再 `fread` 一次来载入 RAM 是
   use-after-free；若要用两次 `fread`，必须放在 `fclose` 之前并先 `rewind(fp)`。
4. **反汇编核对**：
   ```bash
   riscv64-linux-gnu-objdump -D -b binary -m riscv:rv32 mem.bin | less
   ```
5. **gdb 里别 `p ROM`**（64 MiB 会刷屏），用 `x/8wx ROM`；寄存器看 `p/x GPR[10]`（a0）。
