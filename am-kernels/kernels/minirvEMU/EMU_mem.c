#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <stdbool.h>
#include <am.h>				//它声明了io_write
#include <klib-macros.h>   	//定义了io_write

//这是一个简单的宏拓展，相当于logisim的分线器（？用于从指令中取位
#define GET_BITS(v, hi, lo)  (((v) >> (lo)) & ((1u << ((hi) - (lo) + 1)) - 1))

//后面发现opcode和funct3的解析很麻烦。各自单独写成宏
#define ADDI	0b0010011u
#define JALR    0b1100111u
#define ADD		0b0110011u
#define LUI		0b0110111u
#define LW_LBU	0b0000011u
#define SW_SB	0b0100011u
#define SYSTEM	0b1110011u

//I型指令的操作对象解析出来
#define I_RD(i)      (GET_BITS(i, 11, 7) & 0xf)  // 改：RV32E 寄存器号只用低 4 位
#define I_FUNCT3(i)  GET_BITS(i, 14, 12)
#define I_RS1(i)     (GET_BITS(i, 19, 15) & 0xf) // 改：同上
#define I_IMM(i)     ((int32_t)(i) >> 20)

//R型指令的操作对象解析出来
#define R_RD(i)      (GET_BITS(i, 11, 7) & 0xf)  // 改：RV32E 寄存器号只用低 4 位
#define R_FUNCT3(i)  GET_BITS(i, 14, 12)
#define R_RS1(i)     (GET_BITS(i, 19, 15) & 0xf) // 改：同上
#define R_RS2(i)     (GET_BITS(i, 24, 20) & 0xf) // 改：同上
#define R_FUNCT7(i)  GET_BITS(i, 31, 25)

//U型指令的操作对象解析出来
#define U_IMM(i)	 GET_BITS(i, 31, 12)
#define U_RD(i)	 (GET_BITS(i, 11, 7) & 0xf)  // 改：RV32E 寄存器号只用低 4 位

//S型
#define S_IMM_HIGH(i)   GET_BITS(i, 31, 25)
#define S_RS2(i)    (GET_BITS(i, 24, 20) & 0xf) // 改：RV32E 寄存器号只用低 4 位
#define S_RS1(i)    (GET_BITS(i, 19, 15) & 0xf) // 改：同上
#define S_FUNCT3(i)   GET_BITS(i, 14, 12)
#define S_IMM_LOW(i)   GET_BITS(i, 11, 7)

//这是10.28做的VGA功能的宏定义
#define VGA_BASE   0x20000000u	//内存映射基址
#define VGA_W      256
#define VGA_H      256
#define VGA_STRIDE (VGA_W * 4)	//一行多少字节
#define VGA_SIZE   (VGA_H * VGA_STRIDE)	//一共多少字节

uint32_t PC = 0;
uint32_t GPR[16] = {0}; // RV32E
int halted = 0;			//停止标识
uint32_t ROM[1u << 24] = {0};
uint32_t RAM[1u << 24] = {0};
uint32_t VGA_VM[VGA_W * VGA_H];	//显存，一共65536个字
int      vga_dirty = 0;	//显示完毕标识

/* ---- 显示：走 AM 的 IOE 接口，和 screensaver 的 draw() 是同一行 ----
 * SDL 窗口和每帧刷屏都由 AM native 平台负责：
 *   am/src/native/ioe.c       的分发表把 AM_GPU_FBDRAW 派给
 *   am/src/native/ioe/gpu.c   的 __am_gpu_fbdraw()（窗口由 __am_gpu_init() 建）
 * 所以这里不用声明底层函数，也不用自己写 ioe_read / ioe_write。
 */
static void vga_show(void) {
	io_write(AM_GPU_FBDRAW, 0, 0, VGA_VM, VGA_W, VGA_H, true);
}

/* addr 是否落在显存内；无符号减法让越界/下溢自然为假 */
static inline int in_vga(uint32_t addr) {
	return addr - VGA_BASE < VGA_SIZE;
}

/* 写显存：nbytes = 4 (SW) 或 1 (SB) */
static void vga_store(uint32_t addr, uint32_t data, int nbytes) {
	uint32_t off = addr - VGA_BASE;
	if (nbytes == 4) {
		VGA_VM[off >> 2] = data & 0x00FFFFFFu;      /* 硬件丢高 8 位 */
	} else {
		uint32_t *p  = &VGA_VM[off >> 2];           /* 字节使能：读-改-写 */
		uint32_t  sh = (off & 3) * 8;
		*p = ((*p & ~(0xFFu << sh)) | ((data & 0xFFu) << sh)) & 0x00FFFFFFu;
	}
	vga_dirty = 1;
}

