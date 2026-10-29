#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
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

uint32_t PC = 0;
uint32_t GPR[16] = {0}; // RV32E
int halted = 0;			//停止标识
uint32_t ROM[1u << 24] = {0};
uint32_t RAM[1u << 24] = {0};

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
		uint32_t funct7 = R_FUNCT7(inst);
		uint32_t rs2 = R_RS2(inst);
		uint32_t rs1 = R_RS1(inst);
		uint32_t funct3 = R_FUNCT3(inst);
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
				GPR[rd] = RAM[(GPR[rs1] + imm) >> 2]; // 改：基址用 GPR[rs1]，并按字寻址
                break;
			}

            case 0b100:{
                uint32_t addr = GPR[rs1] + imm; // 改：按字节地址取对应字节
                GPR[rd] = (RAM[addr >> 2] >> ((addr & 3) * 8)) & 0xff;
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
        int32_t  imm = (int32_t)(((imm_high << 5) | imm_low) << 20) >> 20; // 改：S 型立即数符号扩展

        switch (funct3) {
            case 0b010:{
				//这里要注意啊，先把拼好数imm左移20位是为了顶到31位，使得>>算数右移是符号拓展
                RAM[(GPR[rs1] + (uint32_t)imm) >> 2] = GPR[rs2]; // 改：基址用 GPR[rs1]，符号扩展立即数，按字寻址
                break;
            }

            case 0b000:{
                uint32_t addr = GPR[rs1] + (uint32_t)imm; // 改：只改目标字节，读-改-写
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

int main(int argc, char *argv[]){
	//把程序镜像读进ROM
	const char *instruction_sequence = (argc > 1) ? argv[1] : "vga.bin";
	FILE *fp = fopen(instruction_sequence, "rb");
	if (fp == NULL) { perror(instruction_sequence); return 1; }//打开文件失败了
	size_t size = fread(ROM, 1, sizeof(ROM), fp);
	fclose(fp);

	//运行时环境补丁。约定程序结束前把状态码写入a0，并停在halt处。
	ROM[0xc >> 2] = 0x00100073;
	
	//什么时候循环会一直运行？
	while (!halted && PC < size) inst_cycle();

	if (halted) {
		if (GPR[10] == 0) printf("HIT GOOD TRAP\n");	//置位0就是正确返回码，事实上我们的EMU非常简单
		else              printf("HIT BAD TRAP\n");	//根本就没有0以外的halted置位
	} else {
		printf("没有执行到 ebreak 就结束了：PC=0x%08x, size=%zu\n", PC, size);
	}

	//调试：打印全部 GPR（RV32E 只有 x0~x15）
	const char *abi[16] = {"zero","ra","sp","gp","tp","t0","t1","t2",
	                       "s0","s1","a0","a1","a2","a3","a4","a5"};
	printf("---- GPR ----\n");
	for (int i = 0; i < 16; i++)
		printf("x%-2d %-4s = 0x%08x (%d)\n", i, abi[i], GPR[i], (int32_t)GPR[i]);
	
	return 0;
}