/* 读显存（程序有可能把像素读回来） */
static uint32_t vga_load(uint32_t addr, int nbytes) {
	uint32_t off = addr - VGA_BASE;
	if (nbytes == 4) return VGA_VM[off >> 2];
	return (VGA_VM[off >> 2] >> ((off & 3) * 8)) & 0xFFu;
}

/* 显示已经交给 AM 的 GPU 接口，不再自己写 BMP 导出 */

void inst_cycle(void){
	uint32_t inst    = ROM[PC >> 2];
	uint32_t opcode  = GET_BITS(inst, 6, 0);//opcode其实是唯一一个可以写在外层switch之外的部分
	uint32_t next_pc = PC + 4;

	switch (opcode) {
	
	//确定I型指令
	case ADDI: {
		//已经确定是I型了之后这些也可以确定下来
		uint32_t rd  = I_RD(inst);
		uint32_t rs1 = I_RS1(inst);
		int32_t  imm = I_IMM(inst);

		//下面开始解析funct3
		switch (I_FUNCT3(inst)) {
		//目前funct只涉及000
		case 0b000:
			GPR[rd] = GPR[rs1] + (uint32_t)imm;
			break;
		default:
			break;
		}
		break;
	}

	case JALR: {
		uint32_t rd  = I_RD(inst);
		uint32_t rs1 = I_RS1(inst);
		int32_t  imm = I_IMM(inst);

		if (I_FUNCT3(inst) == 0b000) {
			uint32_t target = (GPR[rs1] + (uint32_t)imm) & ~1u;
			GPR[rd] = next_pc;
			next_pc = target;
		}
		break;
	}

	case LUI: {
		uint32_t imm = U_IMM(inst);
		uint32_t rd = U_RD(inst);
		GPR[rd] = (imm << 12 & 0xfffff000);//掩码把imm推到高位，低位补零
		break;
	}

	case ADD: {
		// 目前只实现 funct7=0000000 & funct3=000 的 ADD，其余 R 型先不区分
		uint32_t rs2 = R_RS2(inst);
		uint32_t rs1 = R_RS1(inst);
		uint32_t rd = R_RD(inst);
		GPR[rd] = GPR[rs1] + GPR[rs2];
		break;
	}

	case LW_LBU:{
        uint32_t imm = I_IMM(inst);
        uint32_t rs1 = I_RS1(inst);
		uint32_t funct3 = I_FUNCT3(inst);
        uint32_t rd = I_RD(inst);
		
		switch (funct3) {
			case 0b010:{
				uint32_t a = GPR[rs1] + (uint32_t)imm;
				GPR[rd] = in_vga(a) ? vga_load(a, 4) : RAM[a >> 2]; // 改：基址用 GPR[rs1]，并按字寻址
                break;
			}

            case 0b100:{
                uint32_t addr = GPR[rs1] + imm; // 改：按字节地址取对应字节
                GPR[rd] = in_vga(addr) ? vga_load(addr, 1) : (RAM[addr >> 2] >> ((addr & 3) * 8)) & 0xff;
                break;
            }

            default:
                break;
		}
		break;
	}

    case SW_SB:{
        uint32_t imm_high = S_IMM_HIGH(inst);
        uint32_t rs2 = S_RS2(inst);
        uint32_t rs1 = S_RS1(inst);
        uint32_t funct3 = S_FUNCT3(inst);
        uint32_t imm_low = S_IMM_LOW(inst);
        int32_t  imm = (int32_t)(((imm_high << 5) | imm_low) << 20) >> 20; //S型立即数有符号扩展


        switch (funct3) {
			//SW
            case 0b010:{
				uint32_t addr = GPR[rs1] + (uint32_t)imm;
				if (in_vga(addr)) {
					vga_store(addr, GPR[rs2], 4);   /* SW：写一个像素，低 24 位有效 */
				} else {
					//先把拼好数imm左移20位是为了顶到31位，使得>>算数右移是符号拓展
					RAM[(GPR[rs1] + (uint32_t)imm) >> 2] = GPR[rs2]; // 改：基址用 GPR[rs1]，符号扩展立即数，按字寻址    
				}
				break;
            }

            case 0b000:{
                uint32_t addr = GPR[rs1] + (uint32_t)imm; // 改：只改目标字节，读-改-写
                if (in_vga(addr)) { vga_store(addr, GPR[rs2], 1); break; }   /* SB 写显存 */
                if (addr == 0x10000000u) {                                    /* 字符输出口 */
                    putchar((int)(GPR[rs2] & 0xFF)); fflush(stdout); break;
                }
                uint32_t sh = (addr & 3) * 8;
                RAM[addr >> 2] = (RAM[addr >> 2] & ~(0xffu << sh)) | ((GPR[rs2] & 0xff) << sh);
                break;
            }

            default:
                break;
        }
		break;

    }

	case SYSTEM:{
		uint32_t funct3 = I_FUNCT3(inst);
		int32_t imm = I_IMM(inst);

		if(funct3 == 0b000){
			if(imm == 1){          // ebreak：约定为程序结束
				halted = 1;        // 置位，跳出取指循环并判定结果
			}
		}
		break;
	}

	default:
		break;
	}

	GPR[0] = 0;
	PC = next_pc;
}

/* AM 的 native 平台不是由 C 运行库调 main(argc, argv)，而是由
 * am/src/native/platform.c 里的构造函数用 main(getenv("mainargs") ?: "")
 * 调用，所以 kernel 的入口签名固定是 int main(const char *args)。
 * args 是一个空格分隔的字符串：
 *     mainargs="mem.bin 0x1218" ./build/EMU_mem-native.elf
 */
int main(const char *args){
	ioe_init();   /* 和 screensaver 一样：先把 AM 的 IOE 初始化起来 */

	char        argbuf[256] = {0};
	const char *instruction_sequence = "vga.bin";
	const char *halt_arg             = NULL;

	if (args != NULL && *args != '\0') {
		snprintf(argbuf, sizeof(argbuf), "%s", args);
		char *p = strtok(argbuf, " ");
		if (p != NULL) instruction_sequence = p;
		halt_arg = strtok(NULL, " ");
	}

	//把程序镜像读进ROM
	FILE *ROM_fp = fopen(instruction_sequence, "rb");
	if (ROM_fp == NULL) { perror(instruction_sequence); return 1; }//打开文件失败了
	size_t size = fread(ROM, 1, sizeof(ROM), ROM_fp);
	fclose(ROM_fp);

	for (size_t i = 0; i < size / 4; i++) RAM[i] = ROM[i];

	//运行时环境补丁。约定程序结束前把状态码写入a0，并停在halt处。
	/* 第二个参数给出 halt 的字节地址时才补 ebreak；
	   不给则依赖镜像自带的 ebreak（mkvga.py 生成的测试镜像自带） */
	if (halt_arg != NULL) {
		uint32_t halt_addr = (uint32_t)strtoul(halt_arg, NULL, 0);
		ROM[halt_addr >> 2] = 0x00100073; // ebreak
		printf("已把 0x%x 处的自环替换成 ebreak\n", halt_addr);
	}
	
	//什么时候循环会一直运行？
	uint64_t steps = 0;
	while (!halted && PC < size && steps++ < 200000000ull) {
		inst_cycle();
		/* 每 65536 条指令推一帧给 AM，相当于我们自己定的帧节奏 */
		if (vga_dirty && (steps & 0xFFFFu) == 0) vga_show();
	}

	if (halted) {
		if (GPR[10] == 0) printf("HIT GOOD TRAP\n");	//置位0就是正确返回码，事实上我们的EMU非常简单
		else              printf("HIT BAD TRAP\n");	//根本就没有0以外的halted置位
	} else {
		printf("没有执行到 ebreak 就结束了：PC=0x%08x, size=%zu\n", PC, size);
	}

	//打印全部GPR
	const char *abi[16] = {"zero","ra","sp","gp","tp","t0","t1","t2",
	                       "s0","s1","a0","a1","a2","a3","a4","a5"};
	printf("---- GPR ----\n");
	for (int i = 0; i < 16; i++)
		printf("x%-2d %-4s = 0x%08x (%d)\n", i, abi[i], GPR[i], (int32_t)GPR[i]);

	//打印mem测试的结果区
	printf("---- RAM[0x49690..0x496bf] ----\n");
	for (int i = 0; i < 12; i++)
		printf("RAM[0x%05x] = 0x%08x\n", 0x49690 + i * 4, RAM[(0x49690 >> 2) + i]);

	//最后把整块显存交给 AM 显示（screensaver 里也是每帧这一次调用）
	if (vga_dirty) {
		vga_show();
		printf("画面已显示在窗口里，按回车退出\n");
		getchar();
	} else {
		printf("程序没有写过显存 0x%08x，没有画面\n", VGA_BASE);
	}
	
	return 0;
}